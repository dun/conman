##
# Makefile Include for RPM Construction
#   by Chris Dunlap <cdunlap@llnl.gov>
##
# $Id: Make-rpm.mk,v 1.1 2001/10/07 20:43:49 dun Exp $
##
# NOTES:
# - requires PACKAGE and VERSION macro definitions to be defined
# - requires the project to be under CVS version control
# - requires a "VERSION" file of the format "$(PACKAGE)-$(VERSION)" to
#     reside in the top-level directory of the CVS project (eg, foo-1.2.3);
#     this file should be used to define $(PACKAGE) and $(VERSION)
# - requires an RPM spec file named "$(PACKAGE).spec.in" or "$(PACKAGE).spec"
#     to reside in the top-level directory of the CVS project
# - RPM will be built directly from the CVS repository based on the CVS tag
# - CVS tag will be based on the contents of the VERSION file by default
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
#     default value of 1 (eg, make rpm tag=HEAD rel=13)
# - RPM will be signed with a PGP/GPG key if one is defined in ~/.rpmmacros
##
# TO BUILD A NEW RELEASE:
# - update and cvs commit the "VERSION" file in the top-level directory
# - cvs tag/rtag the project with a tag of the form "foo-1-2-3"
# - make rpm
##

rpm tar:
	@if test -z "$(PACKAGE)"; then \
	  echo "ERROR: Undefined PACKAGE macro definition" 1>&2; exit 0; fi; \
	if test -z "$(VERSION)"; then \
	  echo "ERROR: Undefined VERSION macro definition" 1>&2; exit 0; fi; \
	test -f CVS/Root && cvs="cvs -d `cat CVS/Root`" || cvs="cvs"; \
	test -n "$(mkinstalldirs)" && mkdir="$(mkinstalldirs)" \
	  || mkdir="mkdir -p"; \
	test -z "$$tag" && tag=`echo $(PACKAGE)-$(VERSION) | tr '.' '-'`; \
	ver=`$$cvs -Q co -r $$tag -p $(PACKAGE)/VERSION | \
	  sed -ne 's/.*-\(.*\)/\1/p'`; \
	if test -z "$$ver"; then \
	  echo "ERROR: Cannot determine VERSION (tag=$$tag)" 1>&2; exit 0; fi; \
	test "$$tag" = "HEAD" -o "$$tag" = "BASE" && ver="$$ver+"; \
	test -z "$$rel" && rel=1; \
	$(MAKE) -s $@-internal cvs="$$cvs" mkdir="$$mkdir" \
	  tag="$$tag" ver="$$ver" rel="$$rel"

rpm-internal: tar-internal
	@test -z "$$cvs" -o -z "$$mkdir" && exit 1; \
	test -z "$$tag" -o -z "$$ver" -o -z "$$rel" && exit 1; \
	tmp=$${TMPDIR-/tmp}/tmp-$(PACKAGE)-$$$$; \
	tar=$(PACKAGE)-$$ver.tgz; \
	for d in BUILD RPMS SOURCES SPECS SRPMS TMP; do \
	  if ! $$mkdir $$tmp/$$d >/dev/null; then \
	    echo "ERROR: Cannot create \"$$tmp/$$d\" dir." 1>&2; exit 1; fi; \
	done; \
	cp -p $$tar $$tmp/SOURCES; \
	($$cvs -Q co -r $$tag -p $(PACKAGE)/$(PACKAGE).spec.in || \
	  $$cvs -Q co -r $$tag -p $(PACKAGE)/$(PACKAGE).spec) | \
	  sed -e "s/^\(%define name\).*/\1 $(PACKAGE)/i" \
	    -e "s/^\(%define version\).*/\1 $$ver/i" \
	    -e "s/^\(%define release\).*/\1 $$rel/i" \
	    >$$tmp/SPECS/$(PACKAGE).spec; \
	if ! test -s $$tmp/SPECS/$(PACKAGE).spec; then \
	  echo "ERROR: No $(PACKAGE).spec file (tag=$$tag)" 1>&2; \
	  rm -rf $$tmp; exit 0; fi; \
	echo "creating $(PACKAGE)-$$ver*rpm (tag=$$tag)"; \
	rpm --showrc | egrep "_(gpg|pgp)_name" >/dev/null && sign="--sign"; \
	rpm -ba --define "_tmppath $$tmp/TMP" --define "_topdir $$tmp" \
	  $$sign --quiet $$tmp/SPECS/$(PACKAGE).spec >/dev/null 2>&1 && \
	    cp -p $$tmp/RPMS/*/$(PACKAGE)-$$ver*.*.rpm \
	      $$tmp/SRPMS/$(PACKAGE)-$$ver*.src.rpm .; \
	rm -rf $$tmp

tar-internal:
	@test -z "$$cvs" -o -z "$$mkdir" && exit 1; \
	test -z "$$tag" -o -z "$$ver" && exit 1; \
	tmp=$${TMPDIR-/tmp}/tmp-$(PACKAGE)-$$$$; \
	name=$(PACKAGE)-$$ver; \
	dir=$$tmp/$$name; \
	tar=$$name.tgz; \
	rm -f $$tar; \
	echo "creating $$tar (tag=$$tag)"; \
	if ! $$mkdir $$tmp >/dev/null; then \
	  echo "ERROR: Cannot create \"$$tmp\" dir." 1>&2; exit 1; fi; \
	(cd $$tmp; $$cvs -Q export -r $$tag -d $$name $(PACKAGE)) && \
	  (cd $$tmp; tar cf - $$name) | gzip -c9 >$$tar; \
	rm -rf $$tmp; \
	if ! test -f $$tar; then \
	  echo "ERROR: Cannot create $$tar." 1>&2; exit 1; fi

