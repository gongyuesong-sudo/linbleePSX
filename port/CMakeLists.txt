cmake_minimum_required(VERSION 3.20)
project(bleem_port C)

# 无 root 安装的 SDL2 dev 头 (task-001 解包到 ~/.local/portroot)
set(ENV{PKG_CONFIG_PATH} "$ENV{HOME}/.local/portroot/usr/lib/x86_64-linux-gnu/pkgconfig:$ENV{PKG_CONFIG_PATH}")

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -O2)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
find_package(Vulkan REQUIRED)

add_executable(bleem_port
  src/main.c src/boot.c src/vk_host.c src/resources.c src/lzfmt.c)
target_include_directories(bleem_port PRIVATE src ${SDL2_INCLUDE_DIRS}
  "$ENV{HOME}/.local/portroot/usr/include/x86_64-linux-gnu")
target_link_libraries(bleem_port PRIVATE ${SDL2_LIBRARIES} Vulkan::Vulkan m)
target_link_directories(bleem_port PRIVATE ${SDL2_LIBRARY_DIRS})
