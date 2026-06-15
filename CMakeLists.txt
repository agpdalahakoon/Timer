cmake_minimum_required(VERSION 3.0)
project(timer)
set(CMAKE_CXX_STANDARD 17)

# Find GTK3 and X11 idle-detection packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0 gtk+-x11-3.0 x11 xscrnsaver)

file(GLOB SOURCES
    "src/main.cpp"
)

add_compile_options(-fpermissive)
include_directories(./)
include_directories(${GTK3_INCLUDE_DIRS})

add_executable(timer ${SOURCES})


target_link_libraries(timer ${GTK3_LIBRARIES})

add_definitions(${GTK3_CFLAGS_OTHER})
