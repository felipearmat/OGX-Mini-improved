set(FW_NAME "OGX-Mini")
set(FW_VERSION "v1.0.0.13a")

# OGX-Mini-improved version (the web app compares it with the latest release): the release tag
# (-DOGXM_VERSION=improved-vX.Y.Z, set by the release workflow), else `git describe` against the
# improved-v* tags (improved-v1.1.0-14-ge91f18d = 14 commits after v1.1.0), else the commit.
if(NOT OGXM_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "improved-v*" --always
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            OUTPUT_VARIABLE OGXM_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
    if(NOT OGXM_VERSION)
        set(OGXM_VERSION "improved-unknown")
    elseif(NOT OGXM_VERSION MATCHES "^improved-")
        set(OGXM_VERSION "improved-dev-${OGXM_VERSION}")  # no tag reachable (shallow checkout)
    endif()
endif()

# Our own builds (the bench, play-testing): the options that help find problems on by default
# (the kept log saved to flash, pads disconnected before a mode-change reboot) and the version
# marked "-dev". Published builds leave it OFF; nothing else changes (same optimisation, same
# behaviour), so a dev build tests what users get.
option(OGXM_DEV_DEFAULTS "Our own builds: diagnostic options on by default, version marked -dev" OFF)
if(OGXM_DEV_DEFAULTS AND NOT OGXM_VERSION MATCHES "-dev")
    set(OGXM_VERSION "${OGXM_VERSION}-dev")
endif()

# The commit built (8 hex digits, "+" when the tree had changes), kept with a crash record so it
# can be matched with its .elf.
if(NOT OGXM_BUILD_ID)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            OUTPUT_VARIABLE OGXM_BUILD_ID
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} diff --quiet HEAD --
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            RESULT_VARIABLE OGXM_TREE_CHANGED
            ERROR_QUIET)
        if(OGXM_BUILD_ID AND OGXM_TREE_CHANGED)
            set(OGXM_BUILD_ID "${OGXM_BUILD_ID}+")
        endif()
    endif()
    if(NOT OGXM_BUILD_ID)
        set(OGXM_BUILD_ID "unknown")
    endif()
endif()
message(STATUS "OGX-Mini-improved version: ${OGXM_VERSION} (build ${OGXM_BUILD_ID})")
