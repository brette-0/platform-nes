cmake_minimum_required(VERSION 3.21)

# Per-developer overrides (paths to SDKs, custom cache vars, etc.).
# Copy local.cmake.example to local.cmake and edit for your machine.
# local.cmake is gitignored.
include("${CMAKE_SOURCE_DIR}/local.cmake" OPTIONAL)

# The NES target cross-compiles with llvm-mos. Everything it needs -- clang,
# ld.lld, llvm-readelf -- lives inside the SDK, so the build resolves those from
# the SDK root, never the shell PATH. Point LLVM_MOS_SDK at your SDK install in
# local.cmake (the single machine-specific path); from it we derive the
# toolchain file and seed CMAKE_PREFIX_PATH for find_package(llvm-mos-sdk).
# Runs before project(). An explicitly supplied toolchain
# (-DCMAKE_TOOLCHAIN_FILE=... or an IDE preset) takes precedence and skips this.
# Keeping the SDK off PATH lets a bare clang/clang++ resolve to the host system
# compiler for the desktop builds instead of the 6502 cross-compiler.
if(TARGET_PLATFORM STREQUAL "nes" AND NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT LLVM_MOS_SDK)
        message(FATAL_ERROR
            "NES build requires the llvm-mos-sdk location, but LLVM_MOS_SDK is "
            "unset. Copy local.cmake.example to local.cmake and set it to your "
            "SDK install root, e.g.\n"
            "    set(LLVM_MOS_SDK \"/path/to/llvm-mos\" CACHE PATH \"\" FORCE)\n"
            "The SDK does NOT need to be on your PATH.")
    endif()
    set(CMAKE_TOOLCHAIN_FILE
        "${LLVM_MOS_SDK}/lib/cmake/llvm-mos-sdk/llvm-mos-toolchain.cmake")
    # find_package(llvm-mos-sdk) below resolves through CMAKE_PREFIX_PATH; keep
    # it derived from the one source of truth rather than asking for it twice.
    set(CMAKE_PREFIX_PATH "${LLVM_MOS_SDK}"
        CACHE PATH "llvm-mos-sdk root (derived from LLVM_MOS_SDK)" FORCE)
endif()

# Application / SDL3 window title, compiled in as the PROJECT_NAME define.
# Set PROJECT_NAME in local.cmake to override per-developer; a default keeps
# the window titled when local.cmake is absent or leaves it unset.
#
# NOTE: this must stay ABOVE the project() calls below -- project() sets its
# own PROJECT_NAME variable, which would otherwise clobber this value before
# the define is created.
if(NOT PROJECT_NAME)
    set(PROJECT_NAME "My Platform-NES Game")
endif()

# Capture the title for the AppImage .desktop file's Name= (configure_file
# substitutes @GAME_NAME@ in docker/demo.desktop.in). This must happen here,
# before project() resets the PROJECT_NAME *variable* to "platform-nes":
# appimagetool derives the output filename's prefix from Name=, and an empty
# Name produces a "-x86_64.AppImage" argument that mksquashfs rejects as a flag.
# project() does not touch GAME_NAME, so it survives to the linux block below.
set(GAME_NAME "${PROJECT_NAME}")
add_compile_definitions(LANDSCAPE)
add_compile_definitions(PROJECT_NAME="${PROJECT_NAME}")

# The library and demo are compiled as C++23. C++20 would cover the
# designated initialisers used throughout, but the SDL3 backend relies
# on <stdatomic.h>'s `atomic_int` (_vblank_flag), which only became
# usable from C++ in C++23. The gnu++ dialect (CMAKE_CXX_EXTENSIONS ON)
# keeps the GNU compound-literal extension the video/interrupt code uses.
# Both toolchains accept it: llvm-mos's clang++ and host GCC.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# ----------------------------------------------------------------------
# br0::intsh — header-only integer type aliases, used via #include <intsh>.
# An INTERFACE target, so the same declaration serves both the NES and
# desktop builds; FetchContent_MakeAvailable is called inside each branch
# below (after project()), and the target is linked PUBLIC onto the
# platform-nes library so the include dir propagates to the demo.
# ----------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
        intsh
        GIT_REPOSITORY https://github.com/brette-0/intsh.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
)

# ----------------------------------------------------------------------
# br0::tuple — header-only freestanding tuple, used via #include <br0/tuple>.
# SOURCE_SUBDIR points add_subdirectory at the repo's br0/ folder, which
# defines just the br0::tuple INTERFACE target. This deliberately skips the
# repo's top-level CMakeLists.txt, which hard-requires a machine-specific
# cmake/local.cmake and builds the upstream test suite — neither of which we
# want pulled into this build.
# ----------------------------------------------------------------------
FetchContent_Declare(
        tuple
        GIT_REPOSITORY https://github.com/brette-0/tuple.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  br0
)

# ----------------------------------------------------------------------
# NES target
# ----------------------------------------------------------------------
if(TARGET_PLATFORM STREQUAL "nes")
    set(LLVM_MOS_PLATFORM nes-nrom)
    find_package(llvm-mos-sdk REQUIRED)
    project(platform-nes CXX)
    set(PLATFORM_DEFINE TARGET_NES)

    FetchContent_MakeAvailable(intsh tuple)

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
    # two-pass build below. ca65 is part of cc65 (a separate, system-installed
    # toolchain) so it legitimately falls back to PATH. llvm-readelf ships with
    # the SDK: NO_DEFAULT_PATH pins it to the SDK bin (derived from the NES
    # compiler's own directory) so a PATH-resolved llvm-mos can never sneak in.
    find_program(CA65 ca65 HINTS "${CC65_DIR}/bin" "C:/cc65/bin" REQUIRED)
    get_filename_component(LLVM_MOS_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(LLVM_READELF llvm-readelf
        HINTS "${LLVM_MOS_BIN}" NO_DEFAULT_PATH REQUIRED)

    set(FAMISTUDIO_CFG_NTSC_SUPPORT 1 CACHE STRING "FamiStudio: enable NTSC timing tables")
    set(FAMISTUDIO_CFG_PAL_SUPPORT  0 CACHE STRING "FamiStudio: enable PAL timing tables")
    set(FAMISTUDIO_CFG_DPCM_SUPPORT 1 CACHE STRING "FamiStudio: enable DPCM sample playback")
    set(FAMISTUDIO_CFG_SFX_SUPPORT  1 CACHE STRING "FamiStudio: enable SFX engine")
    set(FAMISTUDIO_CFG_SFX_STREAMS  2 CACHE STRING "FamiStudio: number of concurrent SFX streams")
    set(FAMISTUDIO_USE_VOLUME_TRACK 1 CACHE STRING "FamiStudio: enable per-note volume track")

    if(FAMISTUDIO_CFG_NTSC_SUPPORT AND FAMISTUDIO_CFG_PAL_SUPPORT)
        message(FATAL_ERROR
                "FAMISTUDIO_CFG_NTSC_SUPPORT and FAMISTUDIO_CFG_PAL_SUPPORT "
                "are both enabled; DUAL mode requires a runtime region "
                "detection routine that this project does not yet provide.")
    endif()

    # Common FamiStudio configuration block. The pass 1 build uses this
    # as-is; the pass 2 build prepends a generated padding include.
    set(FAMISTUDIO_CFG
            ".define FAMISTUDIO_CA65_ZP_SEGMENT   ZEROPAGE
.define FAMISTUDIO_CA65_RAM_SEGMENT  BSS
.define FAMISTUDIO_CA65_CODE_SEGMENT CODE

FAMISTUDIO_CFG_EXTERNAL     = 1
FAMISTUDIO_CFG_NTSC_SUPPORT = ${FAMISTUDIO_CFG_NTSC_SUPPORT}
FAMISTUDIO_CFG_PAL_SUPPORT  = ${FAMISTUDIO_CFG_PAL_SUPPORT}
FAMISTUDIO_CFG_DPCM_SUPPORT = ${FAMISTUDIO_CFG_DPCM_SUPPORT}
FAMISTUDIO_CFG_SFX_SUPPORT  = ${FAMISTUDIO_CFG_SFX_SUPPORT}
FAMISTUDIO_CFG_SFX_STREAMS  = ${FAMISTUDIO_CFG_SFX_STREAMS}
FAMISTUDIO_USE_VOLUME_TRACK = ${FAMISTUDIO_USE_VOLUME_TRACK}

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

    file(GLOB_RECURSE DEMO_ASM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/*.s)
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

    file(GLOB NES_SOURCES CONFIGURE_DEPENDS src/nes/*.cpp)
    add_library(platform-nes STATIC ${NES_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
            ${CMAKE_BINARY_DIR}/famistudio_include
    )
    target_compile_definitions(platform-nes PUBLIC
            ${PLATFORM_DEFINE}
            FAMISTUDIO_CFG_NTSC_SUPPORT=${FAMISTUDIO_CFG_NTSC_SUPPORT}
            FAMISTUDIO_CFG_PAL_SUPPORT=${FAMISTUDIO_CFG_PAL_SUPPORT}
            FAMISTUDIO_CFG_DPCM_SUPPORT=${FAMISTUDIO_CFG_DPCM_SUPPORT}
            FAMISTUDIO_CFG_SFX_SUPPORT=${FAMISTUDIO_CFG_SFX_SUPPORT}
            FAMISTUDIO_CFG_SFX_STREAMS=${FAMISTUDIO_CFG_SFX_STREAMS}
            FAMISTUDIO_USE_VOLUME_TRACK=${FAMISTUDIO_USE_VOLUME_TRACK}
    )
    target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

    file(GLOB_RECURSE DEMO_C_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/*.cpp)

    set(PASS1_ELF ${CMAKE_BINARY_DIR}/demo_pass1.elf)
    add_custom_command(
            OUTPUT ${PASS1_ELF}
            COMMAND ${CMAKE_CXX_COMPILER}
            # This hand-rolled invocation bypasses CMAKE_CXX_STANDARD and the
            # demo target's options, so the standard and the #embed extension
            # silencer must be set explicitly to match the demo target. -x c++
            # forces the C++ frontend for the source inputs.
            -std=gnu++23 -Wno-c23-extensions -x c++
            -Os -DTARGET_NES -DLANDSCAPE
            -I${CMAKE_SOURCE_DIR}/include
            -I${CMAKE_SOURCE_DIR}/include/umbrella
            -I${CMAKE_BINARY_DIR}/famistudio_include
            -I${intsh_SOURCE_DIR}
            -I${tuple_SOURCE_DIR}
            ${DEMO_C_SOURCES}
            # Stop forcing the C++ frontend before the archive/object inputs
            # so the driver treats them as link inputs, not source files.
            -x none
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

    # metatiles.cpp includes chr.hpp, which #embeds the demo/chr art files and
    # (being the includer) emits the folded CHR ROM image. Declare the art as an
    # explicit dependency so editing it retriggers a recompile.
    file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
            PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

    add_executable(demo ${DEMO_C_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".nes")

    target_link_libraries(demo PRIVATE platform-nes)

    # CHR art is pulled in with #embed (C23), which Clang flags as an extension
    # in C++ mode. It is the intended mechanism here, so silence the diagnostic.
    target_compile_options(demo PRIVATE -Wno-c23-extensions)

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
    # GameCube + Wii target (libogc / GX), via the devkitPPC toolchain.
    #
    # One backend (src/ogc) serves both consoles -- they share the Gekko/Broadway
    # CPU and the Flipper/Hollywood GX pipeline; only input and a couple of build
    # flags differ. The Wii .dol additionally runs unmodified on Wii U via vWii,
    # so this single branch covers all three with the bar set at a Homebrew app.
    #
    # The toolchain file (GameCube.cmake / Wii.cmake) is supplied via the
    # gc-release / wii-release presets; it sets CMAKE_SYSTEM_NAME and, through
    # Platform/libogc.cmake, the arch flags, the -logc -lm link line (plus
    # -lwiiuse -lbte on Wii) and the ogc_create_dol() helper used below.
    # ----------------------------------------------------------------------
elseif(TARGET_PLATFORM STREQUAL "gc" OR TARGET_PLATFORM STREQUAL "wii")
    project(platform-nes CXX)

    FetchContent_MakeAvailable(intsh tuple)

    if(TARGET_PLATFORM STREQUAL "gc")
        set(PLATFORM_DEFINE TARGET_GC)
    else()
        set(PLATFORM_DEFINE TARGET_WII)
    endif()

    # The OGC backend presents the shared software PPU (src/emu) through GX: the
    # emulator and all the nametable/OAM/palette write functions are
    # platform-agnostic and compiled here alongside the GX-specific
    # video/present, the PAD/WPAD input, and the (stub) audio.
    file(GLOB OGC_SOURCES CONFIGURE_DEPENDS src/ogc/*.cpp src/emu/*.cpp)
    add_library(platform-nes STATIC ${OGC_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    # TARGET_OGC is the shared "GameCube-or-Wii" group the engine headers gate on
    # (emulated PPU like SDL, but a fixed viewport and no SDL headers); TARGET_GC
    # / TARGET_WII select the console-specific bits (e.g. WPAD on Wii only).
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE} TARGET_OGC)

    # The on-target statistical PC sampler + on-screen overlay (src/ogc/
    # profiler.cpp and the overlay path in video.cpp) is a development tool, not
    # a shipping feature: it spins up a high-priority sampler thread and draws a
    # hex/bar overlay every frame. Compile it only into Debug builds via the
    # OGC_PROFILER gate, so Release .dols carry neither the thread nor the draw.
    # PRIVATE: only the library's own TUs (profiler.cpp/video.cpp) consult it.
    target_compile_definitions(platform-nes PRIVATE $<$<CONFIG:Debug>:OGC_PROFILER>)
    target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

    file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

    # metatiles.cpp includes chr.hpp, which #embeds the demo/chr art files and
    # (being the includer) emits the folded CHR ROM image. Declare the art as an
    # explicit dependency so editing it retriggers a recompile.
    file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
            PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

    add_executable(demo ${DEMO_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".elf")
    target_link_libraries(demo PRIVATE platform-nes)

    # CHR art is pulled in with #embed (C23). devkitPPC's GCC supports it without
    # a diagnostic here; keep the Clang-only silencer guard for parity with the
    # other branches (no-op under GCC).
    target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

    # Enable LTO for release-type configs. The software PPU is the hot path and
    # spans the platform-nes static lib (emu::GenerateFrame) and the demo (the
    # per-frame IRQ handlers it calls back into); cross-module inlining matters
    # here. CMake routes archiving through gcc-ar/gcc-ranlib once
    # INTERPROCEDURAL_OPTIMIZATION is set, so the .dol-from-.elf step is
    # unaffected. Mirrors the desktop branch below.
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

    # Convert the linked ELF into a bootable .dol (Homebrew Channel / loader
    # format). Defined by Platform/libogc.cmake once the OGC toolchain is active.
    ogc_create_dol(demo)

    # ----------------------------------------------------------------------
    # Nintendo 3DS target (libctru + citro2d), via the devkitARM toolchain.
    #
    # The 3DS presents the shared software PPU (src/emu) like the SDL backend:
    # emu::GenerateFrame rasterises one ARGB frame on the ARM11, which is
    # uploaded as a single RGBA8 texture and scaled to the top screen by
    # citro2d. The PICA200 has no paletted-texture format, so the GX/CI4 tilemap
    # path the GameCube/Wii branch uses cannot port -- a CPU raster into one
    # texture is the natural fit.
    #
    # The toolchain file (3DS.cmake) is supplied via the ctr-* presets; it sets
    # CMAKE_SYSTEM_NAME=Nintendo3DS and, through Platform/devkitArm.cmake, the
    # arch flags and the ctr_generate_smdh() / ctr_create_3dsx() helpers used
    # below. citro2d/citro3d/ctru are linked explicitly (the toolchain does not
    # add them implicitly the way libogc.cmake adds -logc).
    # ----------------------------------------------------------------------
elseif(TARGET_PLATFORM STREQUAL "ctr")
    project(platform-nes CXX)

    FetchContent_MakeAvailable(intsh tuple)

    # The 3DS backend presents the shared software PPU (src/emu) through citro2d:
    # the emulator and all the nametable/OAM/palette write functions are
    # platform-agnostic and compiled here alongside the citro2d video/present,
    # the hid input, and the (stub) audio.
    file(GLOB CTR_SOURCES CONFIGURE_DEPENDS src/3ds/*.cpp src/emu/*.cpp)
    add_library(platform-nes STATIC ${CTR_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    # TARGET_CTR is the group the engine headers gate on (emulated PPU like SDL,
    # but a fixed 32x30 viewport and no SDL headers -- same family as TARGET_OGC).
    target_compile_definitions(platform-nes PUBLIC TARGET_CTR)
    target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

    file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

    # metatiles.cpp includes chr.hpp, which #embeds the demo/chr art files and
    # (being the includer) emits the folded CHR ROM image. Declare the art as an
    # explicit dependency so editing it retriggers a recompile.
    file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
            PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

    add_executable(demo ${DEMO_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".elf")
    target_link_libraries(demo PRIVATE platform-nes citro2d citro3d ctru m)

    # CHR art is pulled in with #embed (C23). devkitARM's GCC supports it without
    # a diagnostic here; keep the Clang-only silencer guard for parity with the
    # other branches (no-op under GCC).
    target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

    # Enable LTO for release-type configs, mirroring the OGC + desktop branches:
    # the software PPU (emu::GenerateFrame) spans the static lib and the demo's
    # per-frame IRQ handlers, so cross-module inlining matters on the hot path.
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

    # Wrap the linked ELF as a .3dsx (homebrew loader format) with an SMDH icon/
    # metadata banner. Both helpers come from devkitARM's Tools3DS.cmake once the
    # 3DS toolchain is active.
    ctr_generate_smdh(demo.smdh
            NAME "platform-nes demo"
            DESCRIPTION "NES engine demo on 3DS (citro2d)"
            AUTHOR "brette"
    )
    ctr_create_3dsx(demo
            SMDH ${CMAKE_BINARY_DIR}/demo.smdh
    )

    # ----------------------------------------------------------------------
    # Nintendo Switch target (libnx), via the devkitA64 toolchain.
    #
    # The Switch presents the shared software PPU (src/emu) like the SDL/3DS
    # backends: emu::GenerateFrame rasterises a 256x240 ARGB frame and the CPU
    # integer-scales it into the libnx framebuffer. The Tegra is wildly
    # overpowered for NES-resolution pixels, so a straight scale-blit avoids
    # pulling in deko3d/EGL for this first pass.
    #
    # The toolchain file (Switch.cmake) is supplied via the switch-* presets; it
    # sets CMAKE_SYSTEM_NAME=NintendoSwitch and, through Platform/NXTools.cmake,
    # the arch flags and the nx_generate_nacp() / nx_create_nro() helpers used
    # below. libnx is linked explicitly (the toolchain does not auto-link it).
    # ----------------------------------------------------------------------
    elseif(TARGET_PLATFORM STREQUAL "switch")
        project(platform-nes CXX)

        FetchContent_MakeAvailable(intsh tuple)

        # The Switch backend presents the shared software PPU (src/emu): the
        # emulator and all the nametable/OAM/palette write functions are
        # platform-agnostic and compiled here alongside the libnx framebuffer
        # video/present, the hid input, and the audout audio.
        file(GLOB NX_SOURCES CONFIGURE_DEPENDS src/switch/*.cpp src/emu/*.cpp)
        add_library(platform-nes STATIC ${NX_SOURCES})
        target_include_directories(platform-nes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        # TARGET_NX is the group the engine headers gate on (emulated PPU, fixed
        # 32x30 viewport, no SDL headers -- same family as TARGET_OGC/TARGET_CTR).
        target_compile_definitions(platform-nes PUBLIC TARGET_NX)
        target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

        # metatiles.cpp #embeds the demo/chr art; declare it as a dependency so
        # editing the art retriggers a recompile.
        file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
                PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        target_link_libraries(demo PRIVATE platform-nes nx)

        # CHR art is pulled in with #embed (C23); keep the Clang-only silencer for
        # parity with the other branches (no-op under devkitA64's GCC).
        target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        # Enable LTO for release-type configs, mirroring the other branches.
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

        # libnx audout is fixed at 48kHz/2ch/s16, so encode the desktop WAV to
        # exactly that raw PCM at build time and bake it into the .nro RomFS
        # (src/switch/audio.cpp streams it). ffmpeg is provided by
        # docker/Dockerfile.switch. This leaves demo/tracks (the lossless WAV)
        # untouched for desktop -- the conversion output lives only in the build
        # tree, mirroring the web branch's OGG step.
        find_program(FFMPEG ffmpeg REQUIRED)
        set(NX_ROMFS ${CMAKE_BINARY_DIR}/romfs)
        set(NX_RAW   ${NX_ROMFS}/tracks/pc_audio.raw)
        set(NX_WAV   ${CMAKE_SOURCE_DIR}/demo/tracks/pc_audio.wav)
        # nx_create_nro() validates the RomFS dir exists at configure time, but
        # the audio asset inside it is only produced at build time (below). Create
        # the directory now so configure passes; the build step then fills it.
        file(MAKE_DIRECTORY ${NX_ROMFS}/tracks)
        add_custom_command(
                OUTPUT ${NX_RAW}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${NX_ROMFS}/tracks
                COMMAND ${FFMPEG} -y -loglevel error -i ${NX_WAV}
                        -ar 48000 -ac 2 -f s16le ${NX_RAW}
                DEPENDS ${NX_WAV}
                COMMENT "Encoding Switch audio: pc_audio.wav -> RomFS pc_audio.raw (48k s16le)"
                VERBATIM)
        add_custom_target(switch-assets DEPENDS ${NX_RAW})
        add_dependencies(demo switch-assets)

        # Wrap the linked ELF as a homebrew .nro with a NACP metadata banner and
        # the RomFS asset tree. Both helpers come from devkitA64's NXTools.cmake
        # once the Switch toolchain is active.
        nx_generate_nacp(demo.nacp
                NAME "platform-nes demo"
                AUTHOR "brette"
                VERSION "1.0.0"
        )
        nx_create_nro(demo
                NACP ${CMAKE_BINARY_DIR}/demo.nacp
                ROMFS ${NX_ROMFS}
        )

    # ----------------------------------------------------------------------
    # Desktop targets (macOS, Windows, Linux)
    # ----------------------------------------------------------------------
else()
    project(platform-nes CXX)

    FetchContent_MakeAvailable(intsh tuple)

    if(TARGET_PLATFORM STREQUAL "mac")
        set(PLATFORM_DEFINE TARGET_MACOS)
    elseif(TARGET_PLATFORM STREQUAL "win")
        set(PLATFORM_DEFINE TARGET_WINDOWS)
    elseif(TARGET_PLATFORM STREQUAL "linux")
        set(PLATFORM_DEFINE TARGET_LINUX)
    elseif(TARGET_PLATFORM STREQUAL "web")
        # Emscripten/WASM. This is "just another SDL3 desktop target": the
        # emsdk toolchain (active via the web-* presets / emcmake) ports SDL3's
        # video, audio, keyboard and gamepad to the browser, so the src/SDL3
        # backend and the emu core compile unchanged. The only target-specific
        # bits are Asyncify (to let the blocking main loop yield to the browser)
        # and packaging the runtime assets into MEMFS -- handled in the
        # if(EMSCRIPTEN) block after the demo target below.
        set(PLATFORM_DEFINE TARGET_WEB)
    else()
        message(FATAL_ERROR "Set -DTARGET_PLATFORM to one of: nes, win, mac, linux, web")
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

    # The SDL3 backend presents the shared software PPU (src/emu): the emulator
    # and all the nametable/OAM/palette write functions are platform-agnostic
    # and compiled here alongside the SDL-specific window/present/input/audio.
    file(GLOB SDL3_SOURCES CONFIGURE_DEPENDS src/SDL3/*.cpp src/emu/*.cpp)
    add_library(platform-nes STATIC ${SDL3_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})
    target_link_libraries(platform-nes PUBLIC SDL3::SDL3-static)
    target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

    if(TARGET_PLATFORM STREQUAL "linux")
        target_link_libraries(platform-nes PUBLIC usb-1.0)
    endif()

    # The web build decodes a compressed Vorbis track at runtime (the full WAV
    # would blow Cloudflare's 25MB/file limit); src/SDL3/audio.cpp #includes
    # stb_vorbis.c under __EMSCRIPTEN__. stb is header-only with no CMakeLists,
    # so FetchContent just populates it and we add the dir to the include path.
    # Desktop never fetches or compiles it.
    if(EMSCRIPTEN)
        FetchContent_Declare(
                stb
                GIT_REPOSITORY https://github.com/nothings/stb.git
                GIT_TAG        master
                GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(stb)
        target_include_directories(platform-nes PRIVATE ${stb_SOURCE_DIR})
    endif()

    file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

    # metatiles.cpp includes chr.hpp, which #embeds the demo/chr art files and
    # (being the includer) emits the folded CHR ROM image. Declare the art as an
    # explicit dependency so editing it retriggers a recompile.
    file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
            PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

    add_executable(demo ${DEMO_SOURCES})
    target_link_libraries(demo PRIVATE platform-nes)

    # CHR art is pulled in with #embed (C23), which Clang flags as an extension
    # in C++ mode. It is the intended mechanism here, so silence the diagnostic.
    # Clang-only: GCC doesn't know this flag and would emit an "unrecognized
    # option" note, and its own #embed support doesn't warn here anyway.
    target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

    # ------------------------------------------------------------------
    # Emscripten / WebAssembly packaging.
    #
    # The browser owns the event loop and cannot be blocked, but the demo's
    # main() is a classic blocking `while (!quit) { ...; WaitForPresent(); }`.
    # -sASYNCIFY lets that loop yield to requestAnimationFrame transparently:
    # the SDL3 backend's per-frame wait calls emscripten_sleep() (guarded by
    # __EMSCRIPTEN__ in src/SDL3/video.cpp), which Asyncify turns into a real
    # browser yield -- so main.cpp stays byte-identical to every other platform.
    #
    # The demo loads its music/SFX from tracks/ and sfx/ at runtime via
    # SDL_LoadWAV; on the web there is no host filesystem, so those trees are
    # baked into the MEMFS virtual FS with --preload-file (producing demo.data).
    # They mount at /tracks and /sfx, matching the relative paths the desktop
    # build uses from the executable's working directory.
    #
    # SUFFIX .html makes emcc emit the HTML shell + demo.js + demo.wasm so the
    # artifact is openable directly (served over http).
    if(EMSCRIPTEN)
        set_target_properties(demo PROPERTIES SUFFIX ".html")

        # The desktop bundle's tracks/ is a 47MB PCM WAV -- too big for the web
        # (Cloudflare Pages rejects any single deployed file > 25MB, and the WAV
        # lands whole inside demo.data). Encode it to ~3MB Vorbis at build time
        # and bake only that into MEMFS; src/SDL3/audio.cpp decodes it with
        # stb_vorbis at load. Nothing under demo/ changes, so desktop keeps the
        # lossless WAV. ffmpeg is provided by docker/Dockerfile.web.
        find_program(FFMPEG ffmpeg REQUIRED)
        set(WEB_ASSET_DIR ${CMAKE_BINARY_DIR}/web-assets)
        set(WEB_OGG ${WEB_ASSET_DIR}/tracks/pc_audio.ogg)
        set(WEB_WAV ${CMAKE_SOURCE_DIR}/demo/tracks/pc_audio.wav)
        add_custom_command(
                OUTPUT ${WEB_OGG}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${WEB_ASSET_DIR}/tracks
                COMMAND ${FFMPEG} -y -loglevel error -i ${WEB_WAV}
                        -c:a libvorbis -q:a 4 ${WEB_OGG}
                DEPENDS ${WEB_WAV}
                COMMENT "Encoding web audio: pc_audio.wav -> pc_audio.ogg (Vorbis)"
                VERBATIM)
        add_custom_target(web-assets DEPENDS ${WEB_OGG})
        add_dependencies(demo web-assets)

        # The single --preload-file mounts the generated OGG at /tracks, matching
        # the relative path the audio backend opens (sfx/ is NES-only .s source
        # and PlaySFX is a no-op on this backend, so nothing is baked for it).
        # SHELL: keeps the flag and path as one token so link-option dedup can't
        # split them.
        target_link_options(demo PRIVATE
                -sASYNCIFY
                -sALLOW_MEMORY_GROWTH=1
                "SHELL:--preload-file ${WEB_ASSET_DIR}/tracks@/tracks"
        )
        # Re-link when the encoded asset changes (emcc reads it at link time).
        set_property(TARGET demo APPEND PROPERTY LINK_DEPENDS "${WEB_OGG}")
    endif()

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

    # Desktop runs the demo from its own directory and loads tracks/ + sfx/ by
    # relative path, so they are copied next to the executable. On Emscripten the
    # same trees are baked into MEMFS via --preload-file (above), so skip the copy.
    if(NOT EMSCRIPTEN)
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
    endif()

    # ------------------------------------------------------------------
    # Strip the final executable in non-Debug builds. MSVC has no strip
    # (symbols live in a separate .pdb), so only run when CMAKE_STRIP is
    # available (GCC/Clang/MinGW toolchains). Emscripten output is .wasm/.js,
    # not an ELF the host `strip` understands, so skip it there (emcc strips
    # itself in non-debug optimisation levels anyway).
    # ------------------------------------------------------------------
    if(CMAKE_STRIP AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT EMSCRIPTEN)
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

            # LGPL-2.1 (libusb) and the zlib license (SDL3) both require
            # their notices to travel with any binary redistribution. The
            # AppImage is exactly such a binary, so the license texts are
            # copied into usr/share/doc/platform-nes/ before linuxdeploy
            # seals the bundle.
            set(_APPDIR_LICENSES "${APPDIR}/usr/share/doc/platform-nes")

            add_custom_target(demo-appimage
                    COMMAND ${CMAKE_COMMAND} -E rm -rf "${APPDIR}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${APPDIR}/usr/bin"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${_APPDIR_LICENSES}"
                    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:demo> "${APPDIR}/usr/bin/demo"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/demo/tracks" "${APPDIR}/usr/bin/tracks"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/demo/sfx"    "${APPDIR}/usr/bin/sfx"
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${CMAKE_SOURCE_DIR}/LICENSE"     "${_APPDIR_LICENSES}/LICENSE"
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${sdl3_SOURCE_DIR}/LICENSE.txt"  "${_APPDIR_LICENSES}/LICENSE.SDL3"
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${libusb_cmake_SOURCE_DIR}/libusb/COPYING" "${_APPDIR_LICENSES}/LICENSE.libusb"
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