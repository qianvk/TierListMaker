# Build Guide

TierListMaker uses CMake, Qt 6, C++20, and VKFrameless.

## Dependencies

- CMake 3.21 or newer.
- Ninja or another CMake generator.
- Qt 6.5 or newer with Core, Gui, Widgets, Svg, Concurrent, and Test.
- VKFrameless initialized as the `external/vkframeless` submodule.

## Configure

```bash
git submodule update --init --recursive
cmake -S . -B build/default -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x.x/platform"
```

## Build and Test

```bash
cmake --build build/default
ctest --test-dir build/default --output-on-failure
```

## User Presets

Copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` and replace the placeholder prefix paths. Do not commit the user preset.
