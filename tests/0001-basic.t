#!/bin/sh

test_description="Check basic functionality"

: "${SHARNESS_TEST_SRCDIR:=$(cd "$(dirname "$0")" && pwd)}"
. "${SHARNESS_TEST_SRCDIR}/sharness.sh"

# Set up the environment.
#
test_expect_success 'setup' '
    conmand_setup
'

# Verify a configuration file has been created.
#
test_expect_success 'check config file creation' '
    ls -l "${CONMAND_CONFIG}" &&
    test -s "${CONMAND_CONFIG}"
'

# Start the daemon.
#
test_expect_success 'start conmand' '
    conmand_start
'

# Verify the pidfile has been created.
#
test_expect_success 'check pidfile creation' '
    ls -l "${CONMAND_PIDFILE}" &&
    test -s "${CONMAND_PIDFILE}"
'

# Verify the pid in the pidfile matches a running conmand process.
# Provide [PID] for later checks.
#
test_expect_success 'check process is running' '
    PID=$(cat "${CONMAND_PIDFILE}") &&
    ps -p "${PID}" -ww | grep conmand
'

# Verify the logfile has been created.
#
test_expect_success 'check logfile creation' '
    ls -l "${CONMAND_LOGFILE}" &&
    test -s "${CONMAND_LOGFILE}"
'

# Query the daemon via the client.
# Verify the expected number of consoles are configured.
#
test_expect_success 'check conman query' '
    "${CONMAN}" -d "127.0.0.1:${CONMAND_PORT}" -q >out.$$ &&
    test "$(wc -l <out.$$)" -eq 2
'

# Stop the daemon.
#
test_expect_success 'stop conmand' '
    conmand_stop -v
'

# Verify the daemon is no longer running.
#
test_expect_success 'check process has exited' '
    test "x${PID}" != x &&
    ! ps -p "${PID}" >/dev/null
'

# Verify the pidfile has been removed.
#
test_expect_success 'check pidfile removal' '
    test "x${CONMAND_PIDFILE}" != x &&
    test ! -f "${CONMAND_PIDFILE}"
'

# Check if the final log message for stopping the daemon has been written out.
#
test_expect_success 'check logfile for final message' '
    grep "Stopping" "${CONMAND_LOGFILE}"
'

# Check the logfile for errors.
#
test_expect_success 'check logfile for errors' '
    ! grep -E -i "(EMERGENCY|ALERT|CRITICAL|ERROR):" "${CONMAND_LOGFILE}"
'

# Verify the test console logs have been created.
#
test_expect_success 'check console logfile creation' '
    ls -l ${CONMAND_CONSOLE_GLOB} &&
    test "$(ls ${CONMAND_CONSOLE_GLOB} | wc -l)" -eq 2 &&
    for f in ${CONMAND_CONSOLE_GLOB}; do test -s "$f"; done
'

# Perform housekeeping to clean up afterwards.
#
test_expect_success 'cleanup' '
    conmand_cleanup
'

test_done
