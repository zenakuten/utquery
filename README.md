# UTQuery - UT2004 Server browser

![image](doc/utquery.png)

## Running

### Linux
You will need a valid `cdkey` file (not provided) in the same folder as the executable for the internet tab to function.

### Windows
The app will read your installed key, if not found will try to read from `cdkey` file.  

## Building

### Windows

Requires [vcpkg](https://github.com/microsoft/vcpkg) and CMake.

```
cmake --preset default
cmake --build build
```

### Linux

Requires CMake. Clone [vcpkg](https://github.com/microsoft/vcpkg) if you don't have it:

```
git clone https://github.com/microsoft/vcpkg.git
```

Then build:

```
cmake -B build --fresh -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Arch Linux (package install)

```
makepkg -si
```
