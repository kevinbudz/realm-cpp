# Dependencies point from adapters/controllers to data and pure game systems.
# No headless target discovers, includes, or links the graphics SDK.
add_library(realm_build_options INTERFACE)
target_include_directories(realm_build_options INTERFACE "${PROJECT_SOURCE_DIR}/include" "${GENERATED_DIR}")
if(MSVC)
    target_compile_options(realm_build_options INTERFACE /W4 /permissive-)
else()
    target_compile_options(realm_build_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
if(REALM_SANITIZERS AND NOT MSVC)
    target_compile_options(realm_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(realm_build_options INTERFACE -fsanitize=address,undefined)
endif()
if(REALM_TRACY)
    target_link_libraries(realm_build_options INTERFACE Tracy::TracyClient)
    target_compile_definitions(realm_build_options INTERFACE REALM_TRACY)
endif()
if(REALM_ALLOCATION_TRACKING)
    target_compile_definitions(realm_build_options INTERFACE REALM_ALLOCATION_TRACKING)
endif()
check_ipo_supported(RESULT REALM_IPO_AVAILABLE OUTPUT REALM_IPO_ERROR)
function(realm_library name)
    add_library(${name} STATIC ${ARGN})
    target_link_libraries(${name} PUBLIC realm_build_options)
    add_dependencies(${name} flash_manifest)
endfunction()

find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)

realm_library(realm_support src/telemetry.cpp src/camera.cpp src/projectile.cpp src/residency.cpp src/draw_types.cpp)
# Preserve the existing IPO scope; changing optimization policy needs its own evidence.
if(REALM_IPO_AVAILABLE AND NOT REALM_SANITIZERS)
    set_property(TARGET realm_support PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
realm_library(realm_platform src/platform.cpp)
realm_library(realm_network src/protocol.cpp src/messages.cpp src/transport.cpp src/http.cpp)
target_link_libraries(realm_network PUBLIC realm_support)
if(WIN32)
    target_link_libraries(realm_network PRIVATE ws2_32)
endif()
# Release IPO for realm_network (protocol and messages).
if(REALM_IPO_AVAILABLE AND NOT REALM_SANITIZERS)
    set_property(TARGET realm_network PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
# Native SFX output. miniaudio 0.11.25 is vendored unmodified (third_party/miniaudio/README.md); only its output
# backend is compiled, so the engine, resource manager, node graph, generation and encoding modules are left out.
if(REALM_AUDIO)
    find_package(Threads REQUIRED)
    # Default asset root for the player; the REALM_SFX_ROOT environment variable overrides it at runtime.
    set(REALM_SFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../realm-server/Resources/Web/sfx" CACHE PATH
        "Default SFX asset root (NAME.mp3 files below it)")
    get_filename_component(REALM_SFX_ROOT_PATH "${REALM_SFX_ROOT}" ABSOLUTE)
    add_library(realm_miniaudio STATIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c")
    target_include_directories(realm_miniaudio SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio")
    target_compile_definitions(realm_miniaudio PRIVATE MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH
        MA_NO_GENERATION MA_NO_ENCODING MA_NO_FLAC)
    # Upstream warnings are not ours to fix; sanitizer flags still reach this target through realm_build_options.
    if(MSVC)
        target_compile_options(realm_miniaudio PRIVATE /w)
    else()
        target_compile_options(realm_miniaudio PRIVATE -w)
    endif()
    target_link_libraries(realm_miniaudio PUBLIC realm_build_options PRIVATE ${CMAKE_DL_LIBS})
    realm_library(realm_audio src/audio.cpp)
    target_link_libraries(realm_audio PRIVATE realm_network realm_miniaudio Threads::Threads)
    target_compile_definitions(realm_audio PRIVATE "REALM_SFX_ROOT_DEFAULT=\"${REALM_SFX_ROOT_PATH}\"")
endif()
realm_library(realm_game src/catalog.cpp src/particles.cpp src/world_feedback.cpp src/world_particles.cpp
    src/world_sounds.cpp src/world.cpp src/session.cpp src/movement.cpp
    src/combat.cpp src/inventory.cpp src/party.cpp src/account.cpp src/settings.cpp src/post_effects.cpp)
target_link_libraries(realm_game PUBLIC realm_network PRIVATE LibXml2::LibXml2 ZLIB::ZLIB nlohmann_json::nlohmann_json)
realm_library(realm_assets src/model.cpp src/image.cpp src/animation.cpp src/processed.cpp src/appearance.cpp
    src/terrain.cpp src/font.cpp src/flash_draw.cpp src/hud_art.cpp src/backdrop_data.cpp src/status_effects.cpp)
target_link_libraries(realm_assets PUBLIC realm_game PRIVATE PNG::PNG Freetype::Freetype ZLIB::ZLIB nlohmann_json::nlohmann_json)
realm_library(realm_ui src/hud.cpp src/credit_display.cpp src/safe_area_display.cpp src/saved_character_tooltip.cpp src/hud_input.cpp src/minimap.cpp src/tooltip.cpp src/item_tooltip_content.cpp src/player_tooltip.cpp
    src/player_menu.cpp src/player_group.cpp src/party_arrows.cpp src/chat.cpp src/ui_draw.cpp src/dialog.cpp
    src/scrollbar.cpp src/guild_board.cpp src/guild_members.cpp src/trade.cpp src/interaction.cpp src/skins.cpp
    src/options.cpp src/text_tooltip.cpp src/ui_stack.cpp)
target_link_libraries(realm_ui PUBLIC realm_assets)
realm_library(realm_fixtures src/stress.cpp)
target_link_libraries(realm_fixtures PUBLIC realm_assets PRIVATE nlohmann_json::nlohmann_json)

if(REALM_BUILD_VIEWER)
    find_package(SDL2 REQUIRED CONFIG)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    realm_library(realm_render src/renderer.cpp src/renderer_post_process.cpp src/renderer_gpu_timers.cpp
        src/renderer_stream.cpp src/atlas.cpp src/scene.cpp src/backdrop.cpp)
    target_link_libraries(realm_render PUBLIC realm_assets PRIVATE OpenGL::GL GLEW::GLEW)
    realm_library(realm_ui_gpu src/minimap_gpu.cpp)
    target_link_libraries(realm_ui_gpu PUBLIC realm_ui realm_render)
    realm_library(realm_platform_sdl src/platform_sdl.cpp)
    target_link_libraries(realm_platform_sdl PUBLIC realm_platform SDL2::SDL2 PRIVATE OpenGL::GL GLEW::GLEW)
    target_compile_definitions(realm_platform_sdl PRIVATE SDL_MAIN_HANDLED)
    # Startup keeps its screen controller, HTTP task service, and retained view model as separate parts.
    realm_library(realm_startup src/startup.cpp src/startup_http_tasks.cpp src/startup_view_model.cpp)
    target_link_libraries(realm_startup PUBLIC realm_ui realm_render)
    # RmlUi's MIT SDL platform and GL3 renderer backends, vendored unmodified in third_party/rmlui.
    set(REALM_RMLUI_BACKENDS "${RMLUI_BACKENDS_DIR}/RmlUi_Renderer_GL3.cpp" "${RMLUI_BACKENDS_DIR}/RmlUi_Platform_SDL.cpp")
    # ui/*.rml and ui/*.rcss are embedded into the binary: the client has no loose UI files to ship.
    file(GLOB REALM_UI_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/*.rml" "${PROJECT_SOURCE_DIR}/ui/*.rcss")
    string(REPLACE ";" "|" REALM_UI_FILE_ARGUMENT "${REALM_UI_FILES}")
    set(REALM_UI_EMBEDDED "${GENERATED_DIR}/ui_files.cpp")
    add_custom_command(OUTPUT "${REALM_UI_EMBEDDED}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${REALM_UI_EMBEDDED}" "-DFILES=${REALM_UI_FILE_ARGUMENT}" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedUiFiles.cmake"
        DEPENDS ${REALM_UI_FILES} "${PROJECT_SOURCE_DIR}/cmake/EmbedUiFiles.cmake" VERBATIM)
    realm_library(realm_ui_rmlui src/ui_rml.cpp "${REALM_UI_EMBEDDED}" ${REALM_RMLUI_BACKENDS})
    target_link_libraries(realm_ui_rmlui PUBLIC realm_ui PRIVATE RmlUi::Core SDL2::SDL2 OpenGL::GL GLEW::GLEW)
    target_include_directories(realm_ui_rmlui SYSTEM PRIVATE "${RMLUI_BACKENDS_DIR}")
    target_compile_definitions(realm_ui_rmlui PRIVATE RMLUI_GL3_CUSTOM_LOADER=<GL/glew.h> RMLUI_SDL_VERSION_MAJOR=2)
    set_source_files_properties(${REALM_RMLUI_BACKENDS} PROPERTIES COMPILE_OPTIONS -w)
    realm_library(realm_ui_views src/ui_retained.cpp src/startup_rml.cpp)
    target_link_libraries(realm_ui_views PUBLIC realm_ui_rmlui realm_startup realm_render PRIVATE RmlUi::Core SDL2::SDL2)
    realm_library(realm_application src/application.cpp src/application_state.cpp src/application_layers_hud.cpp src/application_layers_social.cpp src/application_layers_screens.cpp src/application_loop.cpp src/application_options.cpp src/application_script.cpp src/application_fixtures.cpp src/application_audio.cpp)
    target_link_libraries(realm_application PRIVATE realm_startup realm_ui_views realm_platform_sdl realm_fixtures realm_ui_gpu)
    if(REALM_AUDIO)
        # The sound output bridge (src/application_audio.cpp) is inert without REALM_AUDIO; the define selects the real one.
        target_link_libraries(realm_application PRIVATE realm_audio)
        target_compile_definitions(realm_application PRIVATE REALM_AUDIO)
    endif()
    target_compile_definitions(realm_application PRIVATE SDL_MAIN_HANDLED)
    if(REALM_IPO_AVAILABLE AND NOT REALM_SANITIZERS)
        set_property(TARGET realm_application realm_render realm_startup realm_platform_sdl PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()
