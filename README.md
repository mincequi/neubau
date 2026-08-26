# neubau

Starter CMake-based C++ scaffold using CPM.cmake for dependencies, libhv as the
application framework, cmrc for embedded resources, and ReactivePlusPlus for
reactive programming.

## Layout

- `cmake/` - CMake helper modules
- `resources/` - embedded application assets
- `src/` - application sources

## Build

```bash
cmake -S . -B build
cmake --build build
./build/neubau
```