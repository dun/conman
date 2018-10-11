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
	etc/conman.logrotate \
	etc/conman.service \
	etc/conman.sysconfig

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
	-DSYSCONFDIR='"$(sysconfdir)"' \
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

pkgdataexamplesdir = $(pkgdatadir)/examples

dist_pkgdataexamples_DATA = \
	lib/examples/README

dist_pkgdataexamples_SCRIPTS = \
	lib/examples/boot_alpha.exp \
	lib/examples/mac.exp \
	lib/examples/rmc.exp \
	lib/examples/shell_cmd_1.exp \
	lib/examples/shell_cmd_n.exp \
	lib/examples/srm.exp \
	lib/examples/wtf.exp

pkgdataexecdir = $(pkgdatadir)/exec

dist_pkgdataexec_DATA = \
	lib/exec/README

dist_pkgdataexec_SCRIPTS = \
	lib/exec/cyclades-ssh.exp \
	lib/exec/cyclades-telnet.exp \
	lib/exec/hp-ilo.exp \
	lib/exec/hp-lo100.exp \
	lib/exec/ibm-bc.exp \
	lib/exec/ipmiconsole.exp \
	lib/exec/ipmitool.exp \
	lib/exec/ssh.exp \
	lib/exec/sun-elom.exp \
	lib/exec/sun-ilom.exp \
	lib/exec/sun-v20z-v40z.exp

pkgdatalibdir = $(pkgdatadir)/lib

dist_pkgdatalib_SCRIPTS = \
	lib/alpha.exp \
	lib/conman.exp

install-data-local: install-logrotate install-systemd install-sysvinit

install-logrotate:
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/logrotate.d
	$(INSTALL_DATA) $(top_srcdir)/etc/conman.logrotate \
	  $(DESTDIR)$(sysconfdir)/logrotate.d/$(PACKAGE)

install-systemd:
	$(MKDIR_P) $(DESTDIR)$(prefix)/lib/systemd/system
	$(INSTALL_DATA) $(top_srcdir)/etc/conman.service \
	  $(DESTDIR)$(prefix)/lib/systemd/system/$(PACKAGE).service

install-sysvinit: install-sysvinit-conf
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/init.d
	$(INSTALL_SCRIPT) $(top_builddir)/etc/conman.init \
	  $(DESTDIR)$(sysconfdir)/init.d/$(PACKAGE)

install-sysvinit-conf:
	test -d /etc/sysconfig && d=sysconfig || d=default; \
	  $(MKDIR_P) $(DESTDIR)$(sysconfdir)/$${d}; \
	  $(INSTALL_DATA) $(top_srcdir)/etc/conman.sysconfig \
	    $(DESTDIR)$(sysconfdir)/$${d}/$(PACKAGE)

uninstall-local:
	-cd "$(DESTDIR)$(sysconfdir)/logrotate.d" && rm -f $(PACKAGE)
	-cd "$(DESTDIR)$(prefix)/lib/systemd/system" && rm -f $(PACKAGE).service
	-cd "$(DESTDIR)$(sysconfdir)/init.d" && rm -f $(PACKAGE)
	-test -d /etc/sysconfig && d=sysconfig || d=default; \
	  cd "$(DESTDIR)$(sysconfdir)/$${d}" && rm -f $(PACKAGE)

DISTCLEANFILES = \
	config.h.in~ \
	$(PACKAGE)-*.tar*

distclean-local:
	-rm -rf autom4te.cache/

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
