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
    if(REALM_IPO_AVAILABLE AND NOT REALM_SANITIZERS)
        set_property(TARGET ${name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)

realm_library(realm_support src/util/Telemetry.cpp src/map/Camera.cpp src/objects/ProjectileProperties.cpp src/stage3D/TextureResidency.cpp src/stage3D/DrawTypes.cpp src/stage3D/WallGeometry.cpp)
realm_library(realm_platform src/platform/Platform.cpp)
realm_library(realm_network src/messaging/Protocol.cpp src/messaging/Messages.cpp src/net/Transport.cpp src/net/Http.cpp)
target_link_libraries(realm_network PUBLIC realm_support)
if(WIN32)
    target_link_libraries(realm_network PRIVATE ws2_32)
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
    realm_library(realm_audio src/sound/SoundEffectLibrary.cpp)
    target_link_libraries(realm_audio PRIVATE realm_network realm_miniaudio Threads::Threads)
    target_compile_definitions(realm_audio PRIVATE "REALM_SFX_ROOT_DEFAULT=\"${REALM_SFX_ROOT_PATH}\"")
endif()
realm_library(realm_game src/mapeditor/MapDocument.cpp src/tutorial/Tutorial.cpp src/game/IdleWatcher.cpp src/objects/Catalog.cpp src/objects/ObjectProperties.cpp src/util/Xml.cpp src/objects/particles/Particles.cpp src/map/WorldFeedback.cpp src/map/WorldParticleEffects.cpp
    src/map/WorldSoundEvents.cpp src/map/World.cpp src/map/Quest.cpp src/messaging/Session.cpp src/objects/Movement.cpp
    src/objects/Combat.cpp src/game/Inventory.cpp src/objects/Party.cpp src/appengine/SavedCharactersList.cpp src/appengine/CharacterFame.cpp src/appengine/Legend.cpp src/appengine/GuildMemberList.cpp src/parameters/Settings.cpp src/map/PostEffects.cpp)
target_link_libraries(realm_game PUBLIC realm_network PRIVATE LibXml2::LibXml2 ZLIB::ZLIB nlohmann_json::nlohmann_json)
# The Deca UI vector art (ui/deca/*.svg) is embedded in the headless assets library: HudArt draws the HUD buttons from it,
# and the RmlUi host's deca painter reads the same copy through vector_art::embedded_deca_svg.
file(GLOB REALM_DECA_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/deca/*.svg")
string(REPLACE ";" "|" REALM_DECA_FILE_ARGUMENT "${REALM_DECA_FILES}")
set(REALM_DECA_EMBEDDED "${GENERATED_DIR}/deca_files.cpp")
add_custom_command(OUTPUT "${REALM_DECA_EMBEDDED}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${REALM_DECA_EMBEDDED}" "-DROOT=${PROJECT_SOURCE_DIR}/ui/deca"
        "-DFILES=${REALM_DECA_FILE_ARGUMENT}" "-DHEADER=realm/assets/VectorArt.hpp" "-DNAMESPACE=realm::vector_art"
        "-DFUNCTION=embedded_deca_svg" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedUiFiles.cmake"
    DEPENDS ${REALM_DECA_FILES} "${PROJECT_SOURCE_DIR}/cmake/EmbedUiFiles.cmake" VERBATIM)
realm_library(realm_assets src/engine3d/Model3D.cpp src/engine3d/ConnectedObjects.cpp src/assets/Image.cpp src/assets/AnimationLibrary.cpp src/assets/ProcessedTextures.cpp src/assets/Appearances.cpp
    src/map/Terrain.cpp src/assets/FontBook.cpp src/assets/FlashDraw.cpp src/assets/VectorArt.cpp src/assets/HudArt.cpp src/background/BackdropData.cpp src/objects/StatusArt.cpp
    "${REALM_DECA_EMBEDDED}")
target_link_libraries(realm_assets PUBLIC realm_game PRIVATE PNG::PNG Freetype::Freetype ZLIB::ZLIB nlohmann_json::nlohmann_json)
realm_library(realm_ui src/mapeditor/EditorScreen.cpp src/tutorial/TutorialView.cpp src/hud/KeysView.cpp src/ui/panels/GuildInvite.cpp src/hud/Hud.cpp src/hud/HudState.cpp src/hud/CreditDisplay.cpp src/hud/SafeAreaDisplay.cpp src/ui/tooltip/SavedCharacterTooltip.cpp src/ui/tooltip/RankToolTip.cpp src/ui/tooltip/ClassToolTip.cpp src/hud/HudInput.cpp src/minimap/Minimap.cpp src/ui/tooltip/ItemTooltip.cpp src/ui/tooltip/ItemTooltipContent.cpp src/ui/tooltip/PlayerTooltip.cpp
    src/ui/menu/PlayerMenu.cpp src/ui/menu/PlayerGroup.cpp src/map/partyoverlay/PartyArrows.cpp src/map/partyoverlay/GameObjectArrow.cpp src/map/partyoverlay/QuestArrow.cpp
    src/ui/tooltip/QuestToolTip.cpp src/ui/tooltip/PortraitToolTip.cpp src/ui/Chat.cpp src/map/mapoverlay/SpeechBalloons.cpp src/ui/GraphicsUtil.cpp src/ui/dialogs/Dialog.cpp
    src/ui/Scrollbar.cpp src/ui/board/GuildBoard.cpp src/ui/guild/GuildMembers.cpp src/ui/Trade.cpp src/ui/dialogs/InteractionDialog.cpp src/ui/SkinChooser.cpp
    src/ui/options/OptionsScreen.cpp src/ui/tooltip/TextToolTip.cpp src/lib/ui/Stack.cpp)
target_link_libraries(realm_ui PUBLIC realm_assets)
realm_library(realm_fixtures src/fixtures/StressTimeline.cpp)
target_link_libraries(realm_fixtures PUBLIC realm_assets PRIVATE nlohmann_json::nlohmann_json)

if(REALM_BUILD_VIEWER)
    find_package(SDL2 REQUIRED CONFIG)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    realm_library(realm_render src/stage3D/Renderer.cpp src/stage3D/RendererPostProcess.cpp src/stage3D/RendererGpuTimers.cpp
        src/stage3D/RendererStream.cpp src/stage3D/Atlas.cpp src/map/Scene.cpp src/background/Backdrop.cpp)
    target_link_libraries(realm_render PUBLIC realm_assets PRIVATE OpenGL::GL GLEW::GLEW)
    realm_library(realm_ui_gpu src/minimap/MinimapGpu.cpp)
    target_link_libraries(realm_ui_gpu PUBLIC realm_ui realm_render)
    realm_library(realm_platform_sdl src/platform/PlatformSdl.cpp)
    target_link_libraries(realm_platform_sdl PUBLIC realm_platform SDL2::SDL2 PRIVATE OpenGL::GL GLEW::GLEW)
    target_compile_definitions(realm_platform_sdl PRIVATE SDL_MAIN_HANDLED)
    # Startup is split by concern (entry points and dispatch, frame and task completions, input, dialogs, fame and legends);
    # its HTTP task service and retained view model remain separate parts.
    realm_library(realm_startup src/screens/Startup.cpp src/screens/StartupDialogs.cpp src/screens/StartupFame.cpp src/screens/StartupHttpTasks.cpp src/screens/StartupInput.cpp src/screens/StartupUpdate.cpp src/screens/StartupViewModel.cpp)
    target_link_libraries(realm_startup PUBLIC realm_ui realm_render)
    # RmlUi's MIT SDL platform and GL3 renderer backends, vendored unmodified in third_party/rmlui.
    set(REALM_RMLUI_BACKENDS "${RMLUI_BACKENDS_DIR}/RmlUi_Renderer_GL3.cpp" "${RMLUI_BACKENDS_DIR}/RmlUi_Platform_SDL.cpp")
    # ui/*.rml and ui/*.rcss are embedded into the binary: the client has no loose UI files to ship. The Deca UI vector
    # art is embedded by realm_assets above.
    file(GLOB REALM_UI_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/*.rml" "${PROJECT_SOURCE_DIR}/ui/*.rcss")
    string(REPLACE ";" "|" REALM_UI_FILE_ARGUMENT "${REALM_UI_FILES}")
    set(REALM_UI_EMBEDDED "${GENERATED_DIR}/ui_files.cpp")
    add_custom_command(OUTPUT "${REALM_UI_EMBEDDED}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${REALM_UI_EMBEDDED}" "-DROOT=${PROJECT_SOURCE_DIR}/ui" "-DFILES=${REALM_UI_FILE_ARGUMENT}"
            "-DHEADER=realm/lib/ui/RmlHost.hpp" "-DNAMESPACE=realm::ui" "-DFUNCTION=embedded_ui_file"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedUiFiles.cmake"
        DEPENDS ${REALM_UI_FILES} "${PROJECT_SOURCE_DIR}/cmake/EmbedUiFiles.cmake" VERBATIM)
    realm_library(realm_ui_rmlui src/lib/ui/RmlHost.cpp "${REALM_UI_EMBEDDED}" ${REALM_RMLUI_BACKENDS})
    target_link_libraries(realm_ui_rmlui PUBLIC realm_ui PRIVATE RmlUi::Core SDL2::SDL2 OpenGL::GL GLEW::GLEW)
    target_include_directories(realm_ui_rmlui SYSTEM PRIVATE "${RMLUI_BACKENDS_DIR}")
    target_compile_definitions(realm_ui_rmlui PRIVATE RMLUI_GL3_CUSTOM_LOADER=<GL/glew.h> RMLUI_SDL_VERSION_MAJOR=2)
    set_source_files_properties(${REALM_RMLUI_BACKENDS} PROPERTIES COMPILE_OPTIONS -w)
    realm_library(realm_ui_views src/lib/ui/RetainedUi.cpp src/screens/StartupRml.cpp)
    target_link_libraries(realm_ui_views PUBLIC realm_ui_rmlui realm_startup realm_render PRIVATE RmlUi::Core SDL2::SDL2)
    realm_library(realm_application src/core/Application.cpp src/core/ApplicationState.cpp src/core/ApplicationLayersHud.cpp src/core/ApplicationLayersSocial.cpp src/core/ApplicationLayersScreens.cpp src/core/ApplicationLoop.cpp src/core/ApplicationLoopInput.cpp src/core/ApplicationLoopUpdate.cpp src/core/ApplicationLoopRender.cpp src/core/ApplicationLoopPanels.cpp src/core/ApplicationOptions.cpp src/core/ApplicationScript.cpp src/core/ApplicationFixtures.cpp src/core/ApplicationAudio.cpp)
    target_link_libraries(realm_application PRIVATE realm_startup realm_ui_views realm_platform_sdl realm_fixtures realm_ui_gpu)
    if(REALM_AUDIO)
        # The sound output bridge (src/core/ApplicationAudio.cpp) is inert without REALM_AUDIO; the define selects the real one.
        target_link_libraries(realm_application PRIVATE realm_audio)
        target_compile_definitions(realm_application PRIVATE REALM_AUDIO)
    endif()
    target_compile_definitions(realm_application PRIVATE SDL_MAIN_HANDLED)
endif()
