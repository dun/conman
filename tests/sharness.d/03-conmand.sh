# Set up the environment for running conmand.
# Create a config with 2 test consoles that output 1 byte every 10ms.
# Relocate the config and logfiles to [TMPDIR] if [root] has not been set since
#   the sharness trash directory (which defaults to the cwd) may reside in NFS
#   which can cause problems with advisory lockfiles.  It is not necessary to
#   relocate the pidfile, but not doing so could make it lonely since
#   everything else is potentially moved.
# Provide [CONMAND_CONFIG], [CONMAND_LOGFILE], [CONMAND_PIDFILE], and
#   [CONMAND_CONSOLE_GLOB].
#
conmand_setup()
{
    local prefix

    if test "x${root}" = x; then
        prefix="${TMPDIR:-"/tmp"}/"
    fi

    CONMAND_CONFIG="${prefix}conmand.conf.$$"
    test_debug "echo CONMAND_CONFIG=\"${CONMAND_CONFIG}\""
    CONMAND_LOGFILE="${prefix}conmand.log.$$"
    test_debug "echo CONMAND_LOGFILE=\"${CONMAND_LOGFILE}\""
    CONMAND_PIDFILE="${prefix}conmand.pid.$$"
    test_debug "echo CONMAND_PIDFILE=\"${CONMAND_PIDFILE}\""
    CONMAND_CONSOLE_GLOB="${prefix}console.*.log.$$"
    test_debug "echo CONMAND_CONSOLE_GLOB=\"${CONMAND_CONSOLE_GLOB}\""

    cat > "${CONMAND_CONFIG}" <<-EOF
	server logfile="${CONMAND_LOGFILE}"
	server pidfile="${CONMAND_PIDFILE}"
	server loopback=on
	server port=0
	global log="${prefix}console.%N.log.$$"
	global testopts="b:1,m:10,n:10,p:100"
	console name="test1" dev="test:"
	console name="test2" dev="test:"
	EOF
}

# Start the daemon process after ensuring the previous daemon process has
#   exited.
# Provide [CONMAND_PORT].
#
conmand_start()
{
    conmand_kill
    test_debug "echo \"${CONMAND}\" -c \"${CONMAND_CONFIG}\" $*"
    "${CONMAND}" -c "${CONMAND_CONFIG}" "$@"
    cat "${CONMAND_LOGFILE}"
    CONMAND_PORT=$(sed -n -e '/Listening/ s/.*port \([0-9]*\)/\1/p' \
            < "${CONMAND_LOGFILE}")
    test_debug "echo CONMAND_PORT=\"${CONMAND_PORT}\""
    test "x${CONMAND_PORT}" != x
}

# Stop the daemon process.
# The for-loop is necessary since conmand returns immediately after sending a
#   SIGTERM to the running daemon process and before that target process has
#   likely terminated.  This loop polls the process listing for up to ~5secs
#   waiting for the conmand process to exit.
#
conmand_stop()
{
    local pid i
    pid=$(cat "${CONMAND_PIDFILE}")
    test_debug "echo \"${CONMAND}\" -c \"${CONMAND_CONFIG}\" -k $*"
    "${CONMAND}" -c "${CONMAND_CONFIG}" -k "$@"
    test "x${pid}" != x || return 1
    for i in $(test_seq 1 50); do
        sleep 0.1
        ps -p "${pid}" -ww | grep conmand >/dev/null 2>&1 || return 0
    done
    return 1
}

# Kill an errant conmand process from a previous test that is still running in
#   the background.  This situation is most likely to occur if a test starting
#   conmand is expected to fail and instead erroneously succeeds.
# Only check for the pid named in [CONMAND_PIDFILE] to avoid interfering with
#   conmand processes belonging to other tests or system use.  And check that
#   the named pid is a conmand process and not one recycled by the system for
#   some other running process.
# A SIGKILL is sent instead of SIGTERM in case the signal handler has a bug
#   preventing graceful termination.  Since SIGKILL prevents the process from
#   cleaning up after itself, that cleanup must be performed here afterwards.
#
conmand_kill()
{
    local pid
    pid=$(cat "${CONMAND_PIDFILE}" 2>/dev/null)
    if test "x${pid}" != x; then
        if ps -p "${pid}" -ww | grep conmand; then
            kill -9 "${pid}"
            echo "WARNING: Killed errant conmand pid ${pid}"
        else
            echo "WARNING: Found stale pidfile for conmand pid ${pid}"
        fi
        rm -f "${CONMAND_PIDFILE}"
    fi
}

# Perform housekeeping to clean up after conmand.
# This should be called at the end of any test script that starts a conmand
#   process.  It must be at the start of any &&-chain to ensure it cannot be
#   prevented from running by a preceding failure in the chain.
# Remove files outside the sharness trash directory unless [debug] is set.
#
conmand_cleanup()
{
    conmand_kill
    if test "x${root}" = x && test "x${debug}" != xt; then
        rm -f "${CONMAND_CONFIG}" "${CONMAND_LOGFILE}" "${CONMAND_PIDFILE}" \
                ${CONMAND_CONSOLE_GLOB}
    fi
}
