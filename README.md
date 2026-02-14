# UTQuery - UT2004 Server browser

![image](doc/utquery.png)

## Building

### Windows

Requires [vcpkg](https://github.com/microsoft/vcpkg) and CMake.

```
cmake --preset default
cmake --build build
```

### Linux

Requires [vcpkg](https://github.com/microsoft/vcpkg) and CMake.

```
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Arch Linux (package install)

```
makepkg -si
```
