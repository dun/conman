# Search from directory [start_dir] up to the root directory, looking for a
#   directory that contains [target_file].
# Output the resulting directory if a match is found.
#
search_dirs()
{
    local start_dir=$1
    local target_file=$2
    (
        cd "${start_dir}" &&
        while test "$(pwd)" != "/"; do
            test -e "${target_file}" && pwd && break
            cd ..
        done
    )
}

# Set the top-level build directory (which should contain "config.status" once
#   "./configure" has been run).
# Provide [CONMAN_BUILD_DIR].
#
set_build_dir()
{
    CONMAN_BUILD_DIR=$(search_dirs "." "config.status")
    if test -z "${CONMAN_BUILD_DIR}"; then
        echo "ERROR: Failed to set CONMAN_BUILD_DIR: not configured"
        exit 1
    fi
}

# Set the top-level source directory (which should contain "configure.ac").
# Provide [CONMAN_SOURCE_DIR].
#
set_source_dir()
{
    CONMAN_SOURCE_DIR=$(search_dirs \
            $(dirname "${SHARNESS_TEST_FILE}") "configure.ac")
    if test -z "${CONMAN_SOURCE_DIR}"; then
        echo "ERROR: Failed to set CONMAN_SOURCE_DIR: cannot locate source"
        exit 1
    fi
}

set_build_dir
set_source_dir
