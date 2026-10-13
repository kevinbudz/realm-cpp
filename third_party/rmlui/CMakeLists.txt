# Vendored RmlUi 6.3 core (see VENDORED.md). Built as a static library with no font engine: the client supplies
# FontBook through Rml::FontEngineInterface. Only the core and two backend files are kept.
file(GLOB_RECURSE RMLUI_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/*.cpp")
add_library(rmlui_core STATIC ${RMLUI_CORE_SOURCES})
add_library(RmlUi::Core ALIAS rmlui_core)
target_compile_features(rmlui_core PUBLIC cxx_std_17)
target_include_directories(rmlui_core SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include")
target_compile_definitions(rmlui_core PUBLIC RMLUI_STATIC_LIB PRIVATE "RMLUI_VERSION=\"6.3\"")
# Third-party code: keep the client's own warning policy out of it.
target_compile_options(rmlui_core PRIVATE -w)
set(RMLUI_BACKENDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Backends" PARENT_SCOPE)
