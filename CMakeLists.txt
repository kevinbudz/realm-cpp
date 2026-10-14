cmake_minimum_required(VERSION 3.22)
project(realm_cpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(CheckIPOSupported)
option(REALM_BUILD_VIEWER "Build the graphical client (realm_client)" ON)
option(REALM_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(REALM_TRACY "Enable pinned optional Tracy CPU profiler" OFF)
option(REALM_ALLOCATION_TRACKING "Track main-thread C++ allocation traffic in viewer telemetry" OFF)
option(REALM_AUDIO "Build the native one-shot SFX output (vendored miniaudio, realm_audio)" ON)
option(REALM_USE_LLD "Link with LLVM lld when supported; Release keeps GNU ld because its GCC LTO objects need the GCC plugin" ON)
# Build speed: each block is a no-op when its tool is missing or the cache already sets it.
if(CMAKE_GENERATOR MATCHES "Ninja")
    # Link steps are memory-heavy (ASan, LTO); keep at most two in flight.
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS realm_link=2)
    set(CMAKE_JOB_POOL_LINK realm_link)
endif()
find_program(REALM_CCACHE_PROGRAM ccache)
if(REALM_CCACHE_PROGRAM)
    if(NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${REALM_CCACHE_PROGRAM}")
    endif()
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${REALM_CCACHE_PROGRAM}")
    endif()
endif()
include(CheckLinkerFlag)
# Release IPO (GCC -flto static libraries) links fail under lld without the GCC LTO plugin, and non-IPO test
# executables link those same libraries, so the whole Release configuration keeps the default linker.
if(REALM_USE_LLD AND NOT CMAKE_LINKER_TYPE AND NOT (CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT REALM_SANITIZERS))
    check_linker_flag(CXX "-fuse-ld=lld" REALM_HAS_LLD)
    if(REALM_HAS_LLD)
        set(CMAKE_LINKER_TYPE LLD)
    endif()
endif()
if(REALM_TRACY)
    include(FetchContent)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST ON CACHE BOOL "" FORCE)
    set(TRACY_NO_CODE_TRANSFER ON CACHE BOOL "" FORCE)
    FetchContent_Declare(tracy URL https://codeload.github.com/wolfpld/tracy/tar.gz/refs/tags/v0.11.1
        URL_HASH SHA256=2c11ca816f2b756be2730f86b0092920419f3dabc7a7173829ffd897d91888a1)
    FetchContent_MakeAvailable(tracy)
endif()
if(REALM_BUILD_VIEWER)
    # Vendored, pinned RmlUi 6.3 core (third_party/rmlui/VENDORED.md); no download at configure time.
    add_subdirectory(third_party/rmlui)
endif()
set(REALM_FLASH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../realm-client" CACHE PATH "Authoritative Flash source")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE FLASH_INPUTS CONFIGURE_DEPENDS "${REALM_FLASH_ROOT}/src/*")
if(EXISTS "${REALM_FLASH_ROOT}/.git/logs/HEAD")
    list(APPEND FLASH_INPUTS "${REALM_FLASH_ROOT}/.git/logs/HEAD")
endif()
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${GENERATED_DIR}/flash_assets.hpp" "${GENERATED_DIR}/source_inventory.json"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_flash.py"
        --flash-root "${REALM_FLASH_ROOT}" --output "${GENERATED_DIR}"
    DEPENDS ${FLASH_INPUTS} tools/import_flash.py VERBATIM)
add_custom_command(OUTPUT "${GENERATED_DIR}/flash_bitmap_colors.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_bitmap_colors.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/bitmap-colors.bin" "${GENERATED_DIR}/flash_bitmap_colors.hpp"
    DEPENDS tools/compile_bitmap_colors.py assets/bitmap-colors.bin assets/bitmap-colors.json VERBATIM)
add_custom_command(OUTPUT "${GENERATED_DIR}/flash_party_transforms.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_party_transforms.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/party-transforms.bin" "${GENERATED_DIR}/flash_party_transforms.hpp"
    DEPENDS tools/compile_party_transforms.py assets/party-transforms.bin assets/party-transforms.json VERBATIM)
add_custom_command(OUTPUT "${GENERATED_DIR}/flash_particle_palettes.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_particle_palettes.py"
        "${GENERATED_DIR}/source_inventory.json" "${GENERATED_DIR}/flash_particle_palettes.hpp"
    DEPENDS "${GENERATED_DIR}/source_inventory.json" ${FLASH_INPUTS} tools/compile_particle_palettes.py VERBATIM)
add_custom_command(OUTPUT "${GENERATED_DIR}/flash_models.hpp"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_models.py"
        "${GENERATED_DIR}/source_inventory.json" "${GENERATED_DIR}/flash_models.hpp"
    DEPENDS "${GENERATED_DIR}/source_inventory.json" ${FLASH_INPUTS} tools/compile_models.py VERBATIM)
add_custom_target(flash_manifest DEPENDS "${GENERATED_DIR}/flash_models.hpp" "${GENERATED_DIR}/flash_particle_palettes.hpp" "${GENERATED_DIR}/flash_assets.hpp" "${GENERATED_DIR}/flash_bitmap_colors.hpp" "${GENERATED_DIR}/flash_party_transforms.hpp")

include(cmake/ClientTargets.cmake)

add_executable(realm_session src/SessionMain.cpp)
target_link_libraries(realm_session PRIVATE realm_game realm_ui)

add_executable(realm_stress tools/StressInputs.cpp)
target_link_libraries(realm_stress PRIVATE nlohmann_json::nlohmann_json realm_assets realm_fixtures realm_game realm_support)

add_executable(realm_dye_session_inputs tests/DyeSessionInputs.cpp)
target_link_libraries(realm_dye_session_inputs PRIVATE nlohmann_json::nlohmann_json realm_assets realm_game realm_ui)

if(REALM_BUILD_VIEWER)
    # realm_client is the only product binary; it links the coordinator, platform and render implementations.
    add_executable(realm_client src/Main.cpp)
    target_link_libraries(realm_client PRIVATE realm_application)
    if(REALM_ALLOCATION_TRACKING)
        target_sources(realm_client PRIVATE src/util/AllocationTracking.cpp)
    endif()
    if(REALM_IPO_AVAILABLE AND NOT REALM_SANITIZERS)
        set_property(TARGET realm_client PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()

include(cmake/RealmTests.cmake)
if(BUILD_TESTING)
    realm_add_test(NAME legends TARGET realm_legends_tests LABELS unit
        SOURCES tests/LegendsTests.cpp LINK realm_game
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legends.xml")
    realm_add_test(NAME fame TARGET realm_fame_tests LABELS unit
        SOURCES tests/FameTests.cpp LINK realm_game
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/death-fame.xml")
    realm_add_test(NAME stress_session_guards TARGET realm_stress LABELS unit ARGS --guards)
    realm_add_python_test(NAME stress_timeline LABELS unit
        SCRIPT tests/stress_tests.py ARGS "$<TARGET_FILE:realm_stress>")
    realm_add_python_test(NAME stress_packets LABELS unit
        SCRIPT tests/stress_packets_tests.py
        ARGS "$<TARGET_FILE:realm_stress>" "${CMAKE_CURRENT_BINARY_DIR}/stress-packets")
    foreach(working_set 16 512)
        realm_add_test(NAME stress_icons_${working_set} TARGET realm_stress LABELS unit
            ARGS --icons ${working_set} 24
                "${CMAKE_CURRENT_BINARY_DIR}/stress-icons-${working_set}.json"
                "${CMAKE_CURRENT_BINARY_DIR}/stress-icons-${working_set}.jsonl"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()
    if(REALM_BUILD_VIEWER)
        realm_add_python_test(NAME stress_rendered LABELS gpu
            SCRIPT tests/stress_tests.py
            ARGS "$<TARGET_FILE:realm_stress>" "$<TARGET_FILE:realm_client>")
    endif()
    realm_add_test(NAME telemetry TARGET realm_telemetry_tests LABELS unit
        SOURCES tests/TelemetryTests.cpp LINK nlohmann_json::nlohmann_json realm_support
        ARGS "${CMAKE_CURRENT_BINARY_DIR}/telemetry-test")
    if(REALM_AUDIO)
        realm_add_test(NAME audio TARGET realm_audio_tests LABELS unit
            SOURCES tests/AudioTests.cpp LINK realm_audio
            ARGS "${CMAKE_CURRENT_BINARY_DIR}/audio-test")
        # Game events queue the Flash sounds they should (SoundCues), checked against the real catalog; CPU only.
        realm_add_test(NAME audio_events TARGET realm_audio_events_tests LABELS unit
            SOURCES tests/AudioEventsTests.cpp LINK realm_game realm_audio)
    endif()
    if(REALM_ALLOCATION_TRACKING)
        realm_add_test(NAME allocation_tracking TARGET realm_allocation_tracking_tests LABELS unit
            SOURCES tests/AllocationTrackingTests.cpp src/util/AllocationTracking.cpp
            LINK nlohmann_json::nlohmann_json realm_support
            ARGS "${CMAKE_CURRENT_BINARY_DIR}/allocation-test.jsonl")
    endif()
    if(REALM_BUILD_VIEWER)
        realm_add_python_test(NAME telemetry_gpu LABELS gpu
            SCRIPT tests/telemetry_gpu_tests.py ARGS "$<TARGET_FILE:realm_client>")
    endif()
    add_executable(realm_postfx_session_inputs tests/PostfxSessionInputs.cpp)
    target_link_libraries(realm_postfx_session_inputs PRIVATE realm_game)
    add_executable(realm_status_session_inputs tests/StatusSessionInputs.cpp)
    target_link_libraries(realm_status_session_inputs PRIVATE realm_game)
    add_executable(realm_particle_session_inputs tests/ParticleSessionInputs.cpp)
    target_link_libraries(realm_particle_session_inputs PRIVATE realm_game)
    add_executable(realm_model_session_inputs tests/ModelSessionInputs.cpp)
    target_link_libraries(realm_model_session_inputs PRIVATE realm_assets realm_game)
    realm_add_test(NAME status_rules TARGET realm_status_rules_tests LABELS unit
        SOURCES tests/StatusRulesTests.cpp LINK realm_assets)
    realm_add_test(NAME postfx_rules TARGET realm_postfx_rules_tests LABELS unit
        SOURCES tests/PostfxRulesTests.cpp LINK realm_assets realm_game)
    realm_add_test(NAME models TARGET realm_model_tests LABELS unit
        SOURCES tests/ModelTests.cpp LINK realm_assets realm_game)
    realm_add_test(NAME particles TARGET realm_particle_tests LABELS unit
        SOURCES tests/ParticleTests.cpp LINK realm_game)
    realm_add_test(NAME dyes_masks TARGET realm_dye_tests LABELS unit
        SOURCES tests/DyeTests.cpp LINK realm_assets realm_ui)
    if(REALM_BUILD_VIEWER)
        realm_add_test(NAME postfx TARGET realm_postfx_tests LABELS gpu
            SOURCES tests/PostfxTests.cpp
            LINK GLEW::GLEW OpenGL::GL SDL2::SDL2 realm_game realm_render
            DEFINES SDL_MAIN_HANDLED)
        realm_add_test(NAME visibility_culling TARGET realm_visibility_tests LABELS gpu
            SOURCES tests/VisibilityTests.cpp
            LINK GLEW::GLEW OpenGL::GL SDL2::SDL2 realm_assets realm_render
            DEFINES SDL_MAIN_HANDLED)
        realm_add_test(NAME frame_caches TARGET realm_frame_cache_tests LABELS gpu
            SOURCES tests/FrameCacheTests.cpp LINK realm_ui_gpu realm_platform_sdl)
        realm_add_test(NAME model_instances TARGET realm_model_instance_tests LABELS gpu
            SOURCES tests/ModelInstanceTests.cpp
            LINK realm_render GLEW::GLEW OpenGL::GL SDL2::SDL2 realm_assets realm_game
            DEFINES SDL_MAIN_HANDLED)
        realm_add_test(NAME dynamic_atlas TARGET realm_dynamic_atlas_tests LABELS gpu
            SOURCES tests/DynamicAtlasTests.cpp
            LINK GLEW::GLEW OpenGL::GL SDL2::SDL2 realm_assets realm_render
            DEFINES SDL_MAIN_HANDLED)
        realm_add_python_test(NAME status_gpu LABELS gpu SCRIPT tests/status_gpu_tests.py)
        realm_add_python_test(NAME options_gpu LABELS gpu SCRIPT tests/options_gpu_tests.py)
        # The RmlUi host's retained invalidation, input, clipping and lifetime on real fonts, art and GL.
        realm_add_test(NAME ui_rml_host TARGET realm_ui_rml_host_tests LABELS gpu
            SOURCES tests/UiRmlHostTests.cpp
            LINK realm_ui_rmlui realm_platform_sdl RmlUi::Core GLEW::GLEW OpenGL::GL SDL2::SDL2
            DEFINES SDL_MAIN_HANDLED)
        realm_add_test(NAME ui_retained TARGET realm_ui_retained_tests LABELS gpu
            SOURCES tests/UiRetainedTests.cpp
            LINK realm_ui_views realm_platform_sdl GLEW::GLEW OpenGL::GL SDL2::SDL2
            DEFINES SDL_MAIN_HANDLED)
        realm_add_python_test(NAME ui_bundle_gpu LABELS gpu SCRIPT tests/ui_bundle_gpu_tests.py
            ARGS --binary-dir "${CMAKE_CURRENT_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/ui-bundle")
        realm_add_test(NAME sdl_keys TARGET realm_sdl_keys_tests LABELS gpu
            SOURCES tests/SdlKeysTests.cpp LINK SDL2::SDL2 realm_game realm_platform_sdl)
        realm_add_python_test(NAME legends_gpu LABELS gpu SCRIPT tests/legends_gpu_tests.py
            ARGS --binary-dir "${CMAKE_CURRENT_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/legends-gpu")
        realm_add_python_test(NAME death_fame_gpu LABELS gpu SCRIPT tests/death_fame_gpu_tests.py
            ARGS --binary-dir "${CMAKE_CURRENT_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/death-fame-gpu")
        realm_add_python_test(NAME delete_character_gpu LABELS gpu SCRIPT tests/delete_character_gpu_tests.py
            ARGS --binary-dir "${CMAKE_CURRENT_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/delete-character-gpu")
        add_executable(realm_delete_stale_inputs tests/DeleteStaleInputs.cpp)
        target_link_libraries(realm_delete_stale_inputs PRIVATE realm_startup)
        realm_add_python_test(NAME delete_character_stale LABELS unit
            SCRIPT tests/delete_stale_tests.py ARGS "$<TARGET_FILE:realm_delete_stale_inputs>")
    endif()
    add_executable(realm_skin_session_inputs tests/SkinSessionInputs.cpp)
    target_link_libraries(realm_skin_session_inputs PRIVATE realm_game realm_ui)
    add_executable(realm_skin_http_inputs tests/SkinHttpInputs.cpp)
    target_link_libraries(realm_skin_http_inputs PRIVATE realm_ui)
    realm_add_python_test(NAME skins_http LABELS unit
        SCRIPT tests/skin_http_tests.py ARGS "$<TARGET_FILE:realm_skin_http_inputs>")
    realm_add_test(NAME skins TARGET realm_skin_tests LABELS unit
        SOURCES tests/SkinTests.cpp LINK realm_ui)
    realm_add_test(NAME ui_stack TARGET realm_ui_stack_tests LABELS unit
        SOURCES tests/UiStackTests.cpp LINK realm_ui)
    realm_add_test(NAME hud_credit_safe_area TARGET realm_hud_credit_safe_area_tests LABELS unit
        SOURCES tests/HudCreditSafeAreaTests.cpp LINK realm_ui)
    realm_add_test(NAME account_header TARGET realm_account_header_tests LABELS unit
        SOURCES tests/AccountHeaderTests.cpp LINK realm_ui)
    realm_add_test(NAME ui_surfaces TARGET realm_ui_resize_tests LABELS unit
        SOURCES tests/UiResizeTests.cpp LINK realm_ui)
    realm_add_test(NAME core_parity TARGET realm_core_tests LABELS unit
        SOURCES tests/CoreTests.cpp LINK realm_network realm_support)
    realm_add_test(NAME catalog_world TARGET realm_data_tests LABELS unit
        SOURCES tests/DataTests.cpp LINK realm_game)
    realm_add_test(NAME player_movement TARGET realm_movement_tests LABELS unit
        SOURCES tests/MovementTests.cpp LINK realm_game)
    realm_add_test(NAME projectile_combat TARGET realm_combat_tests LABELS unit
        SOURCES tests/CombatTests.cpp LINK realm_game)
    realm_add_test(NAME projectile_target_search TARGET realm_combat_target_tests LABELS unit
        SOURCES tests/CombatTargetTests.cpp LINK realm_game)
    realm_add_test(NAME world_queries TARGET realm_world_query_tests LABELS unit
        SOURCES tests/WorldQueryTests.cpp LINK realm_assets realm_game realm_ui)
    realm_add_test(NAME appearance_compose TARGET realm_compose_tests LABELS unit
        SOURCES tests/ComposeTests.cpp LINK realm_assets)
    realm_add_test(NAME ui_rectangle TARGET realm_ui_rectangle_tests LABELS unit
        SOURCES tests/UiRectangleTests.cpp LINK realm_assets)
    realm_add_test(NAME world_storage TARGET realm_world_storage_tests LABELS unit
        SOURCES tests/WorldStorageTests.cpp LINK realm_game)
    realm_add_test(NAME texture_residency TARGET realm_texture_residency_tests LABELS unit
        SOURCES tests/TextureResidencyTests.cpp LINK realm_support)
    realm_add_test(NAME party_members TARGET realm_party_tests LABELS unit
        SOURCES tests/PartyTests.cpp LINK realm_game)
    realm_add_test(NAME inventory_actions TARGET realm_inventory_tests LABELS unit
        SOURCES tests/InventoryTests.cpp LINK realm_game realm_ui)
    realm_add_test(NAME interaction_panels TARGET realm_interaction_tests LABELS unit
        SOURCES tests/InteractionTests.cpp LINK realm_ui)
    realm_add_test(NAME item_tooltips TARGET realm_tooltip_tests LABELS unit
        SOURCES tests/TooltipTests.cpp LINK realm_game realm_ui)
    realm_add_test(NAME player_tooltips TARGET realm_player_tooltip_tests LABELS unit
        SOURCES tests/PlayerTooltipTests.cpp LINK realm_ui)
    realm_add_test(NAME guild_members TARGET realm_guild_members_tests LABELS unit
        SOURCES tests/GuildMembersTests.cpp LINK realm_game realm_ui)
    realm_add_python_test(NAME guild_members_http LABELS unit
        SCRIPT tests/guild_members_http_tests.py ARGS "$<TARGET_FILE:realm_guild_members_tests>")
    realm_add_test(NAME guild_board TARGET realm_guild_board_tests LABELS unit
        SOURCES tests/GuildBoardTests.cpp LINK realm_ui)
    realm_add_python_test(NAME guild_board_http LABELS unit
        SCRIPT tests/guild_board_http_tests.py ARGS "$<TARGET_FILE:realm_guild_board_tests>")
    realm_add_test(NAME preferences TARGET realm_preferences_tests LABELS unit
        SOURCES tests/PreferencesTests.cpp LINK realm_game realm_ui)
    realm_add_test(NAME options TARGET realm_options_tests LABELS unit
        SOURCES tests/OptionsTests.cpp LINK realm_ui
        ARGS "${CMAKE_CURRENT_BINARY_DIR}/options-test")
    realm_add_test(NAME dialogs TARGET realm_dialog_tests LABELS unit
        SOURCES tests/DialogTests.cpp LINK realm_ui)
    realm_add_test(NAME trade TARGET realm_trade_tests LABELS unit
        SOURCES tests/TradeTests.cpp LINK realm_ui)
    realm_add_test(NAME chat TARGET realm_chat_tests LABELS unit
        SOURCES tests/ChatTests.cpp LINK realm_ui)
    realm_add_test(NAME player_menus TARGET realm_player_menu_tests LABELS unit
        SOURCES tests/PlayerMenuTests.cpp LINK realm_ui)
    add_executable(realm_social_session_inputs tests/SocialSessionInputs.cpp)
    target_link_libraries(realm_social_session_inputs PRIVATE realm_game)
    realm_add_python_test(NAME social_loopback LABELS unit
        SCRIPT tests/social_session_tests.py ARGS "$<TARGET_FILE:realm_social_session_inputs>")
    add_executable(realm_inventory_session_inputs tests/InventorySessionInputs.cpp)
    target_link_libraries(realm_inventory_session_inputs PRIVATE realm_game)
    realm_add_python_test(NAME inventory_loopback LABELS unit
        SCRIPT tests/inventory_session_tests.py ARGS "$<TARGET_FILE:realm_inventory_session_inputs>")
    realm_add_test(NAME animation_parity TARGET realm_animation_tests LABELS unit
        SOURCES tests/AnimationTests.cpp LINK realm_assets)
    realm_add_test(NAME terrain_cache TARGET realm_terrain_tests LABELS unit
        SOURCES tests/TerrainTests.cpp LINK realm_assets)
    add_executable(realm_teleport_session_inputs tests/TeleportSessionInputs.cpp)
    target_link_libraries(realm_teleport_session_inputs PRIVATE realm_game realm_ui)
    realm_add_python_test(NAME teleport_loopback LABELS unit
        SCRIPT tests/teleport_session_tests.py ARGS "$<TARGET_FILE:realm_teleport_session_inputs>")
    add_executable(realm_party_arrow_session_inputs tests/PartyArrowSessionInputs.cpp)
    target_link_libraries(realm_party_arrow_session_inputs PRIVATE realm_game realm_ui)
    realm_add_test(NAME party_arrows TARGET realm_party_arrow_tests LABELS unit
        SOURCES tests/PartyArrowTests.cpp LINK realm_ui)
    realm_add_test(NAME player_groups TARGET realm_player_group_tests LABELS unit
        SOURCES tests/PlayerGroupTests.cpp LINK realm_ui)
    realm_add_test(NAME minimap_cache TARGET realm_minimap_cache_tests LABELS unit
        SOURCES tests/MinimapCacheTests.cpp LINK realm_ui)
    realm_add_python_test(NAME session_loopback LABELS unit
        SCRIPT tests/session_tests.py ARGS "$<TARGET_FILE:realm_session>")
    realm_add_python_test(NAME flash_import LABELS unit
        SCRIPT tests/import_tests.py ARGS "${REALM_FLASH_ROOT}")
    # Tests that open a window, GL context or GPU (SDL, realm_client, or a *_gpu script). They share one lock so they
    # never overlap; CPU-only tests keep running in parallel. Names missing from this configuration are skipped.
    # delete_character_stale is not listed: it drives Startup headlessly through a Python HTTP mock.
    set(REALM_GPU_TESTS stress_rendered telemetry_gpu postfx visibility_culling frame_caches model_instances
        dynamic_atlas status_gpu options_gpu sdl_keys legends_gpu death_fame_gpu delete_character_gpu
        ui_rml_host ui_retained ui_bundle_gpu)
    foreach(gpu_test IN LISTS REALM_GPU_TESTS)
        if(TEST ${gpu_test})
            set_tests_properties(${gpu_test} PROPERTIES RESOURCE_LOCK realm_gpu)
        endif()
    endforeach()
endif()

include(cmake/ClientBoundaries.cmake)
if(BUILD_TESTING)
    # client_boundaries is declared in cmake/ClientBoundaries.cmake; it is labeled here so every test has a label.
    set_tests_properties(client_boundaries PROPERTIES LABELS unit)
endif()
