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
message(STATUS "OGX-Mini-improved version: ${OGXM_VERSION}")
