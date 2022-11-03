#!/bin/sh

test_description='Check to build, install, and test RPMs'

: "${SHARNESS_TEST_SRCDIR:=$(cd "$(dirname "$0")" && pwd)}"
. "${SHARNESS_TEST_SRCDIR}/sharness.sh"

# Check for the guard variable.
#
if test "x${CONMAN_CHAOS}" != xt; then
    skip_all='skipping rpm test; chaos not enabled'
    test_done
fi

# Ensure this is a RedHat-based system.
# This regexp matches recent AlmaLinux, CentOS, and Fedora.
#
if grep -E '^ID.*=.*\b(rhel|fedora)\b' /etc/os-release >/dev/null 2>&1; then :
else
    skip_all='skipping rpm test; not a supported redhat-based system'
    test_done
fi

# Ensure the rpmbuild executable is already installed.
#
if command -v rpmbuild >/dev/null 2>&1; then :; else
    skip_all='skipping rpm test; rpmbuild not installed'
    test_done
fi

# Ensure that non-interactive sudo is available.
#
if test_have_prereq SUDO; then :; else
    skip_all='skipping rpm test; sudo not enabled'
    test_done
fi

# Ensure none of the conman RPMs are currently installed in order to prevent
#   overwriting an existing installation.
# It would be quicker to just "rpm --query conman", but that could miss RPMs
#   from a partial (un)install that would interfere with the new installation.
#
if rpm --query --all | grep ^conman-; then
    skip_all='skipping rpm test; conman rpm already installed'
    test_done
fi

# Create a scratch directory for the RPM build.
# Provide [CONMAN_RPM_DIR] for later checks.
#
test_expect_success 'setup' '
    CONMAN_RPM_DIR="${TMPDIR:-"/tmp"}/conman-rpm-$$" &&
    mkdir -p "${CONMAN_RPM_DIR}"
'

# Create the dist tarball for rpmbuild and stash it in the scratch directory.
# Provide [CONMAN_TARBALL] for later checks.
#
test_expect_success 'create dist tarball' '
    cd "${CONMAN_BUILD_DIR}" &&
    rm -f conman-*.tar* &&
    make dist &&
    mv conman-*.tar* "${CONMAN_RPM_DIR}"/ &&
    cd &&
    CONMAN_TARBALL=$(ls "${CONMAN_RPM_DIR}"/conman-*.tar*) &&
    test -f "${CONMAN_TARBALL}" &&
    test_set_prereq CONMAN_DIST
'

# Build the source RPM which is needed to install dependencies for building the
#   binary RPMs.
#
test_expect_success CONMAN_DIST 'build srpm' '
    rpmbuild -ts --without=check --without=verify \
            --define="_builddir %{_topdir}/BUILD" \
            --define="_buildrootdir %{_topdir}/BUILDROOT" \
            --define="_rpmdir %{_topdir}/RPMS" \
            --define="_sourcedir %{_topdir}/SOURCES" \
            --define="_specdir %{_topdir}/SPECS" \
            --define="_srcrpmdir %{_topdir}/SRPMS" \
            --define="_topdir ${CONMAN_RPM_DIR}" \
            "${CONMAN_TARBALL}" &&
    test_set_prereq CONMAN_SRPM
'

# Install build dependencies needed for building the binary RPMs.
#
test_expect_success CONMAN_SRPM 'install builddeps' '
    local builddep &&
    if command -v dnf >/dev/null 2>&1; then
        builddep="dnf builddep --assumeyes"
    elif command -v yum-builddep >/dev/null 2>&1; then
        builddep="yum-builddep"
    else
        echo "builddep command not found"; false
    fi &&
    sudo ${builddep} "${CONMAN_RPM_DIR}"/SRPMS/*.src.rpm
'

# Build in binary RPMs.
#
test_expect_success CONMAN_DIST 'build rpm' '
    rpmbuild -tb --without=check --without=verify \
            --define="_builddir %{_topdir}/BUILD" \
            --define="_buildrootdir %{_topdir}/BUILDROOT" \
            --define="_rpmdir %{_topdir}/RPMS" \
            --define="_sourcedir %{_topdir}/SOURCES" \
            --define="_specdir %{_topdir}/SPECS" \
            --define="_srcrpmdir %{_topdir}/SRPMS" \
            --define="_topdir ${CONMAN_RPM_DIR}" \
            "${CONMAN_TARBALL}" &&
    test_set_prereq CONMAN_RPM
'

# Install the binary RPMs.
# Save the resulting output for later removal of the installed RPMs.
#
test_expect_success CONMAN_RPM 'install rpm' '
    sudo rpm --install --verbose "${CONMAN_RPM_DIR}"/RPMS/*/*.rpm \
            >rpm.install.$$ &&
    cat rpm.install.$$ &&
    test_set_prereq CONMAN_INSTALL
'

# Create a config with 2 test consoles that output 1 byte every 10ms.
#
test_expect_success CONMAN_INSTALL 'create config' '
    cat >conman.conf.$$ <<-EOF && sudo cp conman.conf.$$ /etc/conman.conf
	server loopback=on
	global testopts="b:1,m:10,n:10,p:100"
	console name="test1" dev="test:"
	console name="test2" dev="test:"
	EOF
'

# Start the conman service.
#
test_expect_success CONMAN_INSTALL 'start conman service' '
    sudo systemctl start conman.service
'

# Check if the conman service is running.
#
test_expect_success CONMAN_INSTALL 'check service status' '
    systemctl status --full --no-pager conman.service
'

# Check if the client can query the daemon.
#
test_expect_success CONMAN_INSTALL 'query conman' '
    conman -q >out.$$ &&
    test "$(wc -l <out.$$)" -eq 2
'

# Stop the conman service.
#
test_expect_success CONMAN_INSTALL 'stop conman service' '
    sudo systemctl stop conman.service
'

# Remove the binary RPMs installed previously.
#
test_expect_success CONMAN_INSTALL 'remove rpm' '
    grep ^conman- rpm.install.$$ >rpm.pkgs.$$ &&
    sudo rpm --erase --verbose $(cat rpm.pkgs.$$)
'

# Verify all of the conman RPMs have been removed since their continued
#   presence would prevent this test from running again.
#
test_expect_success CONMAN_INSTALL 'verify rpm removal' '
    rpm --query --all >rpm.query.$$ &&
    ! grep ^conman- rpm.query.$$
'

# Remove the config saved by rpm.
#
test_expect_success CONMAN_INSTALL 'remove config' '
    sudo rm -f /etc/conman.conf.rpmsave
'

# Remove the scratch directory unless [debug] is set.
#
test_expect_success 'cleanup' '
    if test "x${debug}" = xt; then
        echo "rpm dir is \"${CONMAN_RPM_DIR}\""
    else
        rm -rf "${CONMAN_RPM_DIR}"
    fi
'

test_done
