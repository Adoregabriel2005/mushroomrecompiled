cmake_minimum_required(VERSION 3.20)
project(MushroomRecompiled C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(RUNNER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tools/nesrecomp-src/runner")
set(SDL2_DIR "${RUNNER_ROOT}/external/SDL2")
include("${RUNNER_ROOT}/cyc/cyc.cmake")
file(GLOB CYCLE_BANKS CONFIGURE_DEPENDS "generated/smb3_cyc*.c")
add_library(smb3-core STATIC ${NESRECOMP_CYC_SOURCES} ${CYCLE_BANKS} runtime/glue.c src/widescreen.c src/wide_enemies.c)
target_include_directories(smb3-core PUBLIC ${NESRECOMP_CYC_INCLUDE_DIRS} runtime generated src)
target_link_libraries(smb3-core PUBLIC ${NESRECOMP_CYC_LIBRARIES})

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

if(MUSHROOM_SDL2_FOUND)
    add_executable(mushroom-sdl src/app_sdl.c src/video.c src/video_sdl.c src/video_vulkan.c src/audio_sdl.c)
    target_include_directories(mushroom-sdl PRIVATE
        ${MUSHROOM_SDL2_INCLUDE} ${SHADER_OUT})
    if(MUSHROOM_SDL2_LIBRARY_DIR)
        target_link_directories(mushroom-sdl PRIVATE ${MUSHROOM_SDL2_LIBRARY_DIR})
    endif()
    target_link_libraries(mushroom-sdl PRIVATE smb3-core ${MUSHROOM_SDL2_LIBRARY})
    target_compile_definitions(mushroom-sdl PRIVATE MUSHROOM_HAVE_VULKAN=${MUSHROOM_HAVE_VULKAN})
    if(MUSHROOM_HAVE_VULKAN)
        target_link_libraries(mushroom-sdl PRIVATE Vulkan::Vulkan)
        add_dependencies(mushroom-sdl mushroom-shaders)
    endif()
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
    message(STATUS "SDL2 not found: building only the engine and verifiers")
endif()

# The verifiers and the engine are portable; the launcher below is Win32.
add_executable(smb3-verify tests/verify.c)
target_link_libraries(smb3-verify PRIVATE smb3-core)
add_executable(smb3-wide-verify tests/widescreen_verify.c)
target_link_libraries(smb3-wide-verify PRIVATE smb3-core)
add_executable(smb3-scenes-verify tests/scenes_verify.c)
target_link_libraries(smb3-scenes-verify PRIVATE smb3-core)

if(WIN32)
    enable_language(RC)
    add_executable(Smb3Recomp WIN32 src/app.c src/theme.c src/preferences.c src/choice.c src/input.c src/app.rc)
    set_target_properties(Smb3Recomp PROPERTIES OUTPUT_NAME "MushroomRecompiled-v1.5.0")
    target_include_directories(Smb3Recomp PRIVATE "${SDL2_DIR}/include")
    target_compile_definitions(Smb3Recomp PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_directories(Smb3Recomp PRIVATE "${SDL2_DIR}/lib/x64")
    target_link_libraries(Smb3Recomp PRIVATE smb3-core SDL2 user32 gdi32 comdlg32 shell32 winmm ole32 windowscodecs uuid comctl32 uxtheme msimg32 dbghelp ws2_32)
    if(MINGW OR CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_link_options(Smb3Recomp PRIVATE -municode)
    endif()
    add_custom_command(TARGET Smb3Recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Smb3Recomp>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/gamecontrollerdb.txt" "$<TARGET_FILE_DIR:Smb3Recomp>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/png.png" "$<TARGET_FILE_DIR:Smb3Recomp>/png.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DIR}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:Smb3Recomp>")

    add_executable(smb3-ui-verify tests/ui_verify.c src/theme.c src/preferences.c src/choice.c src/input.c)
    target_include_directories(smb3-ui-verify PRIVATE "${SDL2_DIR}/include")
    target_compile_definitions(smb3-ui-verify PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_directories(smb3-ui-verify PRIVATE "${SDL2_DIR}/lib/x64")
    target_link_libraries(smb3-ui-verify PRIVATE smb3-core SDL2 user32 gdi32 comdlg32 shell32 winmm ole32 windowscodecs uuid comctl32 uxtheme msimg32)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(smb3-ui-verify PRIVATE -UNDEBUG)
    endif()
endif()
