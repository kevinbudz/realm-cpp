# Emscripten (web) dependencies, included from CMakeLists.txt only when EMSCRIPTEN is set.
# The SDL, PNG, zlib and Freetype ports are compiled and linked through flags: the targets below are empty
# INTERFACE IMPORTED libraries that carry the names the native build links, so target_link_libraries lines
# are shared. GLEW and Threads are empty too (no GLEW on web, no pthreads).
# Every web change is behind EMSCRIPTEN; see docs/web/web-port.md.

# Port flags go on every compile and link: headers come from the port at compile time.
set(REALM_WEB_PORT_FLAGS -sUSE_SDL=2 -sUSE_LIBPNG=1 -sUSE_ZLIB=1 -sUSE_FREETYPE=1)
add_compile_options(-fwasm-exceptions ${REALM_WEB_PORT_FLAGS})
add_link_options(-fwasm-exceptions ${REALM_WEB_PORT_FLAGS})
# WebGPU (REALM_ENABLE_WEBGPU): emdawnwebgpu supplies <webgpu/webgpu.h> at compile time and the browser
# bindings at link time. emcc fetches the port on first use.
if(REALM_ENABLE_WEBGPU)
    add_compile_options(--use-port=emdawnwebgpu)
    add_link_options(--use-port=emdawnwebgpu)
endif()

foreach(web_target IN ITEMS SDL2::SDL2 PNG::PNG ZLIB::ZLIB Freetype::Freetype OpenGL::GL GLEW::GLEW Threads::Threads)
    add_library(${web_target} INTERFACE IMPORTED)
endforeach()

include(FetchContent)

# nlohmann_json is header-only. Fetched with the release tarball (pinned hash), not a system copy.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# libxml2 2.13.9 (latest 2.13 patch tag), built as a static library with the features the Flash XML loader needs.
set(LIBXML2_WITH_ICONV OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_LZMA OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_PYTHON OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_PROGRAMS OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_HTTP OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_FTP OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_THREADS OFF CACHE BOOL "" FORCE)
set(LIBXML2_WITH_ICU OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libxml2
    GIT_REPOSITORY https://gitlab.gnome.org/GNOME/libxml2.git
    GIT_TAG v2.13.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(libxml2)
if(TARGET LibXml2 AND NOT TARGET LibXml2::LibXml2)
    add_library(LibXml2::LibXml2 ALIAS LibXml2)
endif()
