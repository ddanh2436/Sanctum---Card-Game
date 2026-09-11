cmake_minimum_required(VERSION 3.20)
project(SanctumOathOfLight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Tìm SFML
# Thứ tự ưu tiên: -DSFML_DIR=... > biến môi trường SFML_DIR > các đường dẫn quen thuộc.
# (Không hard-code đường dẫn cá nhân để máy khác vẫn configure được.)
# ---------------------------------------------------------------------------
if(NOT DEFINED SFML_DIR AND DEFINED ENV{SFML_DIR})
    set(SFML_DIR "$ENV{SFML_DIR}" CACHE PATH "Path to SFML CMake config")
endif()

if(NOT DEFINED SFML_DIR)
    set(SANCTUM_SFML_HINTS
        "${CMAKE_CURRENT_SOURCE_DIR}/external/SFML/lib/cmake/SFML"
        "${CMAKE_CURRENT_SOURCE_DIR}/../SFML/lib/cmake/SFML"
        "$ENV{USERPROFILE}/Documents/GitHub/Tower_Defense_HCMUS_GROUP7_24C11/lib/cmake/SFML"
        "C:/SFML/lib/cmake/SFML"
        "C:/Libraries/SFML/lib/cmake/SFML"
    )
    foreach(hint IN LISTS SANCTUM_SFML_HINTS)
        if(EXISTS "${hint}/SFMLConfig.cmake")
            set(SFML_DIR "${hint}" CACHE PATH "Path to SFML CMake config" FORCE)
            message(STATUS "Auto-detected SFML_DIR: ${SFML_DIR}")
            break()
        endif()
    endforeach()
endif()

if(NOT DEFINED SFML_DIR)
    message(FATAL_ERROR
        "Khong tim thay SFML. Hay chay lai voi: cmake -B build -DSFML_DIR=<duong/dan>/lib/cmake/SFML")
endif()

# Dùng static library của SFML
set(SFML_STATIC_LIBRARIES TRUE)

# Khắc phục lỗi tương thích SFML 2.6.x: SFMLConfigDependencies tham chiếu target OpenAL
if(NOT TARGET OpenAL)
    add_library(OpenAL INTERFACE IMPORTED)
endif()
if(NOT TARGET OpenAL::OpenAL)
    add_library(OpenAL::OpenAL INTERFACE IMPORTED)
    set_target_properties(OpenAL::OpenAL PROPERTIES INTERFACE_LINK_LIBRARIES OpenAL)
endif()

find_package(SFML 2.6 COMPONENTS graphics window audio system REQUIRED)

# ---------------------------------------------------------------------------
# Core library: rules engine, data, campaign. No rendering lives in here, so
# the whole game can be simulated headlessly by the test binaries.
# ---------------------------------------------------------------------------
set(SANCTUM_CORE_SOURCES
    src/battle/CardData.cpp
    src/battle/Board.cpp
    src/battle/Commander.cpp
    src/battle/DuelEngine.cpp
    src/battle/AICommander.cpp
    src/run/DeckBuilder.cpp
    src/run/DeckStore.cpp
    src/run/RunState.cpp
    src/utils/Avatars.cpp
    src/utils/DataLoader.cpp
    src/utils/Fonts.cpp
    src/utils/Settings.cpp
    src/utils/AudioManager.cpp
    src/utils/AudioCatalogue.cpp
    src/rendering/HolyVFX.cpp
    src/rendering/CardArt.cpp
    src/rendering/CardSpotlight.cpp
    src/rendering/CombatVFX.cpp
    src/rendering/MenuBackdrop.cpp
    src/rendering/EndScreen.cpp
    src/rendering/DeploySignature.cpp
    src/rendering/DrawFlight.cpp
    src/rendering/EndGameVFX.cpp
    src/rendering/PortraitRig.cpp
    src/rendering/FloatingText.cpp
    src/core/Engine.cpp
    src/core/StateManager.cpp
)

set(SANCTUM_CORE_HEADERS
    src/battle/CardData.hpp
    src/battle/Board.hpp
    src/battle/Commander.hpp
    src/battle/DuelEngine.hpp
    src/battle/AICommander.hpp
    src/run/DeckBuilder.hpp
    src/run/DeckStore.hpp
    src/run/RunState.hpp
    src/rendering/HolyVFX.hpp
    src/rendering/CardArt.hpp
    src/rendering/CardSpotlight.hpp
    src/rendering/CombatVFX.hpp
    src/rendering/MenuBackdrop.hpp
    src/rendering/EndScreen.hpp
    src/rendering/DeploySignature.hpp
    src/rendering/DrawFlight.hpp
    src/rendering/EndGameVFX.hpp
    src/rendering/PortraitRig.hpp
    src/rendering/FloatingText.hpp
    src/utils/ResourceManager.hpp
    src/utils/Avatars.hpp
    src/utils/DataLoader.hpp
    src/utils/Fonts.hpp
    src/utils/Settings.hpp
    src/utils/AudioManager.hpp
    src/utils/AudioCatalogue.hpp
    src/utils/Rng.hpp
    src/utils/TextUtils.hpp
    src/core/Engine.hpp
    src/core/StateManager.hpp
)

add_library(SanctumLib STATIC
    ${SANCTUM_CORE_SOURCES}
    ${SANCTUM_CORE_HEADERS}
)

target_include_directories(SanctumLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(SanctumLib PUBLIC
    sfml-graphics
    sfml-window
    sfml-audio
    sfml-system
)
target_compile_definitions(SanctumLib PUBLIC SFML_STATIC)

if(MSVC)
    # /utf-8: the sources are UTF-8 and carry Vietnamese subtitle text. Without
    # it MSVC reads narrow literals in the system codepage and the diacritics
    # come out as mojibake with no warning.
    target_compile_options(SanctumLib PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(SanctumLib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(SanctumGame src/main.cpp)
target_link_libraries(SanctumGame PRIVATE SanctumLib)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
enable_testing()

set(SANCTUM_TEST_TARGETS "")
foreach(test_spec
        "SanctumRulesTests;tests/test_duel_rules.cpp"
        "SanctumDuelSim;tests/test_duel_sim.cpp"
        "SanctumAudioTests;tests/test_audio_catalogue.cpp"
        "SanctumVfxTests;tests/test_vfx.cpp")
    list(GET test_spec 0 test_name)
    list(GET test_spec 1 test_src)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE SanctumLib)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME ${test_name} COMMAND ${test_name}
             WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND SANCTUM_TEST_TARGETS ${test_name})
endforeach()

# ---------------------------------------------------------------------------
# Output & assets
# ---------------------------------------------------------------------------
set_target_properties(SanctumGame ${SANCTUM_TEST_TARGETS} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

# Mirror assets next to the executable after a build (POST_BUILD: only when the
# target actually relinks, rather than an ALL target that runs on every call).
#
# The destination is removed first. copy_directory only ever adds and
# overwrites, so renamed or deleted art piled up: after the card set moved from
# PNG to JPEG the build directory held 96 orphaned .png files - 29 MB of assets
# the game no longer loads.
add_custom_command(TARGET SanctumGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:SanctumGame>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:SanctumGame>/assets"
    COMMENT "Mirroring assets to executable directory"
)

# ---------------------------------------------------------------------------
# openal32.dll
# sfml-audio LUÔN cần openal32.dll lúc chạy, kể cả khi link static.
# Thiếu file này game sẽ thoát ngay với mã lỗi 0xC0000135 (DLL not found).
# ---------------------------------------------------------------------------
get_filename_component(SANCTUM_SFML_ROOT "${SFML_DIR}/../../.." ABSOLUTE)
find_file(SANCTUM_OPENAL_DLL
    NAMES openal32.dll OpenAL32.dll
    HINTS "${SANCTUM_SFML_ROOT}/bin" "${SANCTUM_SFML_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}/external"
    DOC "Duong dan toi openal32.dll (bat buoc cho sfml-audio)"
)

if(SANCTUM_OPENAL_DLL)
    message(STATUS "openal32.dll: ${SANCTUM_OPENAL_DLL}")
    add_custom_command(TARGET SanctumGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SANCTUM_OPENAL_DLL}" "$<TARGET_FILE_DIR:SanctumGame>"
        COMMENT "Copying openal32.dll next to SanctumGame.exe"
    )
else()
    message(WARNING
        "Khong tim thay openal32.dll. Game se khong khoi dong duoc (loi 0xC0000135).\n"
        "Copy file nay tu ban phat hanh SFML vao canh SanctumGame.exe, "
        "hoac chay lai cmake voi -DSANCTUM_OPENAL_DLL=<duong/dan>/openal32.dll")
endif()
