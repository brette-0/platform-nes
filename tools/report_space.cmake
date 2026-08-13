# report_space.cmake
# Runs at build time via: cmake -P report_space.cmake
#   -DREADELF=<path to llvm-readelf>
#   -DNM=<path to llvm-nm>
#   -DELF_FILE=<path to the final linked ELF>
#
# Prints how full every memory region is after a link: capacity, used, free.
# Purely informational -- nothing consumes its output, and it never fails a
# build (a region that genuinely overflows is already a hard linker error long
# before this runs).
#
# WHY REGIONS, NOT SECTIONS: "free space in a section" isn't a quantity -- an
# output section is exactly as large as the content in it, never larger. The
# number that actually matters is how much of a REGION's fixed capacity is
# still unspent: room left in the switchable PRG window, in each 8 KiB bank,
# in the fixed bank, in zero page. So every allocated section is summed into
# whichever region's address range it falls in.
#
# DRIVEN ENTIRELY BY THE LINKER, not by layout knowledge restated here. The
# scripts publish __space_origin_<region> / __space_length_<region> pairs for
# every region they declare (see mmc3-helper.ld's own __space_* block), and
# this file discovers regions purely by finding those pairs in the symbol
# table. Two consequences worth the coupling:
#
#   - An EMPTY region still gets reported. A region holding nothing produces
#     no output section at all, so a linked ELF on its own cannot even tell
#     you an unused 8 KiB bank exists -- these symbols are the only reason
#     "bank 2: 0 / 8192 used" can be printed.
#   - Changing a region's size, or adding a new one, needs no edit here.

cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED READELF OR NOT DEFINED NM OR NOT DEFINED ELF_FILE)
    message(FATAL_ERROR
            "Usage: cmake -P report_space.cmake "
            "-DREADELF=<path> -DNM=<path> -DELF_FILE=<path>")
endif()

execute_process(COMMAND "${NM}" "${ELF_FILE}"
        OUTPUT_VARIABLE NM_OUT ERROR_QUIET RESULT_VARIABLE NM_RC
        OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${READELF}" -S "${ELF_FILE}"
        OUTPUT_VARIABLE SECTIONS ERROR_QUIET RESULT_VARIABLE RE_RC
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT NM_RC EQUAL 0 OR NOT RE_RC EQUAL 0)
    message(STATUS "space: could not read ${ELF_FILE}, skipping region report")
    return()
endif()

# ---- Discover regions from __space_origin_* / __space_length_* ------------
set(RNAMES "")
set(RBASES "")
set(RCAPS  "")
string(REPLACE "\n" ";" NM_LINES "${NM_OUT}")
foreach(LINE ${NM_LINES})
    if(LINE MATCHES "^([0-9a-fA-F]+) [AaNn] __space_origin_(.+)$")
        math(EXPR BASE "0x${CMAKE_MATCH_1}")
        set(RNAME "${CMAKE_MATCH_2}")
        set(CAP "")
        foreach(L2 ${NM_LINES})
            if(L2 MATCHES "^([0-9a-fA-F]+) [AaNn] __space_length_${RNAME}$")
                math(EXPR CAP "0x${CMAKE_MATCH_1}")
                break()
            endif()
        endforeach()
        if(NOT CAP STREQUAL "")
            list(APPEND RNAMES "${RNAME}")
            list(APPEND RBASES ${BASE})
            list(APPEND RCAPS  ${CAP})
            list(APPEND RUSED  0)
        endif()
    endif()
endforeach()

list(LENGTH RNAMES NREGIONS)
if(NREGIONS EQUAL 0)
    message(STATUS "space: no __space_* symbols found, skipping region report")
    return()
endif()

# ---- Sum each allocated section into its region ---------------------------
# FSCODE and friends have no leading dot, so the name pattern can't require
# one. Zero-size sections are skipped: they contribute nothing and llvm-mos
# emits a lot of them (.rom_poke_table_0..31).
string(REPLACE "\n" ";" LINES "${SECTIONS}")
foreach(LINE ${LINES})
    if(NOT LINE MATCHES "\\[ *[0-9]+\\] +([^ ]+) +(PROGBITS|NOBITS) +([0-9a-fA-F]+) +[0-9a-fA-F]+ +([0-9a-fA-F]+)")
        continue()
    endif()
    math(EXPR SADDR "0x${CMAKE_MATCH_3}")
    math(EXPR SSIZE "0x${CMAKE_MATCH_4}")
    if(SSIZE EQUAL 0)
        continue()
    endif()

    set(I 0)
    while(I LESS NREGIONS)
        list(GET RBASES ${I} BASE)
        list(GET RCAPS  ${I} CAP)
        math(EXPR REND "${BASE} + ${CAP}")
        if(SADDR GREATER_EQUAL BASE AND SADDR LESS REND)
            list(GET RUSED ${I} PREV)
            math(EXPR NEW "${PREV} + ${SSIZE}")
            list(REMOVE_AT RUSED ${I})
            list(INSERT RUSED ${I} ${NEW})
            break()
        endif()
        math(EXPR I "${I} + 1")
    endwhile()
endforeach()

# ---- Report ---------------------------------------------------------------
message(STATUS "Region space (used / capacity):")

set(I 0)
while(I LESS NREGIONS)
    list(GET RNAMES ${I} NAME)
    list(GET RCAPS  ${I} CAP)
    list(GET RUSED  ${I} USED)
    if(CAP EQUAL 0)
        message(STATUS "  ${NAME}: (empty region, 0 bytes)")
    else()
        math(EXPR FREE "${CAP} - ${USED}")
        math(EXPR PCT  "(${USED} * 100) / ${CAP}")
        math(EXPR BARS "(${USED} * 20) / ${CAP}")
        set(BAR "")
        foreach(N RANGE 1 20)
            if(N LESS_EQUAL BARS)
                string(APPEND BAR "#")
            else()
                string(APPEND BAR ".")
            endif()
        endforeach()
        string(LENGTH "${NAME}" L)
        set(PAD "")
        while(L LESS 20)
            string(APPEND PAD " ")
            math(EXPR L "${L} + 1")
        endwhile()
        message(STATUS "  ${NAME}${PAD} [${BAR}] ${USED} / ${CAP}, ${FREE} free (${PCT}%)")
    endif()
    math(EXPR I "${I} + 1")
endwhile()
