cmake_minimum_required(VERSION 3.20)

project(GnomeDDC LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(DDCUTIL REQUIRED IMPORTED_TARGET ddcutil)

add_executable(gnomeddc
    src/main.c
    src/ddcutil_client.c
    src/monitor_item.c
)

target_include_directories(gnomeddc PRIVATE src ${ADWAITA_INCLUDE_DIRS} ${DDCUTIL_INCLUDE_DIRS})

# pkg-config exposes additional compiler flags that we should respect.
target_compile_options(gnomeddc PRIVATE ${ADWAITA_CFLAGS_OTHER} ${DDCUTIL_CFLAGS_OTHER})

target_link_libraries(gnomeddc PRIVATE
    PkgConfig::ADWAITA
    PkgConfig::DDCUTIL
)

install(TARGETS gnomeddc RUNTIME DESTINATION bin)
