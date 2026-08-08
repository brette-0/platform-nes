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

# Nametable mirroring orientation: platform-nes.hpp requires exactly one of
# LANDSCAPE or PORTRAIT to be defined. Set ORIENTATION in local.cmake to
# override per-developer; defaults to LANDSCAPE when left unset.
if(NOT ORIENTATION)
    set(ORIENTATION "LANDSCAPE")
endif()
if(NOT ORIENTATION STREQUAL "LANDSCAPE" AND NOT ORIENTATION STREQUAL "PORTRAIT")
    message(FATAL_ERROR
        "ORIENTATION must be LANDSCAPE or PORTRAIT, got: \"${ORIENTATION}\"")
endif()
add_compile_definitions(${ORIENTATION})
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
    # Mapper 4 (MMC3), not NROM: linked against the generic nes-platform
    # crt0 (LLVM_MOS_PLATFORM=nes, not nes-nrom), since nes-nrom's own
    # crt0.o/link.ld hardcode NROM-specific __prg_rom_size/__chr_rom_size
    # defaults that this MMC3 build must supply itself (see mmc3-helper.ld's
    # COROLLARY-equivalent reasoning, carried over from vrc1-helper.ld).
    #
    # The generic nes platform has no default link.ld (unlike nes-nrom and
    # the other mapper subtargets), so -T must be supplied explicitly. This
    # has to happen *before* project() below: project() runs CMake's own
    # compiler-identification and ABI-detection probes, which both perform
    # real links, and without -T those fail before any of this project's
    # own code is ever touched ("cannot find linker script link.ld", then
    # a harder-to-place "No known features for CXX compiler" once
    # identification itself silently comes back empty).
    #
    # CMAKE_CXX_FLAGS, not CMAKE_EXE_LINKER_FLAGS: CMake's own
    # CMAKE_DETERMINE_COMPILER_ID probe builds its test invocation from
    # CMAKE_<LANG>_FLAGS specifically (see CMakeDetermineCompilerId.cmake) and
    # does not consult CMAKE_EXE_LINKER_FLAGS at all, so -T placed there is
    # invisible to that one probe even though it reaches every ordinary
    # target's link step. Compile-only steps harmlessly warn that -T is
    # unused; silenced below rather than left to spam every translation unit.
    # BANKED CALL, Phase 5: TOPLEVEL_LD (originally VRC1_LD, renamed when the
    # demo converted from VRC1 to MMC3 -- the variable was never actually
    # mapper-specific, just named after whichever mapper existed first) is
    # not hardcoded into the library's own tree -- the library only ever
    # supplies a HELPER script (src/nes/mappers/mmc3-helper.ld: hardware
    # facts and mechanical plumbing), never the actual, final linker script a
    # real project links against (see BANKED_CALL_THEORY.txt's "THE FINAL
    # LINKER SCRIPT MUST BE USER-MADE" section -- written against VRC1, but
    # the principle is mapper-agnostic). The demo supplies its own top-level
    # script (demo/link.ld) via local.cmake, matching LLVM_MOS_SDK's own "no
    # fallback, missing is a hard configure-time error" convention just
    # above -- see local.cmake.example.
    if(NOT TOPLEVEL_LD)
        message(FATAL_ERROR
            "NES build requires a top-level linker script, but TOPLEVEL_LD is "
            "unset. Copy local.cmake.example to local.cmake and set it to "
            "your own project's top-level script, e.g.\n"
            "    set(TOPLEVEL_LD \"\${CMAKE_SOURCE_DIR}/demo/link.ld\" CACHE FILEPATH \"\" FORCE)\n"
            "That script should INCLUDE mmc3-helper.ld (src/nes/mappers) for "
            "the hardware/mechanical parts it doesn't need to reinvent.")
    endif()
    # -L: GNU ld/lld's INCLUDE directive does NOT search the directory of the
    # script doing the including -- only the linker's own -L search paths --
    # so a top-level script's `INCLUDE mmc3-helper.ld` needs this to resolve
    # at all.
    set(CMAKE_CXX_FLAGS "-T ${TOPLEVEL_LD} -L${CMAKE_SOURCE_DIR}/src/nes/mappers -Wno-unused-command-line-argument")

    set(LLVM_MOS_PLATFORM nes)
    find_package(llvm-mos-sdk REQUIRED)
    project(platform-nes CXX)
    set(PLATFORM_DEFINE TARGET_NES)

    # NROM has no bankswitching, so RESET has nowhere to "escape to" and
    # stays plain int main(). Every other mapper needs its RESET/main pinned
    # to the fixed bank (interrupts.hpp), since main is the code's one
    # guaranteed-always-resident home base while everything else may be
    # banked in and out from under it. LLVM_MOS_PLATFORM is this project's
    # one source of truth for which mapper is being built, set just above.
    if(NOT LLVM_MOS_PLATFORM STREQUAL "nes-nrom")
        list(APPEND PLATFORM_DEFINE NES_MAPPER_BANKSWITCHED)
    endif()

    # Mapper/submapper number for header.hpp's HEADER() macro (see
    # nes2.cpp). This is the only mapper subtarget this branch currently
    # builds (TOPLEVEL_LD is hardcoded above), so it's unconditional for now
    # -- becomes an if/elseif alongside TOPLEVEL_LD once a second mapper
    # subtarget exists, one MAPPER/SUBMAPPER pair per board.
    list(APPEND PLATFORM_DEFINE "MAPPER=4" "SUBMAPPER=0")

    # Alternative nametable wiring (NES2.0 flags 6, bit 3 -- the "four-screen"
    # bit): the board supplies its own nametable memory/routing instead of the
    # console's default 2-way CIRAM mirror (see TVROM's extra VRAM chip, MMC5's
    # ExRAM/fill-mode quadrant select, Sunsoft-4's CHR-as-nametable trick).
    # header.hpp/nes2.hpp only ever tests this for non-zero-ness -- the value
    # itself is not a hardware quantity read out of the header, just a knob a
    # given board's glue code may interpret its own way. Set
    # ALTERNATIVE_NAMETABLE in local.cmake to any non-zero value to set the
    # bit; unset (the default) leaves it clear.
    if(DEFINED ALTERNATIVE_NAMETABLE)
        list(APPEND PLATFORM_DEFINE "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()

    # -D-prefixed form for the pass-1 custom command below, which invokes the
    # compiler directly and so doesn't pick up target_compile_definitions.
    list(TRANSFORM PLATFORM_DEFINE PREPEND "-D" OUTPUT_VARIABLE PLATFORM_DEFINE_FLAGS)

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

    # Locate the ca65 assembler and llvm-readelf binary used below. ca65 is
    # part of cc65 (a separate, system-installed toolchain) so it legitimately
    # falls back to PATH. llvm-readelf ships with the SDK: NO_DEFAULT_PATH
    # pins it to the SDK bin (derived from the NES compiler's own directory)
    # so a PATH-resolved llvm-mos can never sneak in.
    find_program(CA65 ca65 HINTS "${CC65_DIR}/bin" "C:/cc65/bin" REQUIRED)
    get_filename_component(LLVM_MOS_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(LLVM_READELF llvm-readelf
        HINTS "${LLVM_MOS_BIN}" NO_DEFAULT_PATH REQUIRED)

    # demo/famistudio_config.s is the REAL, project-owned FamiStudio config
    # file (see its own header comment, and BANKED_CALL_THEORY.txt's
    # "RESOLVED: famistudio_config.s moves to the demo root as a real,
    # directly-edited file" -- this used to be a CMake-generated string
    # instead, which is what that entry flags as the thing to fix). CMake's
    # own job here is reduced to READING values back out of that real file
    # (regex-extracted, never written) so audio.cpp's #if FAMISTUDIO_CFG_*
    # checks and its BANKED_EXTERN segment tag stay in sync with whatever
    # this file actually says -- editing the .s file is what changes the
    # engine's real configuration; these extracted CMake variables can never
    # drift from it because they're read from it, not a separate source.
    file(READ ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s FAMISTUDIO_CONFIG_TEXT)

    macro(famistudio_extract var pattern)
        string(REGEX MATCH "${pattern}" _famistudio_match "${FAMISTUDIO_CONFIG_TEXT}")
        if(NOT _famistudio_match)
            message(FATAL_ERROR "demo/famistudio_config.s: couldn't find ${var} (expected to match: ${pattern})")
        endif()
        set(${var} ${CMAKE_MATCH_1})
    endmacro()

    famistudio_extract(FAMISTUDIO_SEGMENT          "FAMISTUDIO_CA65_CODE_SEGMENT[ \t]+([A-Za-z0-9_]+)")
    famistudio_extract(FAMISTUDIO_CFG_NTSC_SUPPORT "FAMISTUDIO_CFG_NTSC_SUPPORT[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_CFG_PAL_SUPPORT  "FAMISTUDIO_CFG_PAL_SUPPORT[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_CFG_DPCM_SUPPORT "FAMISTUDIO_CFG_DPCM_SUPPORT[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_CFG_SFX_SUPPORT  "FAMISTUDIO_CFG_SFX_SUPPORT[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_CFG_SFX_STREAMS  "FAMISTUDIO_CFG_SFX_STREAMS[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_USE_VOLUME_TRACK "FAMISTUDIO_USE_VOLUME_TRACK[ \t]*=[ \t]*([0-9]+)")

    if(FAMISTUDIO_CFG_NTSC_SUPPORT AND FAMISTUDIO_CFG_PAL_SUPPORT)
        message(FATAL_ERROR
                "demo/famistudio_config.s: FAMISTUDIO_CFG_NTSC_SUPPORT and "
                "FAMISTUDIO_CFG_PAL_SUPPORT are both enabled; DUAL mode "
                "requires a runtime region detection routine that this "
                "project does not yet provide.")
    endif()

    file(COPY ${FAMISTUDIO_DIR}/famistudio_ca65.h
            DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)

    if(EXISTS ${FAMISTUDIO_DIR}/famistudio_llvmmos.h)
        file(COPY ${FAMISTUDIO_DIR}/famistudio_llvmmos.h
                DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    endif()

    # Assemble the real, project-owned config once. There is no longer a
    # separate "pass 1 without padding, pass 2 with padding" split -- see
    # the measurement step below for why: it was already a permanent no-op,
    # so building the exact same content twice bought nothing. The one
    # object below is used both for the diagnostic measurement link and the
    # real demo link.
    set(FAMISTUDIO_OBJ ${CMAKE_BINARY_DIR}/famistudio.o)
    add_custom_command(
            OUTPUT ${FAMISTUDIO_OBJ}
            COMMAND ${CA65}
            -I ${CMAKE_SOURCE_DIR}/demo
            -I ${FAMISTUDIO_DIR}
            -I ${FAMISTUDIO_DIR}/NoteTables
            -o ${FAMISTUDIO_OBJ}
            ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s
            DEPENDS ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s
            COMMENT "Assembling FamiStudio"
    )

    # demo/famistudio_config.s is handled explicitly above (assembled once
    # into FAMISTUDIO_OBJ) -- excluded here so it isn't ALSO picked up and
    # assembled a second time by this generic "every hand-written .s file"
    # loop, which would produce a second object defining the same symbols.
    file(GLOB_RECURSE DEMO_ASM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/*.s)
    list(REMOVE_ITEM DEMO_ASM_SOURCES ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s)
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

    file(GLOB_RECURSE NES_SOURCES CONFIGURE_DEPENDS src/nes/*.cpp)
    # This build targets MMC3 only (TOPLEVEL_LD/MAPPER above) -- vrc1.cpp
    # still exists for a VRC1 project to use, but it also defines a global
    # extern "C" _reset (and other symbols mmc3.cpp defines too), which the
    # glob above would otherwise hand to this same static library: since
    # nothing in an MMC3 build references anything vrc1.cpp provides,
    # leaving it in costs nothing at RUNTIME, but a duplicate-_reset link
    # error is a real risk the moment anything ELSE in the link happens to
    # pull that archive member in. Excluded here, the same pattern already
    # used above for famistudio_config.s's own glob -- becomes conditional
    # (alongside MAPPER/TOPLEVEL_LD) once a real "pick your mapper" build
    # option exists instead of this hardcoded single-target branch.
    list(REMOVE_ITEM NES_SOURCES ${CMAKE_SOURCE_DIR}/src/nes/mappers/vrc1.cpp)
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
            # Unquoted on purpose -- audio.cpp stringizes it itself (see
            # FAMISTUDIO_SEGMENT_STR there), the standard two-level macro
            # stringize idiom, rather than trying to pass an already-quoted
            # string literal through this raw command-line definition (which
            # would need shell-escaping that's easy to get subtly wrong).
            FAMISTUDIO_SEGMENT=${FAMISTUDIO_SEGMENT}
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
            -Os ${PLATFORM_DEFINE_FLAGS} -D${ORIENTATION}
            # Reserve 10 ZP bytes for FamiStudio (8 vars + 2 alignment slack).
            # mos-nes.cfg defaults to -mlto-zp=224 (the full region); capping at
            # 214 prevents mos-zero-page-alloc from filling the last 10 bytes and
            # leaving no room for FamiStudio's ZEROPAGE segment.
            -mlto-zp=214
            # This custom command calls the compiler directly, bypassing
            # CMAKE_EXE_LINKER_FLAGS (which supplies -T/-L for every ordinary
            # CMake target's link step) -- so -T/-L have to be repeated here too.
            -T ${TOPLEVEL_LD}
            -L${CMAKE_SOURCE_DIR}/src/nes/mappers
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
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
            -o ${CMAKE_BINARY_DIR}/demo_pass1
            DEPENDS platform-nes
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
            ${DEMO_C_SOURCES}
            COMMENT "Linking diagnostic ELF for ZP/BSS measurement"
    )

    # ------------------------------------------------------------------
    # Diagnostic only: report how much ZP/BSS the C++ side actually uses.
    # Informational -- measure_sections.cmake's own header comment explains
    # why this can never feed back into a real padding step (llvm-mos's own
    # linker script already places ca65's ZEROPAGE/BSS after the C-side
    # sections via wildcard order; adding real padding here would double-
    # count the reservation). Nothing depends on this file's contents.
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
            COMMENT "Extracting ZP/BSS sizes (diagnostic only)"
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

    add_dependencies(demo demo_famistudio_asm)

    # CA65_PADDING is listed here purely so the diagnostic ZP/BSS measurement
    # (informational only -- see its own comment above) actually runs as
    # part of a normal build; nothing downstream reads its output.
    add_custom_target(demo_famistudio_asm
            DEPENDS ${FAMISTUDIO_OBJ} ${DEMO_ASM_OBJECTS} ${CA65_PADDING}
    )

    # FamiStudio and the hand-written assembly objects are required by
    # every build configuration.
    target_link_options(demo PRIVATE
            -mlto-zp=214   # leaves 10 bytes at the top of zero page for FamiStudio's own ZEROPAGE segment
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

    # Alternative nametable wiring (see the TARGET_NES branch's own comment,
    # above): a fact about which board a build is modeling, not something
    # exclusive to the NES target itself, so this desktop/console preview
    # needs the same define to route mmc3.cpp's strong ppu::ReadNametable/
    # WriteNametable overrides correctly off-NES too.
    if(DEFINED ALTERNATIVE_NAMETABLE)
        list(APPEND PLATFORM_DEFINE "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()

    # The OGC backend presents the shared software PPU (src/emu) through GX: the
    # emulator and all the nametable/OAM/palette write functions are
    # platform-agnostic and compiled here alongside the GX-specific
    # video/present, the PAD/WPAD input, and the (stub) audio.
    file(GLOB OGC_SOURCES CONFIGURE_DEPENDS src/ogc/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
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
    file(GLOB CTR_SOURCES CONFIGURE_DEPENDS src/3ds/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
    add_library(platform-nes STATIC ${CTR_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    # TARGET_CTR is the group the engine headers gate on (emulated PPU like SDL,
    # but a fixed 32x30 viewport and no SDL headers -- same family as TARGET_OGC).
    target_compile_definitions(platform-nes PUBLIC TARGET_CTR)
    # Alternative nametable wiring (see the TARGET_NES branch's own comment,
    # near the top of this file): a fact about which board a build is
    # modeling, not something exclusive to the NES target itself.
    if(DEFINED ALTERNATIVE_NAMETABLE)
        target_compile_definitions(platform-nes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()
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
        file(GLOB NX_SOURCES CONFIGURE_DEPENDS src/switch/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        add_library(platform-nes STATIC ${NX_SOURCES})
        target_include_directories(platform-nes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        # TARGET_NX is the group the engine headers gate on (emulated PPU, fixed
        # 32x30 viewport, no SDL headers -- same family as TARGET_OGC/TARGET_CTR).
        target_compile_definitions(platform-nes PUBLIC TARGET_NX)
        # Alternative nametable wiring (see the TARGET_NES branch's own comment,
        # near the top of this file): a fact about which board a build is
        # modeling, not something exclusive to the NES target itself.
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(platform-nes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
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
    # Native Wii U target (devkitPPC + wut + the SDL2 Wii U portlib).
    #
    # This is the NATIVE Cafe OS target (.rpx / .wuhb), distinct from the Wii
    # .dol the gc/wii branch above produces (which runs on Wii U only through
    # vWii). The Espresso is wildly overpowered for an NES frame, so -- like the
    # Switch/3DS branches -- this presents the shared software PPU (src/emu):
    # emu::GenerateFrame rasterises one widescreen ARGB frame straight into an
    # SDL2 streaming texture that the GPU scales to fill the window.
    #
    # The whole backend (src/wiiu) is ordinary SDL2: the devkitPro wiiu-sdl2
    # portlib wraps GX2 + ProcUI + the TV scan buffer + VPAD/KPAD controllers
    # internally. This is SDL2 -- separate from the SDL3 desktop backend -- so no
    # code is shared with src/SDL3.
    #
    # The toolchain file (WiiU.cmake) is supplied via the wiiu-* presets; it sets
    # CMAKE_SYSTEM_NAME=CafeOS and, through Platform/CafeOS.cmake, the Espresso
    # arch flags and the wut_create_rpx() / wut_create_wuhb() helpers used below.
    # ----------------------------------------------------------------------
    elseif(TARGET_PLATFORM STREQUAL "wiiu")
        project(platform-nes CXX)

        FetchContent_MakeAvailable(intsh tuple)

        # The SDL2 Wii U portlib (devkitPro package wiiu-sdl2). Its CMake config
        # provides the SDL2::SDL2-static imported target, whose interface pulls in
        # the GX2/coreinit/VPAD system libs it wraps.
        find_package(SDL2 REQUIRED)

        # The Wii U backend presents the shared software PPU (src/emu): the
        # emulator and all the nametable/OAM/palette write functions are
        # platform-agnostic and compiled here alongside the SDL2 video/present,
        # the SDL2 GameController input, and the SDL2 audio.
        file(GLOB WIIU_SOURCES CONFIGURE_DEPENDS src/wiiu/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        add_library(platform-nes STATIC ${WIIU_SOURCES})
        target_include_directories(platform-nes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        # TARGET_WIIU is the native-Wii-U define; the engine headers pair it with
        # the Switch (TARGET_NX) family for the widescreen 52x30 (416x240)
        # viewport, and exclude the SDL3 headers (this backend uses SDL2).
        target_compile_definitions(platform-nes PUBLIC TARGET_WIIU)
        # Alternative nametable wiring (see the TARGET_NES branch's own comment,
        # near the top of this file): a fact about which board a build is
        # modeling, not something exclusive to the NES target itself.
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(platform-nes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(platform-nes PUBLIC SDL2::SDL2-static br0::intsh br0::tuple)

        # The music is EMBEDDED in the .rpx, not loaded from disk: a native Wii U
        # title only has a /vol/content mount when launched with its packaged
        # content, and a bare .rpx (e.g. dropped into Cemu) has none. So encode
        # the desktop WAV to a small Vorbis OGG and #embed it (src/wiiu/audio.cpp
        # decodes it with stb_vorbis at startup) -- it then plays however the
        # title is launched, and the artifact stays small. Mirrors the web branch.
        find_program(FFMPEG ffmpeg REQUIRED)
        FetchContent_Declare(
                stb
                GIT_REPOSITORY https://github.com/nothings/stb.git
                GIT_TAG        master
                GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(stb)
        target_include_directories(platform-nes PRIVATE ${stb_SOURCE_DIR})

        set(WIIU_ASSET_DIR ${CMAKE_BINARY_DIR}/wiiu-assets)
        set(WIIU_OGG ${WIIU_ASSET_DIR}/pc_audio.ogg)
        set(WIIU_WAV ${CMAKE_SOURCE_DIR}/demo/tracks/pc_audio.wav)
        # #embed reads pc_audio.ogg at compile time of audio.cpp; create the dir
        # now so it is on the include path at configure, and (a) make the lib
        # depend on the encode target so it runs first, (b) mark audio.cpp as
        # depending on the OGG so editing the source WAV retriggers a recompile.
        file(MAKE_DIRECTORY ${WIIU_ASSET_DIR})
        add_custom_command(
                OUTPUT ${WIIU_OGG}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${WIIU_ASSET_DIR}
                COMMAND ${FFMPEG} -y -loglevel error -i ${WIIU_WAV}
                        -ar 48000 -ac 2 -c:a libvorbis -q:a 4 ${WIIU_OGG}
                DEPENDS ${WIIU_WAV}
                COMMENT "Encoding Wii U audio: pc_audio.wav -> pc_audio.ogg (Vorbis, embedded)"
                VERBATIM)
        add_custom_target(wiiu-assets DEPENDS ${WIIU_OGG})
        add_dependencies(platform-nes wiiu-assets)
        # audio.cpp resolves `#embed <pc_audio.ogg>` via --embed-dir (the canonical
        # GCC mechanism; -I is not reliably searched for #embed). Scope both the
        # search dir and the rebuild-on-change dependency to that one source.
        set_source_files_properties(${CMAKE_SOURCE_DIR}/src/wiiu/audio.cpp
                PROPERTIES
                    OBJECT_DEPENDS  ${WIIU_OGG}
                    COMPILE_OPTIONS "--embed-dir=${WIIU_ASSET_DIR}")

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

        # metatiles.cpp #embeds the demo/chr art; declare it as a dependency so
        # editing the art retriggers a recompile.
        file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
                PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        # The demo supplies its own main() via the RESET macro, so the backend
        # builds with SDL_MAIN_HANDLED and SDL2main is deliberately NOT linked.
        target_link_libraries(demo PRIVATE platform-nes)

        # CHR art is pulled in with #embed (C23); keep the Clang-only silencer for
        # parity with the other branches (no-op under devkitPPC's GCC).
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

        # Convert the linked ELF into an .rpx (the Cafe OS executable format) and
        # package it as a .wuhb bundle (the Aroma/Homebrew-Launcher format). The
        # music is embedded in the .rpx (above), so the bundle needs no content
        # tree -- both artifacts are fully self-contained and play audio however
        # they are launched. Both helpers come from wut's CafeOS.cmake.
        wut_create_rpx(demo)
        wut_create_wuhb(demo
                NAME      "platform-nes demo"
                SHORTNAME "platform-nes"
                AUTHOR    "brette"
        )

        # ------------------------------------------------------------------
        # Optional: installable WUP title (the NON-homebrew path)
        #
        # The .rpx/.wuhb above are the homebrew artifacts (Aroma / HBL). For
        # people who want to *install* their game as a real Wii U title --
        # runnable with the system closed, no homebrew environment imposed --
        # this assembles the unpacked code/content/meta tree a Wii U title
        # needs and (when a common key is supplied) runs NUSPacker to encrypt
        # it into an installable WUP (.app/.h3/.tmd/.tik/.cert).
        #
        # This is a separate `demo-wup` target, NOT part of the default build:
        # the homebrew path stays key-free and self-contained, and the WUP
        # path only kicks in when someone asks for it (and only *packs* when a
        # key is present -- otherwise it still emits the tree for manual use).
        # The Wii U common key is a copyrighted secret: it is NEVER committed,
        # only passed in via WIIU_COMMON_KEY at build time (a CI secret / local
        # cache var, as a 32-hex string or a path to a file containing one).
        # ------------------------------------------------------------------
        set(WIIU_TITLE_ID     "000500001337C0DE" CACHE STRING "Wii U title id (16 hex)")
        set(WIIU_GROUP_ID     "0000C0DE"         CACHE STRING "Wii U group id (8 hex)")
        set(WIIU_PRODUCT_CODE "WUP-N-PNES"       CACHE STRING "Wii U product code")
        set(WIIU_COMPANY_CODE "0001"             CACHE STRING "Wii U company code")
        set(WIIU_LONGNAME     "${GAME_NAME}"     CACHE STRING "Wii U menu long name")
        set(WIIU_SHORTNAME    "${GAME_NAME}"     CACHE STRING "Wii U menu short name")
        set(WIIU_PUBLISHER    "brette"           CACHE STRING "Wii U menu publisher")
        set(WIIU_RPX_NAME     "demo.rpx"         CACHE STRING "rpx filename inside the WUP")
        set(WIIU_NUSPACKER_JAR "$ENV{WIIU_NUSPACKER_JAR}" CACHE STRING "Path to NUSPacker.jar")
        set(WIIU_COMMON_KEY   "$ENV{WIIU_COMMON_KEY}"     CACHE STRING "Wii U common key (32 hex) or file path")

        set(WUP_DIR     ${CMAKE_BINARY_DIR}/wup)
        set(WUP_CODE    ${WUP_DIR}/code)
        set(WUP_CONTENT ${WUP_DIR}/content)
        set(WUP_META    ${WUP_DIR}/meta)
        set(WUP_OUT     ${CMAKE_BINARY_DIR}/wup-installable)

        # The title metadata: configure_file substitutes the @WIIU_*@ tokens in
        # the proven dimok HBL-channel templates (src/wiiu/title/*.in) at
        # configure time, so editing a cache var re-emits the XMLs on reconfigure.
        configure_file(${CMAKE_SOURCE_DIR}/src/wiiu/title/app.xml.in  ${WUP_CODE}/app.xml  @ONLY)
        configure_file(${CMAKE_SOURCE_DIR}/src/wiiu/title/cos.xml.in  ${WUP_CODE}/cos.xml  @ONLY)
        configure_file(${CMAKE_SOURCE_DIR}/src/wiiu/title/meta.xml.in ${WUP_META}/meta.xml @ONLY)

        # The menu artwork. The Wii U menu wants UNCOMPRESSED, top-left-origin
        # TGAs at fixed sizes; derive them all from the existing demo/demo.png
        # icon with ImageMagick so a title ships with art out of the box. (The
        # boot sound, bootSound.btsnd, is optional -- the title boots silent
        # without it -- so it is intentionally omitted.)
        find_program(IMAGEMAGICK_CONVERT NAMES magick convert)
        set(WUP_ICON  ${WUP_META}/iconTex.tga)
        set(WUP_BOOTTV  ${WUP_META}/bootTvTex.tga)
        set(WUP_BOOTDRC ${WUP_META}/bootDrcTex.tga)
        set(WUP_BOOTLOGO ${WUP_META}/bootLogoTex.tga)
        set(WIIU_ICON_SRC ${CMAKE_SOURCE_DIR}/demo/demo.png)
        if(IMAGEMAGICK_CONVERT)
            # iconTex 128x128, bootTvTex 1280x720, bootDrcTex 854x480,
            # bootLogoTex ~170x42 -- all uncompressed, top-left origin.
            set(_tga_opts -define tga:image-origin=TopLeft -compress none -type TrueColor)
            add_custom_command(OUTPUT ${WUP_ICON}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 128x128! ${_tga_opts} ${WUP_ICON}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            add_custom_command(OUTPUT ${WUP_BOOTTV}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 1280x720 -background black -gravity center -extent 1280x720 ${_tga_opts} ${WUP_BOOTTV}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            add_custom_command(OUTPUT ${WUP_BOOTDRC}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 854x480 -background black -gravity center -extent 854x480 ${_tga_opts} ${WUP_BOOTDRC}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            add_custom_command(OUTPUT ${WUP_BOOTLOGO}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 170x42 -background black -gravity center -extent 170x42 ${_tga_opts} ${WUP_BOOTLOGO}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            set(WUP_TGAS ${WUP_ICON} ${WUP_BOOTTV} ${WUP_BOOTDRC} ${WUP_BOOTLOGO})
        else()
            message(STATUS "wiiu: ImageMagick not found; demo-wup will skip menu-art (TGA) generation")
            set(WUP_TGAS "")
        endif()

        # The packer step runs through a small -P script (cmake/wiiu_pack.cmake)
        # so the "pack vs. skip when no key" decision happens at BUILD time, not
        # configure time -- you can set WIIU_COMMON_KEY after configuring and
        # just rebuild demo-wup. The script also reads the key from a file if a
        # path was given, so a CI secret can be mounted as a file.
        add_custom_target(demo-wup
                # The rpx is built by the demo target; drop it into code/ as the
                # name cos.xml's argstr references.
                COMMAND ${CMAKE_COMMAND} -E copy
                        ${CMAKE_CURRENT_BINARY_DIR}/${WIIU_RPX_NAME} ${WUP_CODE}/${WIIU_RPX_NAME}
                # NUSPacker requires a non-empty content/; the title has no data
                # files (audio is embedded in the rpx), so seed a placeholder.
                COMMAND ${CMAKE_COMMAND} -E make_directory ${WUP_CONTENT}
                COMMAND ${CMAKE_COMMAND} -E touch ${WUP_CONTENT}/.keep
                COMMAND ${CMAKE_COMMAND}
                        -DWUP_DIR=${WUP_DIR}
                        -DWUP_OUT=${WUP_OUT}
                        -DNUSPACKER_JAR=${WIIU_NUSPACKER_JAR}
                        -DCOMMON_KEY=${WIIU_COMMON_KEY}
                        -DTITLE_ID=${WIIU_TITLE_ID}
                        -P ${CMAKE_SOURCE_DIR}/cmake/wiiu_pack.cmake
                DEPENDS demo ${WUP_TGAS}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Assembling Wii U installable title (code/content/meta) and packing WUP"
                VERBATIM)

    # ----------------------------------------------------------------------
    # Nintendo DS / DSi target (devkitARM + libnds), via the NDS.cmake toolchain.
    #
    # Unlike every other emulated-PPU backend (SDL/OGC/CTR/NX/WIIU), the DS does
    # NOT run the software rasteriser (emu::GenerateFrame). The DS is a 2D tile
    # console whose hardware is a near-cousin of the NES PPU, so this backend maps
    # the NES PPU onto the DS's NATIVE 2D hardware (Model B):
    #   * CHR pattern tables  -> DS character (tile) VRAM (2bpp -> 4bpp expand)
    #   * nametable+attribute -> DS BG tilemap entries (tile + palette + flip)
    #   * NES master palette  -> DS BGR555 palette RAM
    #   * OAM shadow          -> DS hardware OBJ (oamSet)
    #   * raster band timeline (emu::GenerateBands) -> a per-scanline HBlank scroll
    #     table, so mid-frame scroll changes and the sprite-0 split still work.
    # It therefore still pulls in src/emu for the platform-agnostic PPU bookkeeping
    # (nametable/OAM/palette writes, the band/IRQ timeline, the OAM shadow), but
    # the per-pixel rasteriser there goes unused.
    #
    # The 256x192 panel is 6 tiles shorter than the NES's 240px; the backend shows
    # a window onto the unaltered 256x240 world (the engine never compromises the
    # NES game for the DS). Audio: the 4 MB DS has no room for baked PCM, so audio
    # is stubbed (src/nds/audio.cpp) exactly like the GameCube/Wii backends -- the
    # user supplies a synthesiser. The DSi build (TARGET_DSI) has 16 MB and may
    # later embed crunched audio; for now it shares the DS stub path. Only the tiny
    # CHR art (a few KiB) is embedded.
    #
    # The toolchain file (NDS.cmake) is supplied via the nds-*/dsi-* presets; it
    # sets CMAKE_SYSTEM_NAME=NintendoDS and, through Platform/NintendoDS.cmake, the
    # ARM9 arch flags and the nds_create_rom() helper used below. libnds is linked
    # explicitly as -lnds9 (calico's ARM7 + linker script come in via the toolchain).
    # ----------------------------------------------------------------------
    elseif(TARGET_PLATFORM STREQUAL "nds" OR TARGET_PLATFORM STREQUAL "dsi")
        project(platform-nes CXX)

        FetchContent_MakeAvailable(intsh tuple)

        # The DS backend maps the shared PPU bookkeeping (src/emu) onto libnds 2D
        # hardware: the emulator and all the nametable/OAM/palette write functions
        # are platform-agnostic and compiled here alongside the libnds video/
        # present, the keypad input, and the (stub) audio.
        file(GLOB NDS_SOURCES CONFIGURE_DEPENDS src/nds/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        add_library(platform-nes STATIC ${NDS_SOURCES})
        target_include_directories(platform-nes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        # TARGET_NDS is the group the engine headers gate on (native 2D hardware,
        # 32x24 viewport, no SDL headers). The DSi build adds TARGET_DSI on top so
        # the audio path can opt into the larger RAM later.
        target_compile_definitions(platform-nes PUBLIC TARGET_NDS)
        if(TARGET_PLATFORM STREQUAL "dsi")
            target_compile_definitions(platform-nes PUBLIC TARGET_DSI)
        endif()
        # Alternative nametable wiring (see the TARGET_NES branch's own comment,
        # near the top of this file): a fact about which board a build is
        # modeling, not something exclusive to the NES target itself.
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(platform-nes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

        # metatiles.cpp #embeds the demo/chr art; declare it as a dependency so
        # editing the art retriggers a recompile.
        file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
                PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        target_link_libraries(demo PRIVATE platform-nes nds9)

        # CHR art is pulled in with #embed (C23); keep the Clang-only silencer for
        # parity with the other branches (no-op under devkitARM's GCC).
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

        # Wrap the linked ELF as a .nds ROM (the default ARM7 core + libnds icon
        # come from calico automatically). nds_create_rom() comes from devkitARM's
        # Platform/NintendoDS.cmake once the NDS toolchain is active. The output is
        # named demo.nds so it matches the other targets' demo.<ext> convention.
        nds_create_rom(demo_nds
                ARM9 demo
                NAME "platform-nes demo"
                SUBTITLE1 "NES engine demo on DS"
                SUBTITLE2 "github.com/brette"
                OUTPUT ${CMAKE_BINARY_DIR}/demo.nds
        )

    # ----------------------------------------------------------------------
    # Game Boy Advance target (devkitARM + libgba), via devkitPro's official
    # GBA.cmake toolchain (the gba-* presets point CMAKE_TOOLCHAIN_FILE at
    # $DEVKITPRO/cmake/GBA.cmake, exactly like the DS uses NDS.cmake).
    #
    # Same family as the DS: the GBA does NOT run the software rasteriser
    # (emu::GenerateFrame). It is a 2D tile console whose hardware is a near-cousin
    # of the NES PPU, so this backend maps the NES PPU onto the GBA's NATIVE 2D
    # hardware (Model B):
    #   * CHR pattern tables  -> GBA character (tile) VRAM (2bpp -> 4bpp expand)
    #   * nametable+attribute -> GBA BG tilemap entries (tile + palette + flip)
    #   * NES master palette  -> GBA BGR555 palette RAM
    #   * OAM shadow          -> GBA hardware OBJ (OAM written directly)
    #   * raster band timeline (emu::GenerateBands) -> a per-scanline HBlank scroll
    #     table, so mid-frame scroll changes and the sprite-0 split still work.
    # It therefore still pulls in src/emu for the platform-agnostic PPU bookkeeping
    # (nametable/OAM/palette writes, the band/IRQ timeline, the OAM shadow), but
    # the per-pixel rasteriser there goes unused.
    #
    # The 240x160 panel is the first target NARROWER than the NES (30 vs 32 tiles)
    # as well as shorter (20 vs 30); the backend shows a window onto the unaltered
    # 256x240 world (the engine never compromises the NES game for the GBA). Audio:
    # the GBA's tiny RAM has no room for baked PCM, so audio is stubbed
    # (src/gba/audio.cpp) exactly like the DS/GameCube/Wii backends -- the user
    # supplies a synthesiser. Only the tiny CHR art (a few KiB) is embedded.
    #
    # The toolchain ($DEVKITPRO/cmake/GBA.cmake) is supplied via the gba-* presets;
    # it imports devkitARM.cmake and routes CMAKE_SYSTEM_NAME=NintendoGBA to
    # devkitPro's Platform/NintendoGBA.cmake module, which sets the armv4t flags,
    # links libgba and provides the gba_create_rom() helper used below (gba.specs
    # crt0 + cart link script are applied by gba_create_rom itself).
    # ----------------------------------------------------------------------
    elseif(TARGET_PLATFORM STREQUAL "gba")
        project(platform-nes CXX)

        FetchContent_MakeAvailable(intsh tuple)

        # The GBA backend maps the shared PPU bookkeeping (src/emu) onto libgba 2D
        # hardware: the emulator and all the nametable/OAM/palette write functions
        # are platform-agnostic and compiled here alongside the libgba video/
        # present, the keypad input, and the (stub) audio.
        file(GLOB GBA_SOURCES CONFIGURE_DEPENDS src/gba/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        add_library(platform-nes STATIC ${GBA_SOURCES})
        target_include_directories(platform-nes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        # TARGET_GBA is the group the engine headers gate on (native 2D hardware,
        # 30x20 viewport -- the first target narrower than the NES, no SDL headers).
        target_compile_definitions(platform-nes PUBLIC TARGET_GBA)
        # Alternative nametable wiring (see the TARGET_NES branch's own comment,
        # near the top of this file): a fact about which board a build is
        # modeling, not something exclusive to the NES target itself.
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(platform-nes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)

        # metatiles.cpp #embeds the demo/chr art; declare it as a dependency so
        # editing the art retriggers a recompile.
        file(GLOB DEMO_CHR_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/*)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/metatiles.cpp
                PROPERTIES OBJECT_DEPENDS "${DEMO_CHR_FILES}")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        # libgba (-lgba) + gba.specs come in via the platform module's standard
        # libraries/linker flags, so only the engine archive is linked explicitly.
        target_link_libraries(demo PRIVATE platform-nes)

        # CHR art is pulled in with #embed (C23); keep the Clang-only silencer for
        # parity with the other branches (no-op under devkitARM's GCC).
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

        # Wrap the linked ELF as a header-fixed .gba ROM. devkitPro's
        # gba_create_rom() operates on the executable target directly: it adds the
        # cart link options (-specs=gba.specs) to `demo`, then POST_BUILD runs
        # objcopy -O binary + gbafix (Nintendo logo checksum/complement + the
        # title/game/maker codes). Output is named demo.gba to match demo.<ext>.
        gba_create_rom(demo
                OUTPUT ${CMAKE_BINARY_DIR}/demo.gba
                TITLE "PLATFORMNES"
                GAMECODE "CPNE"
                MAKERCODE "00"
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

    # Alternative nametable wiring (see the TARGET_NES branch's own comment,
    # above): this is a fact about which board a build is modeling, not
    # something exclusive to the NES target itself, so the desktop preview
    # needs the same define to route mmc3.cpp's strong ppu::ReadNametable/
    # WriteNametable overrides correctly off-NES too -- otherwise
    # ALTERNATIVE_NAMETABLE reads as 0 in every #if here regardless of what
    # local.cmake actually sets.
    if(DEFINED ALTERNATIVE_NAMETABLE)
        list(APPEND PLATFORM_DEFINE "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()

    # ------------------------------------------------------------------
    # Fetch SDL3 source and build it statically alongside the project.
    # ------------------------------------------------------------------
    set(SDL3_GIT_TAG "release-3.2.16" CACHE STRING "SDL3 git tag to fetch")

    include(FetchContent)

    # ------------------------------------------------------------------
    # On Linux, link against the system's libusb-1.0 (LGPL-2.1) instead
    # of vendoring and building our own copy. Policy: LGPL dependencies
    # must always be linked dynamically, against the distro-provided
    # library, never bundled/embedded as source or a static archive —
    # this keeps us out of the business of redistributing (and relicensing
    # obligations around) libusb ourselves, and lets users satisfy the
    # LGPL's relink requirement by swapping their system package.
    #
    # NOTE: requires libusb-1.0 + its pkg-config file installed on the
    # host (libusb-1.0-0-dev on Debian/Ubuntu, libusbx-devel on Fedora/RHEL).
    # ------------------------------------------------------------------
    if(TARGET_PLATFORM STREQUAL "linux")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LibUSB REQUIRED IMPORTED_TARGET libusb-1.0)

        # --------------------------------------------------------------
        # Wire the system libusb into SDL3's own detection.
        #
        # SDL3 calls find_package(LibUSB), which invokes its bundled
        # cmake/FindLibUSB.cmake. Pre-creating the LibUSB::LibUSB target
        # it would otherwise define (as an alias of the PkgConfig-derived
        # target above) satisfies its `if(LibUSB_FOUND AND NOT TARGET
        # LibUSB::LibUSB)` guard, so it adopts the same system library
        # instead of running its own find_path/find_library scan.
        # --------------------------------------------------------------
        if(NOT TARGET LibUSB::LibUSB)
            add_library(LibUSB::LibUSB ALIAS PkgConfig::LibUSB)
        endif()

        set(SDL_HIDAPI_LIBUSB_SHARED ON CACHE BOOL "" FORCE)

        # Disable SDL3's libusb-based HIDAPI backend. Its configure-time
        # check_c_source_compiles test requires LibUSB::LibUSB to resolve
        # inside an isolated try_compile scratch project, which ALIAS
        # targets (our hook above) don't survive. The project links
        # libusb directly for its own NES-over-USB needs, and SDL3
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
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER_GPU OFF CACHE BOOL "" FORCE)

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
    file(GLOB SDL3_SOURCES CONFIGURE_DEPENDS src/SDL3/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
    add_library(platform-nes STATIC ${SDL3_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})
    target_link_libraries(platform-nes PUBLIC SDL3::SDL3-static)
    target_link_libraries(platform-nes PUBLIC br0::intsh br0::tuple)

    if(TARGET_PLATFORM STREQUAL "linux")
        target_link_libraries(platform-nes PUBLIC PkgConfig::LibUSB)
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
            # seals the bundle. libusb (LGPL-2.1) is deliberately excluded
            # here: it's linked dynamically against the host's system
            # package rather than vendored, so we hold no local copy of its
            # license text to copy — the bundled libusb-1.0.so.0 itself
            # still carries redistribution obligations, but they fall on
            # whoever built that system package, not on this build.
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