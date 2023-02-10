# [CONMAN_BUILD_DIR] is set in "01-directories.sh".

# Set paths to executables.
#
CONMAN="${CONMAN_BUILD_DIR}/conman"
CONMAND="${CONMAN_BUILD_DIR}/conmand"

# Require executables to be built before tests can proceed.
#
set_executables()
{
    local prog
    for prog in "${CONMAN}" "${CONMAND}"; do
        if test ! -x "${prog}"; then
            echo "ERROR: ConMan has not been built: ${prog} not found."
            exit 1
        fi
    done
}

set_executables
