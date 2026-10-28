# Desktop build of the dashboard UI: renders every page to PNG and runs the
# protocol tests, using the exact drawing code from firmware/cyd_dashboard.
#
#   git clone --depth 1 https://github.com/lovyan03/LovyanGFX.git
#   cmake -S . -B build -DLGFX_DIR=LovyanGFX && cmake --build build -j
#   ./build/ui_preview ../../docs        # writes docs/screen_*.png
#   ./build/test_protocol
cmake_minimum_required(VERSION 3.10)
project(ui_preview C CXX)

set(LGFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/LovyanGFX" CACHE PATH "Path to a LovyanGFX checkout")
set(L "${LGFX_DIR}/src")
if(NOT EXISTS "${L}/LovyanGFX.hpp")
  message(FATAL_ERROR "LovyanGFX not found at ${LGFX_DIR}. Clone it or pass -DLGFX_DIR=...")
endif()

add_definitions(-DLGFX_LINUX_FB)  # desktop back end; we only use off-screen sprites
file(GLOB LGFX_SRC
  ${L}/lgfx/Fonts/efont/*.c ${L}/lgfx/Fonts/IPA/*.c ${L}/lgfx/Fonts/lvgl/*.c
  ${L}/lgfx/utility/*.c ${L}/lgfx/v1/*.cpp ${L}/lgfx/v1/panel/*.cpp ${L}/lgfx/v1/touch/*.cpp
  ${L}/lgfx/v1/lv_font/*.c ${L}/lgfx/v1/platforms/framebuffer/*.cpp ${L}/lgfx/v1/misc/*.cpp)
add_library(lgfx STATIC ${LGFX_SRC})
target_include_directories(lgfx PUBLIC ${L})
target_compile_features(lgfx PUBLIC cxx_std_17)
target_compile_options(lgfx PRIVATE -w)

set(FW "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/cyd_dashboard")

add_executable(ui_preview preview.cpp)
target_include_directories(ui_preview PRIVATE ${FW})
target_link_libraries(ui_preview lgfx pthread)
target_compile_options(ui_preview PRIVATE -Wall -Wextra)

add_executable(test_protocol test_protocol.cpp)
target_include_directories(test_protocol PRIVATE ${FW})
target_compile_options(test_protocol PRIVATE -Wall -Wextra)
