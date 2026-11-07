#!/bin/sh
# Builds the scratch harness against the scratch build's libraries (same link line as realm_client).
set -e
# Usage: build.sh REALM_CPP_DIR BUILD_DIR (a Release build with Vulkan ON). Writes next to this script.
ROOT=${1:?realm-cpp dir}; B=${2:?build dir}; H=$(cd "$(dirname "$0")" && pwd)

cd $B
/usr/bin/c++ -O2 -std=c++20 -flto=auto -DREALM_VULKAN_ENABLED=1 -DSDL_MAIN_HANDLED   -I$ROOT/include -I$B/generated -I$ROOT/src -I$ROOT/src/stage3D   -I/usr/include/SDL2 -c $H/s23.cpp -o $H/s23.o
/usr/bin/c++ -O2 -flto=auto -fno-fat-lto-objects $H/s23.o -o $H/s23   librealm_application.a librealm_ui_views.a librealm_startup.a librealm_ui_rmlui.a third_party/rmlui/librmlui_core.a   librealm_platform_sdl.a /usr/lib/libSDL2-2.0.so.0.3200.74 librealm_platform.a librealm_fixtures.a librealm_ui_gpu.a   librealm_ui.a librealm_render.a /usr/lib/libGLX.so /usr/lib/libOpenGL.so /usr/lib/libGLEW.so librealm_assets.a   librealm_game.a /usr/lib/libxml2.so /usr/lib/libpng.so /usr/lib/libfreetype.so /usr/lib/libz.so librealm_audio.a   librealm_network.a librealm_support.a librealm_miniaudio.a -ldl
