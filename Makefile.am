# ConMan Makefile.am

ACLOCAL_AMFLAGS = -I m4

SUBSTITUTE_FILES = \
	conman.spec \
	etc/conman.init \
	etc/conman.logrotate \
	etc/conman.service \
	etc/conman.sysconfig \
	man/conman.1 \
	man/conman.conf.5 \
	man/conmand.8 \
	# End of SUBSTITUTE_FILES

conman.spec: conman.spec.in
etc/conman.init: etc/conman.init.in
etc/conman.logrotate: etc/conman.logrotate.in
etc/conman.service: etc/conman.service.in
etc/conman.sysconfig: etc/conman.sysconfig.in
man/conman.1: man/conman.1.in
man/conman.conf.5: man/conman.conf.5.in
man/conmand.8: man/conmand.8.in

noinst_DATA = \
	$(SUBSTITUTE_FILES) \
	# End of noinst_DATA

bin_PROGRAMS = \
	conman \
	# End of bin_PROGRAMS

dist_bin_SCRIPTS = \
	conmen \
	# End of dist_bin_SCRIPTS

sbin_PROGRAMS = \
	conmand \
	# End of sbin_PROGRAMS

dist_sysconf_DATA = \
	etc/conman.conf \
	# End of dist_sysconf_DATA

man_MANS = \
	man/conman.1 \
	man/conman.conf.5 \
	man/conmand.8 \
	# End of man_MANS

conman_CPPFLAGS = \
	-DWITH_OOMF \
	-DWITH_PTHREADS \
	# End of conman_CPPFLAGS

conman_LDADD = \
	$(LIBOBJS) \
	$(PTHREADLIBS) \
	# End of conman_LDADD

conman_SOURCES = \
	client.c \
	client.h \
	client-conf.c \
	client-sock.c \
	client-tty.c \
	$(common_sources) \
	# End of conman_SOURCES

conmand_CPPFLAGS = \
	-DSYSCONFDIR='$(sysconfdir)' \
	-DWITH_OOMF \
	-DWITH_PTHREADS \
	# End of conmand_CPPFLAGS

conmand_DEPENDENCIES = \
	$(FREEIPMIOBJS) \
	# End of conmand_DEPENDENCIES

conmand_LDADD = \
	$(FREEIPMIOBJS) \
	$(FREEIPMILIBS) \
	$(LIBOBJS) \
	$(PTHREADLIBS) \
	$(TCPWRAPPERSLIBS) \
	# End of conmand_LDADD

conmand_SOURCES = \
	server.c \
	server.h \
	server-conf.c \
	server-esc.c \
	server-logfile.c \
	server-obj.c \
	server-process.c \
	server-serial.c \
	server-sock.c \
	server-telnet.c \
	server-test.c \
	server-unixsock.c \
	bool.h \
	inevent.c \
	inevent.h \
	tpoll.c \
	tpoll.h \
	wrapper.h \
	$(common_sources) \
	# End of conmand_SOURCES

EXTRA_conmand_SOURCES = \
	server-ipmi.c \
	# End of EXTRA_conmand_SOURCES

common_sources = \
	common.c \
	common.h \
	lex.c \
	lex.h \
	list.c \
	list.h \
	log.c \
	log.h \
	util.c \
	util.h \
	util-file.c \
	util-file.h \
	util-net.c \
	util-net.h \
	util-str.c \
	util-str.h \
	# End of common_sources

# For dependency on SYSCONFDIR via the #define for CONMAN_CONF.
#
conmand-server-conf.$(OBJEXT): Makefile

pkgdataexamplesdir = $(pkgdatadir)/examples

dist_pkgdataexamples_DATA = \
	scripts/examples/README \
	# End of dist_pkgdataexamples_DATA

dist_pkgdataexamples_SCRIPTS = \
	scripts/examples/boot_alpha.exp \
	scripts/examples/mac.exp \
	scripts/examples/rmc.exp \
	scripts/examples/shell_cmd_1.exp \
	scripts/examples/shell_cmd_n.exp \
	scripts/examples/srm.exp \
	scripts/examples/wtf.exp \
	# End of dist_pkgdataexamples_SCRIPTS

pkgdataexecdir = $(pkgdatadir)/exec

dist_pkgdataexec_DATA = \
	scripts/exec/README \
	# End of dist_pkgdataexec_DATA

dist_pkgdataexec_SCRIPTS = \
	scripts/exec/cyclades-ssh.exp \
	scripts/exec/cyclades-telnet.exp \
	scripts/exec/hp-ilo.exp \
	scripts/exec/hp-lo100.exp \
	scripts/exec/ibm-bc.exp \
	scripts/exec/ipmiconsole.exp \
	scripts/exec/ipmitool.exp \
	scripts/exec/ssh.exp \
	scripts/exec/sun-elom.exp \
	scripts/exec/sun-ilom.exp \
	scripts/exec/sun-v20z-v40z.exp \
	# End of dist_pkgdataexec_SCRIPTS

pkgdatalibdir = $(pkgdatadir)/lib

dist_pkgdatalib_SCRIPTS = \
	scripts/lib/alpha.exp \
	scripts/lib/conman.exp \
	# End of dist_pkgdatalib_SCRIPTS

$(SUBSTITUTE_FILES): Makefile
	@$(MKDIR_P) `dirname "$(builddir)/$@"`
	$(AM_V_GEN)$(substitute) < "$(srcdir)/$@.in" > "$(builddir)/$@"

substitute = $(SED) \
	-e 's|[@]bindir[@]|$(bindir)|g' \
	-e 's|[@]datadir[@]|$(datadir)|g' \
	-e 's|[@]datarootdir[@]|$(datarootdir)|g' \
	-e 's|[@]docdir[@]|$(docdir)|g' \
	-e 's|[@]dvidir[@]|$(dvidir)|g' \
	-e 's|[@]exec_prefix[@]|$(exec_prefix)|g' \
	-e 's|[@]htmldir[@]|$(htmldir)|g' \
	-e 's|[@]includedir[@]|$(includedir)|g' \
	-e 's|[@]infodir[@]|$(infodir)|g' \
	-e 's|[@]libdir[@]|$(libdir)|g' \
	-e 's|[@]libexecdir[@]|$(libexecdir)|g' \
	-e 's|[@]localedir[@]|$(localedir)|g' \
	-e 's|[@]localstatedir[@]|$(localstatedir)|g' \
	-e 's|[@]mandir[@]|$(mandir)|g' \
	-e 's|[@]oldincludedir[@]|$(oldincludedir)|g' \
	-e 's|[@]pdfdir[@]|$(pdfdir)|g' \
	-e 's|[@]pkgdatadir[@]|$(pkgdatadir)|g' \
	-e 's|[@]pkgincludedir[@]|$(pkgincludedir)|g' \
	-e 's|[@]pkglibdir[@]|$(pkglibdir)|g' \
	-e 's|[@]pkglibexecdir[@]|$(pkglibexecdir)|g' \
	-e 's|[@]prefix[@]|$(prefix)|g' \
	-e 's|[@]psdir[@]|$(psdir)|g' \
	-e 's|[@]runstatedir[@]|$(runstatedir)|g' \
	-e 's|[@]sbindir[@]|$(sbindir)|g' \
	-e 's|[@]sharedstatedir[@]|$(sharedstatedir)|g' \
	-e 's|[@]sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|[@]sysconfigdir[@]|$(sysconfigdir)|g' \
	-e 's|[@]systemdunitdir[@]|$(systemdunitdir)|g' \
	-e 's|[@]sysvinitddir[@]|$(sysvinitddir)|g' \
	-e 's|[@]CONMAN_CONF[@]|$(CONMAN_CONF)|g' \
	-e 's|[@]CONMAN_HOST[@]|$(CONMAN_HOST)|g' \
	-e 's|[@]CONMAN_PORT[@]|$(CONMAN_PORT)|g' \
	-e 's|[@]DATE[@]|$(DATE)|g' \
	-e 's|[@]PACKAGE[@]|$(PACKAGE)|g' \
	-e 's|[@]VERSION[@]|$(VERSION)|g' \
	# End of substitute

install-data-hook: install-logrotate install-systemd install-sysvinit

uninstall-local: uninstall-logrotate uninstall-systemd uninstall-sysvinit

install-logrotate: etc/conman.logrotate
	$(MKDIR_P) '$(DESTDIR)$(sysconfdir)/logrotate.d'
	$(INSTALL_DATA) '$(top_builddir)/etc/conman.logrotate' \
	    '$(DESTDIR)$(sysconfdir)/logrotate.d/$(PACKAGE)'

uninstall-logrotate:
	( cd '$(DESTDIR)$(sysconfdir)/logrotate.d' && rm -f '$(PACKAGE)' )

install-systemd: etc/conman.service
	@if test 'x$(systemdunitdir)' != x; then \
	  echo $(MKDIR_P) "'$(DESTDIR)$(systemdunitdir)'"; \
	  $(MKDIR_P) '$(DESTDIR)$(systemdunitdir)'; \
	  echo $(INSTALL_DATA) "'$(top_builddir)/etc/conman.service'" \
	      "'$(DESTDIR)$(systemdunitdir)/$(PACKAGE).service'"; \
	  $(INSTALL_DATA) '$(top_builddir)/etc/conman.service' \
	      '$(DESTDIR)$(systemdunitdir)/$(PACKAGE).service'; \
	fi

uninstall-systemd:
	@if test 'x$(systemdunitdir)' != x; then \
	  echo " ( cd '$(DESTDIR)$(systemdunitdir)'" \
	      "&& rm -f '$(PACKAGE).service' )"; \
	  ( cd '$(DESTDIR)$(systemdunitdir)' && rm -f '$(PACKAGE).service' ); \
	fi

install-sysvinit: etc/conman.init install-sysvinit-conf
	@if test 'x$(sysvinitddir)' != x; then \
	  echo $(MKDIR_P) "'$(DESTDIR)$(sysvinitddir)'"; \
	  $(MKDIR_P) '$(DESTDIR)$(sysvinitddir)'; \
	  echo $(INSTALL_SCRIPT) "'$(top_builddir)/etc/conman.init'" \
	      "'$(DESTDIR)$(sysvinitddir)/$(PACKAGE)'"; \
	  $(INSTALL_SCRIPT) '$(top_builddir)/etc/conman.init' \
	      '$(DESTDIR)$(sysvinitddir)/$(PACKAGE)'; \
	fi

uninstall-sysvinit: uninstall-sysvinit-conf
	@if test 'x$(sysvinitddir)' != x; then \
	  echo " ( cd '$(DESTDIR)$(sysvinitddir)' && rm -f '$(PACKAGE)' )"; \
	  ( cd '$(DESTDIR)$(sysvinitddir)' && rm -f '$(PACKAGE)' ); \
	fi

install-sysvinit-conf: etc/conman.sysconfig
	@if test 'x$(sysvinitddir)' != x \
	    && test 'x$(sysconfigdir)' != x; then \
	  echo $(MKDIR_P) "'$(DESTDIR)$(sysconfigdir)'"; \
	  $(MKDIR_P) '$(DESTDIR)$(sysconfigdir)'; \
	  echo $(INSTALL_DATA) "'$(top_builddir)/etc/conman.sysconfig'" \
	      "'$(DESTDIR)$(sysconfigdir)/$(PACKAGE)'"; \
	  $(INSTALL_DATA) '$(top_builddir)/etc/conman.sysconfig' \
	      '$(DESTDIR)$(sysconfigdir)/$(PACKAGE)'; \
	fi

uninstall-sysvinit-conf:
	@if test 'x$(sysvinitddir)' != x \
	    && test 'x$(sysconfigdir)' != x; then \
	  echo " ( cd '$(DESTDIR)$(sysconfigdir)' && rm -f '$(PACKAGE)' )"; \
	  ( cd '$(DESTDIR)$(sysconfigdir)' && rm -f '$(PACKAGE)' ); \
	fi

TEST_EXTENSIONS = .t

T_LOG_DRIVER = \
	env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/build-aux/tap-driver.sh

TESTS = \
	# End of TESTS

EXTRA_DIST = \
	.gitignore \
	AUTHORS \
	COPYING \
	DISCLAIMER.LLNS \
	DISCLAIMER.UC \
	FAQ \
	INSTALL \
	KEYS \
	NEWS \
	PLATFORMS \
	README \
	README.md \
	THANKS \
	bootstrap \
	build-aux/gen-date \
	build-aux/gen-version \
	build-aux/tap-driver.sh \
	conman.spec.in \
	etc/conman.conf \
	etc/conman.init.in \
	etc/conman.logrotate.in \
	etc/conman.service.in \
	etc/conman.sysconfig.in \
	man/conman.1.in \
	man/conman.conf.5.in \
	man/conmand.8.in \
	test/lib-sharness \
	test/sharness.sh \
	$(TESTS) \
	# End of EXTRA_DIST

dist-hook: conman.spec
	$(INSTALL_DATA) conman.spec '$(distdir)/conman.spec'
	echo $(VERSION) > '$(distdir)/.dist-version'
	echo $(DATE) > '$(distdir)/.dist-date'

CLEANFILES = \
	$(SUBSTITUTE_FILES) \
	# End of CLEANFILES

clean-local:
	-rm -rf test-results
	-rm -rf trash*directory.*

DISTCLEANFILES = \
	config.h.in~ \
	configure~ \
	$(PACKAGE)-*.tar* \
	# End of DISTCLEANFILES

distclean-local:
	-rm -rf autom4te.cache/
	-rmdir etc/ man/ 2>/dev/null || :

MAINTAINERCLEANFILES = \
	Makefile.in \
	aclocal.m4 \
	build-aux/compile \
	build-aux/depcomp \
	build-aux/install-sh \
	build-aux/missing \
	build-aux/tap-driver.sh \
	build-aux/test-driver \
	config.h.in \
	configure \
	# End of MAINTAINERCLEANFILES
