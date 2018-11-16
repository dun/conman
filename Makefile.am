# ConMan Makefile.am

ACLOCAL_AMFLAGS = -I m4

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
	conman.spec \
	etc/conman.conf \
	etc/conman.init.in \
	etc/conman.logrotate.in \
	etc/conman.service.in \
	etc/conman.sysconfig.in \
	man/conman.1.in \
	man/conman.conf.5.in \
	man/conmand.8.in

SUBSTITUTE_FILES = \
	etc/conman.init \
	etc/conman.logrotate \
	etc/conman.service \
	etc/conman.sysconfig \
	man/conman.1 \
	man/conman.conf.5 \
	man/conmand.8

etc/conman.init: etc/conman.init.in
etc/conman.logrotate: etc/conman.logrotate.in
etc/conman.service: etc/conman.service.in
etc/conman.sysconfig: etc/conman.sysconfig.in
man/conman.1: man/conman.1.in
man/conman.conf.5: man/conman.conf.5.in
man/conmand.8: man/conmand.8.in

noinst_DATA = \
	$(SUBSTITUTE_FILES)

bin_PROGRAMS = \
	conman

dist_bin_SCRIPTS = \
	conmen

sbin_PROGRAMS = \
	conmand

dist_sysconf_DATA = \
	etc/conman.conf

man_MANS = \
	man/conman.1 \
	man/conman.conf.5 \
	man/conmand.8

conman_CPPFLAGS = \
	-DWITH_OOMF \
	-DWITH_PTHREADS

conman_LDADD = \
	$(LIBOBJS) \
	$(PTHREADLIBS)

conman_SOURCES = \
	client.c \
	client.h \
	client-conf.c \
	client-sock.c \
	client-tty.c \
	$(common_sources)

conmand_CPPFLAGS = \
	-DSYSCONFDIR='$(sysconfdir)' \
	-DWITH_OOMF \
	-DWITH_PTHREADS

conmand_DEPENDENCIES = \
	$(FREEIPMIOBJS)

conmand_LDADD = \
	$(FREEIPMIOBJS) \
	$(FREEIPMILIBS) \
	$(LIBOBJS) \
	$(PTHREADLIBS) \
	$(TCPWRAPPERSLIBS)

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
	$(common_sources)

EXTRA_conmand_SOURCES = \
	server-ipmi.c

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
	util-str.h

# For dependency on SYSCONFDIR via the #define for CONMAN_CONF.
#
conmand-server-conf.$(OBJEXT): Makefile

pkgdataexamplesdir = $(pkgdatadir)/examples

dist_pkgdataexamples_DATA = \
	scripts/examples/README

dist_pkgdataexamples_SCRIPTS = \
	scripts/examples/boot_alpha.exp \
	scripts/examples/mac.exp \
	scripts/examples/rmc.exp \
	scripts/examples/shell_cmd_1.exp \
	scripts/examples/shell_cmd_n.exp \
	scripts/examples/srm.exp \
	scripts/examples/wtf.exp

pkgdataexecdir = $(pkgdatadir)/exec

dist_pkgdataexec_DATA = \
	scripts/exec/README

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
	scripts/exec/sun-v20z-v40z.exp

pkgdatalibdir = $(pkgdatadir)/lib

dist_pkgdatalib_SCRIPTS = \
	scripts/lib/alpha.exp \
	scripts/lib/conman.exp

$(SUBSTITUTE_FILES): Makefile
	@$(MKDIR_P) `dirname "$(builddir)/$@"`
	$(AM_V_GEN)$(substitute) < "$(srcdir)/$@.in" > "$(builddir)/$@"

substitute = $(SED) \
	-e 's,[@]bindir[@],$(bindir),g' \
	-e 's,[@]datadir[@],$(datadir),g' \
	-e 's,[@]datarootdir[@],$(datarootdir),g' \
	-e 's,[@]docdir[@],$(docdir),g' \
	-e 's,[@]dvidir[@],$(dvidir),g' \
	-e 's,[@]exec_prefix[@],$(exec_prefix),g' \
	-e 's,[@]htmldir[@],$(htmldir),g' \
	-e 's,[@]includedir[@],$(includedir),g' \
	-e 's,[@]infodir[@],$(infodir),g' \
	-e 's,[@]libdir[@],$(libdir),g' \
	-e 's,[@]libexecdir[@],$(libexecdir),g' \
	-e 's,[@]localedir[@],$(localedir),g' \
	-e 's,[@]localstatedir[@],$(localstatedir),g' \
	-e 's,[@]mandir[@],$(mandir),g' \
	-e 's,[@]oldincludedir[@],$(oldincludedir),g' \
	-e 's,[@]pdfdir[@],$(pdfdir),g' \
	-e 's,[@]pkgdatadir[@],$(pkgdatadir),g' \
	-e 's,[@]pkgincludedir[@],$(pkgincludedir),g' \
	-e 's,[@]pkglibdir[@],$(pkglibdir),g' \
	-e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' \
	-e 's,[@]prefix[@],$(prefix),g' \
	-e 's,[@]psdir[@],$(psdir),g' \
	-e 's,[@]runstatedir[@],$(runstatedir),g' \
	-e 's,[@]sbindir[@],$(sbindir),g' \
	-e 's,[@]sharedstatedir[@],$(sharedstatedir),g' \
	-e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	-e 's,[@]CONMAN_CONF[@],$(CONMAN_CONF),g' \
	-e 's,[@]CONMAN_HOST[@],$(CONMAN_HOST),g' \
	-e 's,[@]CONMAN_PORT[@],$(CONMAN_PORT),g' \
	-e 's,[@]DATE[@],$(DATE),g' \
	-e 's,[@]PACKAGE[@],$(PACKAGE),g' \
	-e 's,[@]VERSION[@],$(VERSION),g'

install-data-local: install-logrotate install-systemd install-sysvinit

install-logrotate: etc/conman.logrotate
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/logrotate.d
	$(INSTALL_DATA) $(top_builddir)/etc/conman.logrotate \
	  $(DESTDIR)$(sysconfdir)/logrotate.d/$(PACKAGE)

install-systemd: etc/conman.service
	$(MKDIR_P) $(DESTDIR)$(prefix)/lib/systemd/system
	$(INSTALL_DATA) $(top_builddir)/etc/conman.service \
	  $(DESTDIR)$(prefix)/lib/systemd/system/$(PACKAGE).service

install-sysvinit: etc/conman.init install-sysvinit-conf
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/init.d
	$(INSTALL_SCRIPT) $(top_builddir)/etc/conman.init \
	  $(DESTDIR)$(sysconfdir)/init.d/$(PACKAGE)

install-sysvinit-conf: etc/conman.sysconfig
	test -d /etc/sysconfig && d=sysconfig || d=default; \
	  $(MKDIR_P) $(DESTDIR)$(sysconfdir)/$${d}; \
	  $(INSTALL_DATA) $(top_builddir)/etc/conman.sysconfig \
	    $(DESTDIR)$(sysconfdir)/$${d}/$(PACKAGE)

uninstall-local:
	-cd "$(DESTDIR)$(sysconfdir)/logrotate.d" && rm -f $(PACKAGE)
	-cd "$(DESTDIR)$(prefix)/lib/systemd/system" && rm -f $(PACKAGE).service
	-cd "$(DESTDIR)$(sysconfdir)/init.d" && rm -f $(PACKAGE)
	-test -d /etc/sysconfig && d=sysconfig || d=default; \
	  cd "$(DESTDIR)$(sysconfdir)/$${d}" && rm -f $(PACKAGE)

CLEANFILES = \
	$(SUBSTITUTE_FILES)

DISTCLEANFILES = \
	config.h.in~ \
	$(PACKAGE)-*.tar*

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
	config.h.in \
	configure

maintainer-clean-local:
	-rmdir build-aux/ 2>/dev/null || :
