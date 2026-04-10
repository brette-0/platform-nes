cmake_minimum_required(VERSION 3.20)
add_compile_definitions(LANDSCAPE)
# ----------------------------------------------------------------------
# NES target
# ----------------------------------------------------------------------
if(TARGET_PLATFORM STREQUAL "nes")
    set(LLVM_MOS_PLATFORM nes-nrom)
    find_package(llvm-mos-sdk REQUIRED)
    project(platform-nes C)
    set(PLATFORM_DIR "nes")
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
            COMMENT "Assembling FamiStudio (pass 1, no padding)"
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
            -Os -DTARGET_NES
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

    # Custom linker scripts are only used by the desktop targets.
    if(NOT TARGET_PLATFORM STREQUAL "nes")
        target_link_options(demo PRIVATE
                -T ${CMAKE_SOURCE_DIR}/scripts/base.ld
                -T ${CMAKE_SOURCE_DIR}/scripts/chr_rom.ld
        )
    endif()

    # ----------------------------------------------------------------------
    # Desktop targets (macOS, Windows, Linux)
    # ----------------------------------------------------------------------
else()
    project(platform-nes C)
    find_package(SDL3 REQUIRED)

    if(TARGET_PLATFORM STREQUAL "mac")
        set(PLATFORM_DIR "macos")
        set(PLATFORM_DEFINE TARGET_MACOS)
    elseif(TARGET_PLATFORM STREQUAL "win")
        set(PLATFORM_DEFINE TARGET_WINDOWS)
        set(PLATFORM_DIR "windows")
    elseif(TARGET_PLATFORM STREQUAL "linux")
        set(PLATFORM_DEFINE TARGET_LINUX)
        set(PLATFORM_DIR "linux")
    else()
        message(FATAL_ERROR "Set -DTARGET_PLATFORM to one of: nes, win, mac, linux")
    endif()

    file(GLOB UNIFIED_SOURCES src/unified/*.c)
    file(GLOB PLATFORM_SOURCES src/${PLATFORM_DIR}/*.c)

    add_library(platform-nes STATIC ${UNIFIED_SOURCES} ${PLATFORM_SOURCES})
    target_include_directories(platform-nes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})
    target_link_libraries(platform-nes PRIVATE SDL3::SDL3)

    file(GLOB DEMO_SOURCES demo/*.c)
    add_executable(demo ${DEMO_SOURCES})
    target_link_libraries(demo PRIVATE platform-nes)

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

    if(TARGET_PLATFORM STREQUAL "win")
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:demo>
        )
    endif()
endif()