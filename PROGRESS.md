# Progress

## Current state

C++20 CLI visualization project. Stack fully configured:

- **Language**: C++20, Clang with `-Wall -Wextra -Wpedantic -Werror`
- **Test runner**: Google Test 1.15.0 (via Conan)
- **Build**: CMake 3.20+ with Conan 2 for dependency management
- **Pre-commit**: local hooks for cmake build + ctest
- **Conan installed via**: `uv tool install conan`

### Project structure
```
CMakeLists.txt          # Root build config (C++20, strict flags)
conanfile.txt           # Conan dependencies (gtest)
.pre-commit-config.yaml # Build + test hooks
tests/
  CMakeLists.txt        # Test target config
  smoke_test.cpp        # Smoke test verifying toolchain
.bedrock/stack.yml      # Stack metadata for /qa
```

### Build commands
```
conan install . --output-folder=build --build=missing -s compiler.cppstd=20
cmake --preset conan-release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Next

- Define project purpose and initial features
- Add src/ directory with library/executable targets
