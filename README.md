Tornado 3D - C++ OpenGL demo

This project renders a simple tornado-like mesh that follows the mouse cursor.

Requirements
- CMake >= 3.14
- A C++17 compiler (g++/clang)
- Internet access for CMake's FetchContent to download GLFW, GLAD and GLM (first configure)

Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Run

```bash
./tornado
```

Notes
- Shaders are copied by CMake to the build directory automatically.
- If the program can't find shaders, run it from the project root so paths resolve.
- If your system lacks OpenGL development packages (GL, X11), install them (e.g., on Ubuntu: `sudo apt install build-essential cmake libx11-dev libxrandr-dev libxi-dev libxcursor-dev libgl1-mesa-dev`)

Next steps / improvements
- Add particle system for dust and debris
- Add lighting and better material shading
- Use VBO indexing and LOD for performance
