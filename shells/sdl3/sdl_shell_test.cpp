// SDL3 shell tests on SDL's "dummy" video driver: real SDL windows and event
// loop without a display. Windows report no native surface there, so frames
// go through the renderer's headless path. Lives in shells/sdl3 because it
// injects SDL events (SDL types stay inside this directory).

#include "sdl3/sdl_shell.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <thread>

#include "frame/frame_driver.h"
#include "platform/core.h"
#include "render/render_fixture.h"

using namespace ui::foundation;
using namespace ui::platform;
using ui::sdl3::SdlShell;
using ui::test::LogCapture;

namespace {

int g_paints = 0;

void paint(void*, SurfaceId, ui::display_list::DisplayListBuilder& b) {
  ++g_paints;
  b.fill_rect(LogicalRect::from_xywh(0, 0, 10, 10), LinearRgbaPremul{1, 0, 0, 1});
}

void push(SDL_Event event) { CHECK(SDL_PushEvent(&event)); }

}  // namespace

UI_TEST_CASE("SDL shell: windows, input translation, frames, wake-up and quit") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  LogCapture log(LogLevel::Warning);
  auto shell = SdlShell::create(memory(), log.logger());
  UI_REQUIRE(shell.has_value());
  CHECK((*shell)->video_driver() == "dummy");
  {
    Core core(**shell, memory(), log.logger());
    auto renderer = ui::test::make_renderer(memory(), log.logger());
    if (!renderer) {
      return;
    }
    g_paints = 0;
    ui::frame::FrameDriver driver(core, *renderer, memory(), &paint, nullptr);

    auto a = (*shell)->create_window({"a", 320, 200, true});
    auto b = (*shell)->create_window({"b", 160, 100, true});
    UI_REQUIRE(a.has_value());
    UI_REQUIRE(b.has_value());
    CHECK(core.surface_count() == 2);
    CHECK((*shell)->find_window((*a)->id()) == *a);
    CHECK((*a)->native_handle().is_none());  // The dummy driver has no OS surface.
    const SurfaceInfo info_a = (*core.surface((*a)->id()))->info;
    CAPTURE(info_a.physical_size.width);
    CAPTURE(info_a.physical_size.height);
    CAPTURE(info_a.scale.value());
    CHECK(info_a.logical_size() == LogicalSize{320, 200});

    // First frames are due immediately.
    CHECK((*shell)->pump());
    CHECK(driver.frames_presented() == 2);
    CHECK(g_paints == 2);

    // Mouse motion and a button press reach the core in logical pixels.
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = (*a)->sdl_window_id();
    motion.motion.which = 1;
    motion.motion.x = 40;
    motion.motion.y = 30;
    push(motion);
    SDL_Event press{};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.windowID = (*a)->sdl_window_id();
    press.button.which = 1;
    press.button.button = SDL_BUTTON_LEFT;
    press.button.down = true;
    press.button.x = 41;
    press.button.y = 31;
    push(press);
    // Keys: physical scancode, logical key and repeat.
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.windowID = (*a)->sdl_window_id();
    key.key.scancode = SDL_SCANCODE_A;
    key.key.key = SDLK_A;
    key.key.mod = SDL_KMOD_LSHIFT;
    key.key.down = true;
    key.key.repeat = true;
    push(key);
    // IME: preedit then the committing text input.
    SDL_Event edit{};
    edit.type = SDL_EVENT_TEXT_EDITING;
    edit.edit.windowID = (*a)->sdl_window_id();
    edit.edit.text = "にほ";
    edit.edit.start = 1;
    edit.edit.length = 1;
    push(edit);
    CHECK((*shell)->pump());
    const SurfaceState& state = **core.surface((*a)->id());
    UI_REQUIRE(state.pointers.size() == 1);
    CHECK(state.pointers[0].position == LogicalVec2{41, 31});
    CHECK(state.pointers[0].buttons.has(PointerButton::Primary));
    CHECK(core.modifiers().has(Modifier::Shift));
    CHECK(state.preedit.text == "にほ");
    CHECK(state.preedit.cursor_begin == 3);  // One character = 3 UTF-8 bytes.
    CHECK(state.preedit.cursor_end == 6);

    SDL_Event commit{};
    commit.type = SDL_EVENT_TEXT_INPUT;
    commit.text.windowID = (*a)->sdl_window_id();
    commit.text.text = "日本";
    push(commit);
    CHECK((*shell)->pump());
    CHECK((**core.surface((*a)->id())).preedit.text.empty());

    // Close requests are recorded per window; the app decides.
    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = (*b)->sdl_window_id();
    push(close);
    CHECK((*shell)->pump());
    CHECK((*core.surface((*b)->id()))->close_requested);
    CHECK_FALSE((*core.surface((*a)->id()))->close_requested);
    const SurfaceId b_id = (*b)->id();
    (*shell)->destroy_window(*b);
    CHECK(core.surface_count() == 1);
    CHECK_FALSE(core.surface(b_id).has_value());

    // Idle: nothing pending, so pump() sleeps until woken from another thread.
    while ((**core.surface((*a)->id())).frame_requested) {
      CHECK((*shell)->pump());
    }
    const std::uint64_t frames = driver.frames_presented();
    const auto start = std::chrono::steady_clock::now();
    std::thread waker([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      (*shell)->wake();
    });
    CHECK((*shell)->pump());
    const auto waited = std::chrono::steady_clock::now() - start;
    waker.join();
    CHECK(waited >= std::chrono::milliseconds(80));  // Slept; did not spin.
    CHECK(driver.frames_presented() == frames);      // No frame without a request.

    // Quit.
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    push(quit);
    CHECK_FALSE((*shell)->pump());
    (*shell)->destroy_window(*a);
  }
  for (const auto& record : log.records()) {
    MESSAGE(record.message);
  }
  CHECK(log.records().empty());
}
