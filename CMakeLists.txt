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

    # ---- FamiStudio Sound Engine (download only what we need) ----
    set(FAMISTUDIO_DIR ${CMAKE_BINARY_DIR}/famistudio)
    set(FAMISTUDIO_BASE_URL "https://raw.githubusercontent.com/BleuBleu/FamiStudio/master/SoundEngine")

    if(NOT EXISTS ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(MAKE_DIRECTORY ${FAMISTUDIO_DIR}/NoteTables)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.s
                ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.h
                ${FAMISTUDIO_DIR}/famistudio_ca65.h)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/FamiStudioNoteLookup.s
                ${FAMISTUDIO_DIR}/NoteTables/FamiStudioNoteLookup.s)
    endif()

    # ---- FamiStudio config ----
    file(WRITE ${CMAKE_BINARY_DIR}/famistudio_config.s
            ".define FAMISTUDIO_CA65_ZP_SEGMENT   ZEROPAGE
.define FAMISTUDIO_CA65_RAM_SEGMENT  BSS
.define FAMISTUDIO_CA65_CODE_SEGMENT CODE

FAMISTUDIO_CFG_EXTERNAL       = 1
FAMISTUDIO_CFG_NTSC_SUPPORT   = 1
FAMISTUDIO_CFG_PAL_SUPPORT    = 0
FAMISTUDIO_CFG_DPCM_SUPPORT   = 1
FAMISTUDIO_CFG_SFX_SUPPORT    = 1
FAMISTUDIO_CFG_SFX_STREAMS    = 2
FAMISTUDIO_CFG_C_BINDINGS     = 1
FAMISTUDIO_CFG_SMOOTH_VIBRATO = 1

.include \"famistudio_ca65.s\"
")

    # ---- Find ca65 ----
    find_program(CA65 ca65 HINTS "C:/cc65/bin" REQUIRED)

    # ---- Assemble FamiStudio engine ----
    add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/famistudio.o
            COMMAND ${CA65}
            -I ${FAMISTUDIO_DIR}
            -I ${FAMISTUDIO_DIR}/NoteTables
            -o ${CMAKE_BINARY_DIR}/famistudio.o
            ${CMAKE_BINARY_DIR}/famistudio_config.s
            DEPENDS ${CMAKE_BINARY_DIR}/famistudio_config.s
            COMMENT "Assembling FamiStudio sound engine with ca65"
    )
    add_custom_target(famistudio_asm DEPENDS ${CMAKE_BINARY_DIR}/famistudio.o)

    # ---- Copy FamiStudio headers ----
    file(COPY ${FAMISTUDIO_DIR}/famistudio_ca65.h
            DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    if(EXISTS ${FAMISTUDIO_DIR}/famistudio_llvmmos.h)
        file(COPY ${FAMISTUDIO_DIR}/famistudio_llvmmos.h
                DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    endif()

    # ---- NES library ----
    file(GLOB NES_SOURCES src/nes/*.c)
    add_library(platform-nes STATIC ${NES_SOURCES})
    target_include_directories(platform-nes PUBLIC
            include/
            ${CMAKE_BINARY_DIR}/famistudio_include
    )
    target_compile_definitions(platform-nes PUBLIC ${PLATFORM_DEFINE})

    # ---- Assemble all .s files in demo/ ----
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
    add_custom_target(demo_asm DEPENDS ${DEMO_ASM_OBJECTS})

    # ---- NES demo ----
    file(GLOB DEMO_C_SOURCES ${CMAKE_SOURCE_DIR}/demo/*.c)
    add_executable(demo ${DEMO_C_SOURCES})
    target_link_libraries(demo PRIVATE platform-nes)
    add_dependencies(demo famistudio_asm demo_asm)
    target_link_options(demo PRIVATE
            ${CMAKE_BINARY_DIR}/famistudio.o
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

    # Copy SDL3.dll next to binary (Windows)
    if(TARGET_PLATFORM STREQUAL "win")
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:demo>
        )
    endif()
endif()