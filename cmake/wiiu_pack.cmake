# wiiu_pack.cmake -- build-time packer for the optional installable Wii U title.
#
# Invoked by the `demo-wup` target (see the wiiu branch of CMakeLists.txt) via
#   cmake -DWUP_DIR=... -DWUP_OUT=... -DNUSPACKER_JAR=... -DCOMMON_KEY=...
#         -DTITLE_ID=... -P cmake/wiiu_pack.cmake
#
# By this point CMake has already assembled the unpacked title tree under
# WUP_DIR (code/ with app.xml + cos.xml + the rpx, meta/ with meta.xml + the
# TGAs, content/ with a placeholder). This script's only job is the final,
# secret-dependent step: run NUSPacker to encrypt that tree into an installable
# WUP -- and to GRACEFULLY SKIP (leaving the usable tree behind) when the common
# key or the jar is missing, so the homebrew/key-free flow never breaks.
#
# COMMON_KEY may be the 32-hex key itself OR a path to a file containing it (so a
# CI secret can be mounted as a file); a file is read and the first 32 hex chars
# are used. The key is NEVER printed.
#
# This script runs as a FRESH cmake process (via -P) that inherits the build
# environment, so it reads WIIU_COMMON_KEY / WIIU_NUSPACKER_JAR from the live
# env when the -D values are empty. That deliberately bypasses the configure-time
# cache: a CI runner can export the secret and just rebuild `demo-wup` without
# reconfiguring (the CACHE STRING captured at configure would otherwise be stale).

# Resolve the common key: -D override first, then the live environment.
set(_key "${COMMON_KEY}")
if(NOT _key)
    set(_key "$ENV{WIIU_COMMON_KEY}")
endif()
# Likewise resolve the jar from the env if the -D value was empty.
if(NOT NUSPACKER_JAR)
    set(NUSPACKER_JAR "$ENV{WIIU_NUSPACKER_JAR}")
endif()
if(_key AND EXISTS "${_key}")
    file(READ "${_key}" _key)
endif()
# Keep only hex characters (strips whitespace / a trailing newline / comments).
string(REGEX MATCH "[0-9A-Fa-f]+" _key "${_key}")

if(NOT _key OR NOT NUSPACKER_JAR OR NOT EXISTS "${NUSPACKER_JAR}")
    message(STATUS "")
    message(STATUS "demo-wup: unpacked title tree is ready at:")
    message(STATUS "    ${WUP_DIR}   (code/ content/ meta/)")
    if(NOT _key)
        message(STATUS "demo-wup: WIIU_COMMON_KEY not set -- skipping WUP encryption.")
        message(STATUS "          Pack it yourself with NUSPacker, or set WIIU_COMMON_KEY")
        message(STATUS "          (32-hex string or file path) and rebuild demo-wup.")
    endif()
    if(NOT NUSPACKER_JAR OR NOT EXISTS "${NUSPACKER_JAR}")
        message(STATUS "demo-wup: NUSPacker.jar not found -- set WIIU_NUSPACKER_JAR.")
    endif()
    message(STATUS "")
    return()
endif()

string(LENGTH "${_key}" _keylen)
if(NOT _keylen EQUAL 32)
    message(FATAL_ERROR "demo-wup: WIIU_COMMON_KEY must be 32 hex characters (got ${_keylen})")
endif()

find_program(_java NAMES java)
if(NOT _java)
    message(FATAL_ERROR "demo-wup: java not found on PATH (needed to run NUSPacker)")
endif()

file(MAKE_DIRECTORY "${WUP_OUT}")

# NUSPacker parses code/app.xml for the title id; pass it explicitly too so the
# output is unambiguous. -encryptKeyWith takes the Wii U common key.
message(STATUS "demo-wup: packing installable WUP -> ${WUP_OUT}")
execute_process(
        COMMAND "${_java}" -jar "${NUSPACKER_JAR}"
                -in  "${WUP_DIR}"
                -out "${WUP_OUT}"
                -encryptKeyWith "${_key}"
        RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "demo-wup: NUSPacker failed (exit ${_rc})")
endif()
message(STATUS "demo-wup: WUP written to ${WUP_OUT}")
