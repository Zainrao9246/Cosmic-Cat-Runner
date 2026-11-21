cmake_minimum_required(VERSION 3.16)
project(CosmoCatRun C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# raylib is fetched and compiled automatically the first time you configure
# the project. That means no manual library installation is needed — this
# works out of the box on GitHub Actions runners, Linux and macOS.
# ---------------------------------------------------------------------------
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED   OFF CACHE BOOL "" FORCE)   # static lib -> self-contained .exe (no DLLs to ship)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(cosmo-cat-run src/main.cpp)
target_link_libraries(cosmo-cat-run PRIVATE raylib)

if (WIN32)
    # static raylib still needs these Windows system libraries
    target_link_libraries(cosmo-cat-run PRIVATE winmm gdi32 shell32 opengl32)
elseif (UNIX AND NOT APPLE)
    target_link_libraries(cosmo-cat-run PRIVATE GL X11)
endif()

if (MSVC)
    target_compile_options(cosmo-cat-run PRIVATE /W3)
else()
    target_compile_options(cosmo-cat-run PRIVATE -Wall -Wextra)
endif()
