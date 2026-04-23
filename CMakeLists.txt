cmake_minimum_required(VERSION 3.21)

# Per-developer overrides (paths to SDKs, custom cache vars, etc.).
# Copy local.cmake.example to local.cmake and edit for your machine.
# local.cmake is gitignored.
include("${CMAKE_SOURCE_DIR}/local.cmake" OPTIONAL)

set(GAME_NAME "Super Brette Bros" CACHE STRING "Project name passed as PROJECT_NAME define")
add_compile_definitions(LANDSCAPE)
add_compile_definitions(PROJECT_NAME="${GAME_NAME}")
# ----------------------------------------------------------------------
# NES target
# ----------------------------------------------------------------------
if(TARGET_PLATFORM STREQUAL "nes")
    set(LLVM_MOS_PLATFORM nes-nrom)
    find_package(llvm-mos-sdk REQUIRED)
    project(platform-nes C)
    set(PLATFORM_DEFINE TARGET_NES)

    # FamiStudio sound engine sources are fetched directly from upstream
    # to avoid vendoring a copy in the repository.
    set(FAMISTUDIO_DIR ${CMAKE_BINARY_DIR}/famistudio)
    set(FAMISTUDIO_BASE_URL "https://raw.githubusercontent.com/BleuBleu/FamiStudio/master/SoundEngine")

    if(NOT EXISTS ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(MAKE_DIRECTORY ${FAMISTUDIO_DIR}/NoteTables)

        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.s ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.h ${FAMISTUDIO_DIR}/famistudio_ca65.h)

        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_lsb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_lsb.bin)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_msb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_msb.bin)
    endif()

    # Locate the ca65 assembler and llvm-readelf binary used during the
    # two-pass build below.
    find_program(CA65 ca65 HINTS "C:/cc65/bin" REQUIRED)
    get_filename_component(LLVM_MOS_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(LLVM_READELF llvm-readelf HINTS "${LLVM_MOS_BIN}" REQUIRED)

    # Common FamiStudio configuration block. The pass 1 build uses this
    # as-is; the pass 2 build prepends a generated padding include.
    set(FAMISTUDIO_CFG
            ".define FAMISTUDIO_CA65_ZP_SEGMENT   ZEROPAGE
.define FAMISTUDIO_CA65_RAM_SEGMENT  BSS
.define FAMISTUDIO_CA65_CODE_SEGMENT CODE

FAMISTUDIO_CFG_EXTERNAL     = 1
FAMISTUDIO_CFG_NTSC_SUPPORT = 1
FAMISTUDIO_CFG_PAL_SUPPORT  = 0
FAMISTUDIO_CFG_DPCM_SUPPORT = 1
FAMISTUDIO_CFG_SFX_SUPPORT  = 1
FAMISTUDIO_CFG_SFX_STREAMS  = 2
FAMISTUDIO_USE_VOLUME_TRACK = 1

.include \"famistudio_ca65.s\"
")

    file(COPY ${FAMISTUDIO_DIR}/famistudio_ca65.h
            DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)

    if(EXISTS ${FAMISTUDIO_DIR}/famistudio_llvmmos.h)
        file(COPY ${FAMISTUDIO_DIR}/famistudio_llvmmos.h
                DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    endif()

    # ------------------------------------------------------------------
    # Pass 1: build FamiStudio and the demo with no zero-page or BSS
    # padding so that the resulting ELF can be inspected to determine
    # how much space the C code actually consumes in those segments.
    # ------------------------------------------------------------------

    file(WRITE ${CMAKE_BINARY_DIR}/famistudio_config_pass1.s "${FAMISTUDIO_CFG}")

    set(FAMISTUDIO_PASS1_OBJ ${CMAKE_BINARY_DIR}/famistudio_pass1.o)
    add_custom_command(
            OUTPUT ${FAMISTUDIO_PASS1_OBJ}
            COMMAND ${CA65}
            -I ${FAMISTUDIO_DIR}
            -I ${FAMISTUDIO_DIR}/NoteTables
            -o ${FAMISTUDIO_PASS1_OBJ}
            ${CMAKE_BINARY_DIR}/famistudio_config_pass1.s
            DEPENDS ${CMAKE_BINARY_DIR}/famistudio_config_pass1.s
            COMMENT "Assembling FamiStudio (pass 1)"
    )

    file(GLOB_RECURSE DEMO_ASM_SOURCES ${CMAKE_SOURCE_DIR}/demo/*.s)
    set(DEMO_ASM_OBJECTS "")
    foreach(ASM_SRC ${DEMO_ASM_SOURCES})
        get_filename_component(ASM_NAME ${ASM_SRC} NAME_WE)
        set(ASM_OBJ ${CMAKE_BINARY_DIR}/${ASM_NAME}.o)
        add_custom_command(
                OUTPUT ${ASM_OBJ}
                COMMAND ${CA65}
                -I ${FAMISTUDIO_DIR}
                -I ${FAMISTUDIO_DIR}/NoteTables
                -o ${ASM_OBJ}
                ${ASM_SRC}
                DEPENDS ${ASM_SRC}
                COMMENT "Assembling ${ASM_NAME}"
        )
        list(APPEND DEMO_ASM_OBJECTS ${ASM_OBJ})
    endforeach()

    file(GLOB NES_SOURCES src/nes/*.c)
    add_library(platform-nes STATIC ${NES_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/famistudio_include
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})

    file(GLOB DEMO_C_SOURCES ${CMAKE_SOURCE_DIR}/demo/*.c)

    set(PASS1_ELF ${CMAKE_BINARY_DIR}/demo_pass1.elf)
    add_custom_command(
            OUTPUT ${PASS1_ELF}
            COMMAND ${CMAKE_C_COMPILER}
            -Os -DTARGET_NES -DLANDSCAPE "-DPROJECT_NAME=${GAME_NAME}"
            -I${CMAKE_SOURCE_DIR}/include
            -I${CMAKE_BINARY_DIR}/famistudio_include
            ${DEMO_C_SOURCES}
            $<TARGET_FILE:platform-nes>
            ${FAMISTUDIO_PASS1_OBJ}
            ${DEMO_ASM_OBJECTS}
            -o ${CMAKE_BINARY_DIR}/demo_pass1
            DEPENDS platform-nes
            ${FAMISTUDIO_PASS1_OBJ}
            ${DEMO_ASM_OBJECTS}
            ${DEMO_C_SOURCES}
            COMMENT "Linking pass 1 ELF for segment measurement"
    )

    # ------------------------------------------------------------------
    # Measurement step: read ZP/BSS sizes from the pass 1 ELF and emit
    # a ca65 include defining the padding required for pass 2.
    # ------------------------------------------------------------------

    set(CA65_PADDING ${CMAKE_BINARY_DIR}/ca65_padding.s)
    add_custom_command(
            OUTPUT ${CA65_PADDING}
            COMMAND ${CMAKE_COMMAND}
            -DREADELF=${LLVM_READELF}
            -DELF_FILE=${PASS1_ELF}
            -DOUTPUT_FILE=${CA65_PADDING}
            -P ${CMAKE_SOURCE_DIR}/tools/measure_sections.cmake
            DEPENDS ${PASS1_ELF}
            ${CMAKE_SOURCE_DIR}/tools/measure_sections.cmake
            COMMENT "Extracting ZP/BSS sizes from pass 1 ELF"
    )

    # ------------------------------------------------------------------
    # Pass 2: reassemble FamiStudio with the measured padding so its
    # zero-page and BSS allocations sit above the C runtime's usage.
    # ------------------------------------------------------------------

    file(WRITE ${CMAKE_BINARY_DIR}/famistudio_config.s
            ".include \"ca65_padding.s\"
${FAMISTUDIO_CFG}")

    set(FAMISTUDIO_OBJ ${CMAKE_BINARY_DIR}/famistudio.o)
    add_custom_command(
            OUTPUT ${FAMISTUDIO_OBJ}
            COMMAND ${CA65}
            -I ${CMAKE_BINARY_DIR}
            -I ${FAMISTUDIO_DIR}
            -I ${FAMISTUDIO_DIR}/NoteTables
            -o ${FAMISTUDIO_OBJ}
            ${CMAKE_BINARY_DIR}/famistudio_config.s
            DEPENDS ${CA65_PADDING}
            ${CMAKE_BINARY_DIR}/famistudio_config.s
            COMMENT "Assembling FamiStudio (pass 2, with padding)"
    )

    # ------------------------------------------------------------------
    # Final link
    # ------------------------------------------------------------------

    add_executable(demo ${DEMO_C_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".nes")

    target_link_libraries(demo PRIVATE platform-nes)

    add_dependencies(demo demo_pass2_asm)

    add_custom_target(demo_pass2_asm
            DEPENDS ${FAMISTUDIO_OBJ} ${DEMO_ASM_OBJECTS}
    )

    # FamiStudio and the hand-written assembly objects are required by
    # every build configuration.
    target_link_options(demo PRIVATE
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
    )

    # ----------------------------------------------------------------------
    # Desktop targets (macOS, Windows, Linux)
    # ----------------------------------------------------------------------
else()
    project(platform-nes C)

    if(TARGET_PLATFORM STREQUAL "mac")
        set(PLATFORM_DEFINE TARGET_MACOS)
    elseif(TARGET_PLATFORM STREQUAL "win")
        set(PLATFORM_DEFINE TARGET_WINDOWS)
    elseif(TARGET_PLATFORM STREQUAL "linux")
        set(PLATFORM_DEFINE TARGET_LINUX)
    else()
        message(FATAL_ERROR "Set -DTARGET_PLATFORM to one of: nes, win, mac, linux")
    endif()

    # ------------------------------------------------------------------
    # Fetch SDL3 source and build it statically alongside the project.
    # ------------------------------------------------------------------
    set(SDL3_GIT_TAG "release-3.2.16" CACHE STRING "SDL3 git tag to fetch")

    include(FetchContent)

    # ------------------------------------------------------------------
    # On Linux, also build libusb from source from the official
    # libusb-cmake wrapper repository. libusb-cmake embeds the real
    # libusb source as a git subtree; its 'main' branch tracks the
    # latest stable libusb release (currently 1.0.29). Override
    # LIBUSB_CMAKE_GIT_TAG to pin to a specific tag or SHA for
    # reproducible builds.
    #
    # NOTE: libusb's Linux backend requires libudev headers to be
    # installed on the host system (libudev-dev on Debian/Ubuntu,
    # systemd-devel on Fedora/RHEL).
    # ------------------------------------------------------------------
    if(TARGET_PLATFORM STREQUAL "linux")
        set(LIBUSB_CMAKE_GIT_TAG "main" CACHE STRING
                "libusb-cmake git ref; 'main' tracks latest stable libusb")

        set(LIBUSB_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(LIBUSB_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
        set(LIBUSB_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
        set(LIBUSB_INSTALL_TARGETS   OFF CACHE BOOL "" FORCE)
        set(LIBUSB_ENABLE_LOGGING    ON  CACHE BOOL "" FORCE)

        FetchContent_Declare(
                libusb_cmake
                GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
                GIT_TAG        ${LIBUSB_CMAKE_GIT_TAG}
                GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(libusb_cmake)

        # --------------------------------------------------------------
        # Redirect SDL3's libusb detection at our in-tree build.
        #
        # SDL3 calls find_package(LibUSB), which invokes its bundled
        # cmake/FindLibUSB.cmake and scans the filesystem for libusb.
        # To make it adopt the libusb we just built, we:
        #
        #   1. Pre-create the LibUSB::LibUSB imported target the find
        #      module would otherwise define, as an alias of usb-1.0.
        #      The module's `if(LibUSB_FOUND AND NOT TARGET
        #      LibUSB::LibUSB)` guard then skips its own add_library.
        #
        #   2. Seed LibUSB_INCLUDE_PATH and LibUSB_LIBRARY in the
        #      cache, so find_path / find_library are no-ops and
        #      find_package_handle_standard_args sets LibUSB_FOUND.
        #
        #   3. Point LibUSB_INCLUDE_PATH at libusb-cmake's vendored
        #      libusb sources so the find module's file(READ) of
        #      libusb.h returns a real LIBUSB_API_VERSION that passes
        #      its >= 0x01000102 check.
        #
        #   4. Force SDL_HIDAPI_LIBUSB_SHARED=OFF. Our libusb is a
        #      static archive with no SONAME; SDL3's dlopen path can't
        #      extract USB_1.0_LIB_SONAME from it, and we want the
        #      direct-link branch in sdlchecks.cmake instead.
        # --------------------------------------------------------------
        if(NOT TARGET LibUSB::LibUSB)
            add_library(LibUSB::LibUSB ALIAS usb-1.0)
        endif()

        set(LibUSB_INCLUDE_PATH "${libusb_cmake_SOURCE_DIR}/libusb/libusb"
                CACHE PATH "Path to libusb headers (points at libusb-cmake's vendored sources)" FORCE)

        # LibUSB_LIBRARY only needs a non-empty, non-NOTFOUND value to
        # satisfy find_package_handle_standard_args; the actual link
        # edge goes through the LibUSB::LibUSB alias above, not this
        # path. Pointing it at the header file is a harmless placeholder.
        set(LibUSB_LIBRARY "${LibUSB_INCLUDE_PATH}/libusb.h"
                CACHE FILEPATH "Placeholder; real linkage flows through the LibUSB::LibUSB alias target" FORCE)

        set(SDL_HIDAPI_LIBUSB_SHARED OFF CACHE BOOL "" FORCE)

        # Disable SDL3's libusb-based HIDAPI backend. Its configure-time
        # check_c_source_compiles test requires LibUSB::LibUSB to resolve
        # inside an isolated try_compile scratch project, which ALIAS
        # targets (our hook above) don't survive. The project links
        # usb-1.0 directly for its own NES-over-USB needs, and SDL3
        # still supports gamepads via the native hidraw backend.
        set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)

        # Make SDL link the system audio/video libraries via DT_NEEDED
        # instead of dlopen'ing them at runtime. Required so linuxdeploy's
        # ldd walk can discover and bundle them into the AppImage.
        set(SDL_ALSA_SHARED       OFF CACHE BOOL "" FORCE)
        set(SDL_PULSEAUDIO_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_X11_SHARED        OFF CACHE BOOL "" FORCE)
        set(SDL_WAYLAND_SHARED    OFF CACHE BOOL "" FORCE)
    endif()

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS  OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        ${SDL3_GIT_TAG}
            GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    file(GLOB SDL3_SOURCES src/SDL3/*.c)

    add_library(platform-nes STATIC ${SDL3_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})
    target_link_libraries(platform-nes PUBLIC SDL3::SDL3-static)

    if(TARGET_PLATFORM STREQUAL "linux")
        target_link_libraries(platform-nes PUBLIC usb-1.0)
    endif()

    file(GLOB DEMO_SOURCES demo/*.c)
    add_executable(demo ${DEMO_SOURCES})
    target_link_libraries(demo PRIVATE platform-nes)

    # Enable LTO for release-type configs on desktop targets. CMake routes
    # archive/link through gcc-ar/gcc-ranlib (GCC) or lld (Clang) automatically
    # once INTERPROCEDURAL_OPTIMIZATION is set on the target.
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        foreach(_cfg Release MinSizeRel RelWithDebInfo)
            string(TOUPPER ${_cfg} _CFG)
            set_property(TARGET demo platform-nes
                    PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
        endforeach()
    else()
        message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
    endif()

    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/demo/tracks
            $<TARGET_FILE_DIR:demo>/tracks
    )

    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/demo/sfx
            $<TARGET_FILE_DIR:demo>/sfx
    )

    # ------------------------------------------------------------------
    # Strip the final executable in non-Debug builds. MSVC has no strip
    # (symbols live in a separate .pdb), so only run when CMAKE_STRIP is
    # available (GCC/Clang/MinGW toolchains).
    # ------------------------------------------------------------------
    if(CMAKE_STRIP AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${CMAKE_STRIP} $<TARGET_FILE:demo>
                COMMENT "Stripping demo executable"
        )
    endif()

    # ------------------------------------------------------------------
    # AppImage packaging (Linux only). Produces a single-file executable
    # bundling the demo, SDL3's dynamic deps, and the tracks/sfx assets.
    # Runs on any glibc >= 2.31 distro with no install step.
    # Build with: cmake --build <dir> --target demo-appimage
    # ------------------------------------------------------------------
    if(TARGET_PLATFORM STREQUAL "linux")
        find_program(LINUXDEPLOY linuxdeploy)
        if(LINUXDEPLOY)
            set(APPDIR "${CMAKE_BINARY_DIR}/AppDir")

            configure_file(
                    "${CMAKE_SOURCE_DIR}/docker/demo.desktop.in"
                    "${CMAKE_BINARY_DIR}/demo.desktop"
                    @ONLY
            )

            # Custom AppRun — switches cwd to usr/bin so the demo finds
            # its bundled tracks/ and sfx/ via the same relative paths
            # used for local dev runs.
            file(WRITE "${CMAKE_BINARY_DIR}/AppRun"
"#!/bin/bash
HERE=\"$(dirname \"$(readlink -f \"\${0}\")\")\"
export LD_LIBRARY_PATH=\"\${HERE}/usr/lib:\${LD_LIBRARY_PATH}\"
cd \"\${HERE}/usr/bin\"
exec ./demo \"\$@\"
")

            add_custom_target(demo-appimage
                    COMMAND ${CMAKE_COMMAND} -E rm -rf "${APPDIR}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${APPDIR}/usr/bin"
                    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:demo> "${APPDIR}/usr/bin/demo"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/demo/tracks" "${APPDIR}/usr/bin/tracks"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/demo/sfx"    "${APPDIR}/usr/bin/sfx"
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${CMAKE_BINARY_DIR}/AppRun"      "${APPDIR}/AppRun"
                    COMMAND chmod +x "${APPDIR}/AppRun"
                    COMMAND ${LINUXDEPLOY}
                            --appdir "${APPDIR}"
                            --executable "${APPDIR}/usr/bin/demo"
                            --desktop-file "${CMAKE_BINARY_DIR}/demo.desktop"
                            --icon-file "${CMAKE_SOURCE_DIR}/demo/demo.png"
                            # Force-bundle libasound: AppImage's default excludelist skips it
                            # on the assumption every Linux desktop has ALSA, but minimal
                            # images (e.g., WSL's default Ubuntu) don't.
                            --library /usr/lib/x86_64-linux-gnu/libasound.so.2
                            --output appimage
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                    DEPENDS demo
                    COMMENT "Packaging AppImage"
                    VERBATIM
            )
        else()
            message(STATUS
                    "linuxdeploy not found on PATH; 'demo-appimage' target disabled. "
                    "Build inside the provided Docker image, or install linuxdeploy locally.")
        endif()
    endif()
endif()