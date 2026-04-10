cmake_minimum_required(VERSION 3.18)

set(TARGET_PLATFORM "" CACHE STRING "Target platform: nes, win, mac, linux")

# ============================================================
# NES Target
# ============================================================
if(TARGET_PLATFORM STREQUAL "nes")
    set(LLVM_MOS_PLATFORM nes-nrom)
    find_package(llvm-mos-sdk REQUIRED)
    project(platform-nes C)
    set(PLATFORM_DIR "nes")
    set(PLATFORM_DEFINE TARGET_NES)

    # ---- FamiStudio Sound Engine (Download raw binary tables) ----
    set(FAMISTUDIO_DIR ${CMAKE_BINARY_DIR}/famistudio)
    set(FAMISTUDIO_BASE_URL "https://raw.githubusercontent.com/BleuBleu/FamiStudio/master/SoundEngine")

    if(NOT EXISTS ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(MAKE_DIRECTORY ${FAMISTUDIO_DIR}/NoteTables)

        # Download the driver files
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.s ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.h ${FAMISTUDIO_DIR}/famistudio_ca65.h)

        # Download the RAW binary tables specifically
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_lsb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_lsb.bin)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_msb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_msb.bin)
    endif()

    # ---- Find tools ----
    find_program(CA65 ca65 HINTS "C:/cc65/bin" REQUIRED)
    get_filename_component(LLVM_MOS_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(LLVM_READELF llvm-readelf HINTS "${LLVM_MOS_BIN}" REQUIRED)

    # ---- Shared FamiStudio config (minus padding — that gets prepended) ----
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

    # ---- Copy FamiStudio headers ----
    file(COPY ${FAMISTUDIO_DIR}/famistudio_ca65.h
            DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    if(EXISTS ${FAMISTUDIO_DIR}/famistudio_llvmmos.h)
        file(COPY ${FAMISTUDIO_DIR}/famistudio_llvmmos.h
                DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    endif()

    # ============================================================
    # PASS 1: Assemble ca65 without padding, link to measure
    # ============================================================

    # Pass 1 FamiStudio config — no padding
    file(WRITE ${CMAKE_BINARY_DIR}/famistudio_config_pass1.s "${FAMISTUDIO_CFG}")

    # Pass 1: assemble FamiStudio
    set(FAMISTUDIO_PASS1_OBJ ${CMAKE_BINARY_DIR}/famistudio_pass1.o)
    add_custom_command(
            OUTPUT ${FAMISTUDIO_PASS1_OBJ}
            COMMAND ${CA65}
            -I ${FAMISTUDIO_DIR}
            -I ${FAMISTUDIO_DIR}/NoteTables
            -o ${FAMISTUDIO_PASS1_OBJ}
            ${CMAKE_BINARY_DIR}/famistudio_config_pass1.s
            DEPENDS ${CMAKE_BINARY_DIR}/famistudio_config_pass1.s
            COMMENT "Pass 1: Assembling FamiStudio (no padding)"
    )

    # Assemble all user .s files (only done once — they don't change)
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

    # NES library (compiled once, used by both passes)
    file(GLOB NES_SOURCES src/nes/*.c)
    add_library(platform-nes STATIC ${NES_SOURCES})
    target_include_directories(platform-nes PUBLIC
            include/
            ${CMAKE_BINARY_DIR}/famistudio_include
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})

    # Collect demo C sources
    file(GLOB DEMO_C_SOURCES ${CMAKE_SOURCE_DIR}/demo/*.c)

    # Pass 1: link everything to produce a measurable ELF
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
            COMMENT "Pass 1: Linking for ZP/BSS measurement"
    )

    # ============================================================
    # MEASUREMENT: Extract ZP/BSS sizes, generate padding
    # ============================================================

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
            COMMENT "Measuring ZP/BSS usage from pass 1 ELF"
    )

    # ============================================================
    # PASS 2: Reassemble FamiStudio with padding, final link
    # ============================================================

    # Pass 2 FamiStudio config — includes padding before engine
    file(WRITE ${CMAKE_BINARY_DIR}/famistudio_config.s
            ".include \"ca65_padding.s\"
${FAMISTUDIO_CFG}")

    # Pass 2: assemble FamiStudio with padding
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
            COMMENT "Pass 2: Assembling FamiStudio (with padding)"
    )

    # Final link
    add_executable(demo ${DEMO_C_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".nes")
    target_link_libraries(demo PRIVATE platform-nes)
    add_dependencies(demo demo_pass2_asm)

    # Custom target to pull the dependency chain together
    add_custom_target(demo_pass2_asm
            DEPENDS ${FAMISTUDIO_OBJ} ${DEMO_ASM_OBJECTS}
    )

    target_link_options(demo PRIVATE
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
    )

    # ============================================================
    # Desktop Targets (win, mac, linux)
    # ============================================================
else()
    project(platform-nes C)
    find_package(SDL3 REQUIRED)

    if(TARGET_PLATFORM STREQUAL "mac")
        set(PLATFORM_DIR "macos")
        set(PLATFORM_DEFINE TARGET_MACOS)
    elseif(TARGET_PLATFORM STREQUAL "win")
        set(PLATFORM_DIR "windows")
        set(PLATFORM_DEFINE TARGET_WINDOWS)
    elseif(TARGET_PLATFORM STREQUAL "linux")
        set(PLATFORM_DIR "linux")
        set(PLATFORM_DEFINE TARGET_LINUX)
    else()
        message(FATAL_ERROR "Set -DTARGET_PLATFORM to one of: nes, win, mac, linux")
    endif()

    # ---- Desktop library ----
    file(GLOB UNIFIED_SOURCES src/unified/*.c)
    file(GLOB PLATFORM_SOURCES src/${PLATFORM_DIR}/*.c)
    add_library(platform-nes STATIC ${UNIFIED_SOURCES} ${PLATFORM_SOURCES})
    target_include_directories(platform-nes PUBLIC include/)
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})
    target_link_libraries(platform-nes PRIVATE SDL3::SDL3)

    # ---- Desktop demo ----
    file(GLOB DEMO_SOURCES demo/*.c)
    add_executable(demo ${DEMO_SOURCES})
    target_link_libraries(demo PRIVATE platform-nes)

    # Copy tracks folder next to binary
    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/demo/tracks
            $<TARGET_FILE_DIR:demo>/tracks
    )

    # Copy sfx folder next to binary
    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/demo/sfx
            $<TARGET_FILE_DIR:demo>/sfx
    )

    # Copy SDL3.dll next to binary (Windows)
    if(TARGET_PLATFORM STREQUAL "win")
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:demo>
        )
    endif()
endif()