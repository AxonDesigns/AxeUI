#!/usr/bin/env sh
# Regenerates tests/goldens/*.png. Run on the reference configuration only:
# Linux with Mesa lavapipe (apt install mesa-vulkan-drivers), after checking
# the rendering change is intended. Review the image diff before committing.
#
#   tools/update_goldens.sh [build-dir]   (default: build)
set -eu
build_dir="${1:-build}"
cmake --build "$build_dir" --target ui_render_tests
UI_UPDATE_GOLDENS=1 ctest --test-dir "$build_dir" -R '^ui_render_tests$' --output-on-failure
git status --short tests/goldens
