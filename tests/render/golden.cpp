#include "render/golden.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_CONVERSIONS
#include <stb_image.h>
#include <stb_image_write.h>

namespace ui::test {

namespace {

std::filesystem::path golden_path(std::string_view name) {
  return std::filesystem::path(UI_GOLDEN_DIR) / (std::string(name) + ".png");
}

std::filesystem::path output_path(std::string_view name, std::string_view suffix) {
  std::filesystem::path dir(UI_GOLDEN_OUTPUT_DIR);
  std::error_code ignored;
  std::filesystem::create_directories(dir, ignored);
  return dir / (std::string(name) + std::string(suffix) + ".png");
}

bool write_png(const std::filesystem::path& path, const std::uint8_t* rgba, std::uint32_t width,
               std::uint32_t height) {
  return stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                        rgba, static_cast<int>(width * 4)) != 0;
}

}  // namespace

bool golden_update_requested() {
  const char* value = std::getenv("UI_UPDATE_GOLDENS");
  return value != nullptr && std::string_view(value) == "1";
}

GoldenResult compare_with_golden(std::string_view name, std::span<const std::uint8_t> rgba,
                                 std::uint32_t width, std::uint32_t height,
                                 const GoldenTolerance& tolerance) {
  GoldenResult result;
  const std::filesystem::path golden = golden_path(name);

  if (golden_update_requested()) {
    std::error_code ignored;
    std::filesystem::create_directories(golden.parent_path(), ignored);
    result.passed = write_png(golden, rgba.data(), width, height);
    result.message = result.passed ? "golden updated: " + golden.string()
                                   : "failed to write golden " + golden.string();
    return result;
  }

  int golden_width = 0;
  int golden_height = 0;
  int channels = 0;
  stbi_uc* expected =
      stbi_load(golden.string().c_str(), &golden_width, &golden_height, &channels, 4);
  if (expected == nullptr) {
    write_png(output_path(name, ".actual"), rgba.data(), width, height);
    result.message = "missing golden " + golden.string() +
                     " (generate it with tools/update_goldens.sh on the reference configuration)";
    return result;
  }
  if (static_cast<std::uint32_t>(golden_width) != width ||
      static_cast<std::uint32_t>(golden_height) != height) {
    stbi_image_free(expected);
    write_png(output_path(name, ".actual"), rgba.data(), width, height);
    result.message = "size mismatch: golden is " + std::to_string(golden_width) + "x" +
                     std::to_string(golden_height) + ", actual " + std::to_string(width) + "x" +
                     std::to_string(height);
    return result;
  }

  const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
  std::vector<std::uint8_t> diff(pixel_count * 4);
  for (std::size_t i = 0; i < pixel_count; ++i) {
    int worst = 0;
    for (std::size_t c = 0; c < 4; ++c) {
      worst = std::max(worst, std::abs(int{rgba[i * 4 + c]} - int{expected[i * 4 + c]}));
    }
    result.max_channel_difference = std::max(result.max_channel_difference, worst);
    std::uint8_t* d = &diff[i * 4];
    if (worst > tolerance.per_channel) {
      ++result.differing_pixels;
      d[0] = 255;
      d[1] = 0;
      d[2] = 255;
    } else {
      // Dimmed grayscale of the expected image for context.
      const auto gray = static_cast<std::uint8_t>(
          (int{expected[i * 4]} + int{expected[i * 4 + 1]} + int{expected[i * 4 + 2]}) / 9);
      d[0] = gray;
      d[1] = gray;
      d[2] = gray;
    }
    d[3] = 255;
  }
  stbi_image_free(expected);

  result.passed = result.differing_pixels <= tolerance.max_differing_pixels;
  if (!result.passed) {
    write_png(output_path(name, ".actual"), rgba.data(), width, height);
    write_png(output_path(name, ".diff"), diff.data(), width, height);
    result.message = std::to_string(result.differing_pixels) + " pixel(s) differ by more than " +
                     std::to_string(tolerance.per_channel) + " (max difference " +
                     std::to_string(result.max_channel_difference) + "); wrote " +
                     output_path(name, ".actual").string() + " and .diff.png";
  }
  return result;
}

}  // namespace ui::test
