cmake_minimum_required(VERSION 3.20)
project(MushroomRecompiled C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(RUNNER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tools/nesrecomp-src/runner")
set(SDL2_DIR "${RUNNER_ROOT}/external/SDL2")
include("${RUNNER_ROOT}/cyc/cyc.cmake")

# ---- game translation (ROM in, C blocks out, at build time) ------
# The user's own ROM. Both dumps share the PRG/CHR payload and differ only in
# the iNES header, so both filenames are accepted out of the box.
set(SMB3_ROM "${CMAKE_SOURCE_DIR}/roms/Super Mario Bros. 3 (U) (PRG1) [!].nes" CACHE FILEPATH
    "Path to the user's legally obtained SMB3 (U) PRG1 ROM")
if(NOT EXISTS "${SMB3_ROM}")
    foreach(_smb3_alt
            "${CMAKE_SOURCE_DIR}/roms/Super Mario Bros. 3 (U) (PRG1) [!].nes"
            "${CMAKE_SOURCE_DIR}/roms/Super Mario Bros. 3 (USA) (Rev 1).nes"
            "${CMAKE_SOURCE_DIR}/Super Mario Bros. 3 (U) (PRG1) [!].nes"
            "${CMAKE_SOURCE_DIR}/Super Mario Bros. 3 (USA) (Rev 1).nes")
        if(EXISTS "${_smb3_alt}")
            message(STATUS "SMB3 ROM not found at ${SMB3_ROM}; using ${_smb3_alt}")
            set(SMB3_ROM "${_smb3_alt}" CACHE FILEPATH
                "Path to the user's legally obtained SMB3 (U) PRG1 ROM" FORCE)
            break()
        endif()
    endforeach()
endif()
add_subdirectory(tools/nesrecomp-src/recompiler recompiler)
set(SMB3_GENERATE_STAMP "${CMAKE_BINARY_DIR}/smb3-generate.stamp")
add_custom_command(
    OUTPUT "${SMB3_GENERATE_STAMP}"
    COMMAND "$<TARGET_FILE:NESRecomp>" "${SMB3_ROM}"
            --game "${CMAKE_SOURCE_DIR}/game.toml"
            --cycle-seed-file "${CMAKE_SOURCE_DIR}/docs/smb3-cycle-seeds.txt"
    COMMAND ${CMAKE_COMMAND} -E touch "${SMB3_GENERATE_STAMP}"
    DEPENDS NESRecomp "${SMB3_ROM}"
            "${CMAKE_SOURCE_DIR}/game.toml"
            "${CMAKE_SOURCE_DIR}/docs/smb3-cycle-seeds.txt"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Translating game code to C (verifying against the ROM)"
    VERBATIM)
add_custom_target(smb3-generate DEPENDS "${SMB3_GENERATE_STAMP}")
file(GLOB CYCLE_BANKS CONFIGURE_DEPENDS "generated/smb3_cyc*.c")
file(GLOB NES_APU_SOURCES CONFIGURE_DEPENDS "src/audio/nes_apu/*.cpp")
add_library(smb3-core STATIC ${NESRECOMP_CYC_SOURCES} ${CYCLE_BANKS}
    runtime/glue.c src/widescreen.c src/wide_enemies.c src/hud.c src/snd_owner.c
    src/audio_engine.cpp src/audio/apu.cpp ${NES_APU_SOURCES})
target_include_directories(smb3-core PUBLIC ${NESRECOMP_CYC_INCLUDE_DIRS} runtime generated src)
target_link_libraries(smb3-core PUBLIC ${NESRECOMP_CYC_LIBRARIES})
add_dependencies(smb3-core smb3-generate)

# ---- optional Vulkan presenter ------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
find_package(Vulkan QUIET)
find_program(GLSL_COMPILER glslc)
set(MUSHROOM_HAVE_VULKAN 0)
set(SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
if(Vulkan_FOUND AND GLSL_COMPILER AND Python3_Interpreter_FOUND)
    set(MUSHROOM_HAVE_VULKAN 1)
    add_custom_command(
        OUTPUT "${SHADER_OUT}/present_vert.h" "${SHADER_OUT}/present_frag.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT}"
        COMMAND ${GLSL_COMPILER} -fshader-stage=vertex
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present.vert"
                -o "${SHADER_OUT}/present.vert.spv"
        COMMAND ${GLSL_COMPILER} -fshader-stage=fragment
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present.frag"
                -o "${SHADER_OUT}/present.frag.spv"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
                "${SHADER_OUT}/present.vert.spv" "${SHADER_OUT}/present_vert.h" present_vert_spv
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
                "${SHADER_OUT}/present.frag.spv" "${SHADER_OUT}/present_frag.h" present_frag_spv
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present.frag"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
        VERBATIM)
    add_custom_target(mushroom-shaders DEPENDS "${SHADER_OUT}/present_vert.h" "${SHADER_OUT}/present_frag.h")
endif()

# ---- portable SDL2 frontend ---------------------------------------------------
if(WIN32)
    set(MUSHROOM_SDL2_FOUND TRUE)
    set(MUSHROOM_SDL2_INCLUDE "${SDL2_DIR}/include")
    set(MUSHROOM_SDL2_LIBRARY SDL2)
    set(MUSHROOM_SDL2_LIBRARY_DIR "${SDL2_DIR}/lib/x64")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MUSHROOM_SDL2 QUIET sdl2)
    endif()
    if(MUSHROOM_SDL2_FOUND)
        set(MUSHROOM_SDL2_INCLUDE ${MUSHROOM_SDL2_INCLUDE_DIRS})
        set(MUSHROOM_SDL2_LIBRARY ${MUSHROOM_SDL2_LIBRARIES})
        set(MUSHROOM_SDL2_LIBRARY_DIR ${MUSHROOM_SDL2_LIBRARY_DIRS})
    endif()
endif()

if(MUSHROOM_SDL2_FOUND AND MUSHROOM_HAVE_VULKAN)
    add_executable(mushroom-sdl src/app_sdl.c src/overlay.c src/video.c src/video_vulkan.cpp src/audio_sdl.c)
    if(WIN32)
        set_target_properties(mushroom-sdl PROPERTIES OUTPUT_NAME MushroomRecompiled)
    endif()
    target_include_directories(mushroom-sdl PRIVATE
        ${MUSHROOM_SDL2_INCLUDE} ${SHADER_OUT})
    if(MUSHROOM_SDL2_LIBRARY_DIR)
        target_link_directories(mushroom-sdl PRIVATE ${MUSHROOM_SDL2_LIBRARY_DIR})
    endif()
    target_link_libraries(mushroom-sdl PRIVATE smb3-core ${MUSHROOM_SDL2_LIBRARY})
    set_target_properties(mushroom-sdl PROPERTIES LINKER_LANGUAGE CXX)
    target_compile_definitions(mushroom-sdl PRIVATE MUSHROOM_HAVE_VULKAN=1)
    target_link_libraries(mushroom-sdl PRIVATE Vulkan::Vulkan)
    add_dependencies(mushroom-sdl mushroom-shaders)
    if(UNIX)
        target_link_libraries(mushroom-sdl PRIVATE m ${CMAKE_DL_LIBS})
    endif()
    add_custom_command(TARGET mushroom-sdl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mushroom-sdl>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/gamecontrollerdb.txt"
                "$<TARGET_FILE_DIR:mushroom-sdl>/assets")
    if(WIN32)
        add_custom_command(TARGET mushroom-sdl POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DIR}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:mushroom-sdl>")
    endif()
else()
    message(STATUS "SDL2 or the Vulkan SDK not found: building only the engine and verifiers")
endif()

# The verifiers and the engine are portable; the launcher below is Win32.
add_executable(smb3-verify tests/verify.c)
target_link_libraries(smb3-verify PRIVATE smb3-core)
set_target_properties(smb3-verify PROPERTIES LINKER_LANGUAGE CXX)
add_executable(smb3-wide-verify tests/widescreen_verify.c)
target_link_libraries(smb3-wide-verify PRIVATE smb3-core)
set_target_properties(smb3-wide-verify PROPERTIES LINKER_LANGUAGE CXX)
add_executable(smb3-scenes-verify tests/scenes_verify.c)
target_link_libraries(smb3-scenes-verify PRIVATE smb3-core)
set_target_properties(smb3-scenes-verify PROPERTIES LINKER_LANGUAGE CXX)
add_executable(smb3-wide-collision-verify tests/wide_collision_verify.c)
target_link_libraries(smb3-wide-collision-verify PRIVATE smb3-core)
set_target_properties(smb3-wide-collision-verify PROPERTIES LINKER_LANGUAGE CXX)

# The portable SDL2/Vulkan frontend (mushroom-sdl) is the single launcher on
# every platform. With no remembered ROM it shows an in-app picker, so a
# plain double-click (or one command with no flags) starts the game.
