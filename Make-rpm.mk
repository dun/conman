##
# Makefile Include for RPM Construction
#   by Chris Dunlap <cdunlap@llnl.gov>
##
# $Id: Make-rpm.mk,v 1.9 2001/12/05 19:02:58 dun Exp $
##
# REQUIREMENTS:
# - requires project to be under CVS version control
# - requires PACKAGE and VERSION makefile macro definitions to be defined
# - supports optional RELEASE makefile macro definition for the rpm target
# - requires a "VERSION" file of the format "$(PACKAGE)-$(VERSION)" to
#     reside in the top-level directory of the CVS project (eg, foo-1.2.3);
#     this file should be used to define $(PACKAGE) and $(VERSION)
# - requires RPM spec file named "$(PACKAGE).spec.in" or "$(PACKAGE).spec"
#     to reside in the top-level directory of the CVS project
##
# NOTES:
# - RPM will be built directly from the CVS repository based on the CVS tag
# - CVS tag will be based on the contents of the VERSION file by default;
#     this allows the version information to be stored and tagged within CVS
# - CVS tag can be specified on the make cmdline to override the default
#     (eg, make rpm tag=foo-1-2-3)
# - CVS "HEAD" tag can be used to build the most recent version in CVS
#     w/o requiring the release to be CVS tagged (eg, make rpm tag=HEAD);
#     this is intended for pre-release testing purposes only
# - CVS "HEAD" releases will have a "+" appended to the version to denote
#     an augmented release; the contents of such a release can be resurrected
#     from CVS by using a CVS date spec "-D" based on the RPM's "Build Date"
#     (eg, rpm -qp --queryformat="%{BUILDTIME:date}\n" foo-1.2.3-1.i386.rpm)
# - RPM release number can be specified on the make cmdline to override the
#     default of 1 or the RELEASE macro def (eg, make rpm tag=HEAD rel=13)
# - RPM will be signed with a PGP/GPG key if one is specified in ~/.rpmmacros
##
# USAGE:
# - update and cvs commit the "VERSION" file in the top-level directory
# - cvs tag/rtag the project with a tag of the form "foo-1-2-3" (foo v1.2.3)
# - make rpm
##

rpm tar:
	@if test -z "$(PACKAGE)"; then \
	  echo "ERROR: Undefined PACKAGE macro definition" 1>&2; exit 0; fi; \
	pkg=$(PACKAGE); \
	if test -z "$(VERSION)"; then \
	  echo "ERROR: Undefined VERSION macro definition" 1>&2; exit 0; fi; \
	ver=$(VERSION); \
	test -f CVS/Root && cvs="cvs -d `cat CVS/Root`" || cvs="cvs"; \
	test -n "$(mkinstalldirs)" && mkdir="$(mkinstalldirs)" \
	  || mkdir="mkdir -p"; \
	test -z "$$tag" && tag=`echo $$pkg-$$ver | tr '.' '-'`; \
	info=`$$cvs -Q co -r $$tag -p $$pkg/VERSION`; \
	pkg=`echo "$$info" | sed -ne 's/^\(.*\)-\(.*\)/\1/p'`; \
	if test "$$pkg" != "$(PACKAGE)"; then \
	  echo "ERROR: Cannot determine PACKAGE (tag=$$tag)" 1>&2; exit 0; fi; \
	ver=`echo "$$info" | sed -ne 's/^\(.*\)-\(.*\)/\2/p'`; \
	if test -z "$$ver"; then \
	  echo "ERROR: Cannot determine VERSION (tag=$$tag)" 1>&2; exit 0; fi; \
	test "$$tag" = "HEAD" -o "$$tag" = "BASE" && ver="$$ver+"; \
	test -z "$$rel" && rel=$(RELEASE); \
	test -z "$$rel" && rel=1; \
	$(MAKE) -s $@-internal cvs="$$cvs" mkdir="$$mkdir" \
	  tag="$$tag" pkg="$$pkg" ver="$$ver" rel="$$rel"

rpm-internal: tar-internal
	@test -z "$$cvs" -o -z "$$mkdir" && exit 1; \
	test -z "$$tag" -o -z "$$ver" -o -z "$$rel" && exit 1; \
	tmp=$${TMPDIR-/tmp}/tmp-$$pkg-$$$$; \
	log=$$tmp/TMP/rpm.log; \
	tar=$$pkg-$$ver.tgz; \
	for d in BUILD RPMS SOURCES SPECS SRPMS TMP; do \
	  if ! $$mkdir $$tmp/$$d >/dev/null; then \
	    echo "ERROR: Cannot create \"$$tmp/$$d\" dir." 1>&2; exit 1; fi; \
	done; \
	cp -p $$tar $$tmp/SOURCES; \
	(test -f $$pkg.spec.in && $$cvs -Q co -r $$tag -p $$pkg/$$pkg.spec.in \
	  || $$cvs -Q co -r $$tag -p $$pkg/$$pkg.spec) | \
	  sed -e "s/^\([ 	]*Name:\).*/\1 $$pkg/i" \
	    -e "s/^\([ 	]*Version:\).*/\1 $$ver/i" \
	    -e "s/^\([ 	]*Release:\).*/\1 $$rel/i" \
	    >$$tmp/SPECS/$$pkg.spec; \
	if ! test -s $$tmp/SPECS/$$pkg.spec; then \
	  echo "ERROR: No $$pkg.spec file (tag=$$tag)" 1>&2; \
	  rm -rf $$tmp; exit 0; fi; \
	echo "creating $$pkg-$$ver*rpm (tag=$$tag)"; \
	rpm --showrc | egrep "_(gpg|pgp)_name" >/dev/null && sign="--sign"; \
	rpm -ba --define "_tmppath $$tmp/TMP" --define "_topdir $$tmp" \
	  $$sign --quiet $$tmp/SPECS/$$pkg.spec >$$log 2>&1 \
	    && cp -p $$tmp/RPMS/*/$$pkg-$$ver*.*.rpm \
	      $$tmp/SRPMS/$$pkg-$$ver*.src.rpm . \
	    || cat $$log; \
	rm -rf $$tmp

tar-internal:
	@test -z "$$cvs" -o -z "$$mkdir" && exit 1; \
	test -z "$$tag" -o -z "$$ver" && exit 1; \
	tmp=$${TMPDIR-/tmp}/tmp-$$pkg-$$$$; \
	name=$$pkg-$$ver; \
	dir=$$tmp/$$name; \
	tar=$$name.tgz; \
	rm -f $$tar; \
	echo "creating $$tar (tag=$$tag)"; \
	if ! $$mkdir $$tmp >/dev/null; then \
	  echo "ERROR: Cannot create \"$$tmp\" dir." 1>&2; exit 1; fi; \
	(cd $$tmp; $$cvs -Q export -r $$tag -d $$name $$pkg) \
	  && (cd $$tmp; tar cf - $$name) | gzip -c9 >$$tar; \
	rm -rf $$tmp; \
	if ! test -f $$tar; then \
	  echo "ERROR: Cannot create $$tar." 1>&2; exit 1; fi

