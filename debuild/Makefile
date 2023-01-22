# This is a GNU Makefile. Special tools needed: GNU make and awk, git.

# Make Debian packages. This builds a package from the current HEAD in a
# subdirectory named $(debdist). It also creates the source archive that goes
# along with it. All files will be created in the debuild directory.

# To make this work, you need to have the Debian package toolchain (debuild
# and friends) installed. You should also set the DEBEMAIL environment (or
# make) variable to your full name and email address as explained in the
# debchange(1) manual page, e.g.: DEBEMAIL="John Doe <johndoe@mail.com>"
# This information is used to create changelog entries with 'make debchange',
# which in turn are used to sign the Debian packages created with 'make deb'
# and 'make debsrc'. (Otherwise some generic information supplied by the
# system will be used, and you may not be able to sign the package.)

# The typical workflow is as follows:

# 1. Run 'make debchange' once to create a new debian/changelog entry. You
# *must* do this once after changes in the source repository so that debuild
# knows about the proper version number of the package.

# 2. Run 'make deb' to build a signed binary package. Or 'make deb-us' for an
# unsigned one.

# If you only need the binary package for local deployment then you're done.
# Otherwise proceed to step 3.

# 3. Run 'make debsrc' to create a signed Debian source package which can be
# uploaded, e.g, to Launchpad using 'dput'. Or 'make debsrc-us' for an
# unsigned package.

# 4. Run 'make debclean' to get rid of any files that were created in steps 2
# and 3, as well as any changes to the debian directory.

# Pd flavor. This adjusts the package name and installation directory.
puredata = pd
pd = $(shell echo "$(puredata)" | sed -e 's/-//g')
ifeq ($(puredata),purr-data)
pdlibdir = /opt/purr-data/lib/pd-l2ork
else
pdlibdir = /usr/lib/$(puredata)
endif

# The Debian version gets derived from the date and serial number of the last
# commit. You can print it with 'make debversion'.
version = $(shell grep "#define FAUSTGEN_VERSION_STR" ../src/faustgen_tilde.c | sed 's|^#define *FAUSTGEN_VERSION_STR *"\(.*\)".*|\1|')
debversion = $(version)+git$(shell git rev-list --count HEAD)+$(shell git rev-parse --short HEAD)
# Debian revision number of the package.
debrevision = 1
# Source tarball and folder.
debsrc = $(pd)-faustgen2_$(debversion).orig.tar.gz
debdist = pd-faustgen2-$(version)

# This is used for automatically generated debian/changelog entries (cf. 'make
# debchange'). Adjust as needed.
debmsg = "Build from latest upstream source."
debprio = "low"

.PHONY: debversion debchange debclean deb debsrc deb-us debsrc-us

debversion:
	@echo $(debversion)

debchange:
	dch -u $(debprio) -v $(debversion)-$(debrevision) $(debmsg) && dch -r ""

debclean:
	rm -rf $(debdist)
	rm -f $(pd)-faustgen2_*+git* $(pd)-faustgen2-dbgsym_*+git*
	git checkout debian/

deb: $(debsrc)
# Unpack the source into our temporary build directory.
	rm -rf $(debdist)
	tar xfz $(debsrc)
# Make sure to copy the Debian files which may have uncommitted changes.
# Then run debuild to create the package.
	cd $(debdist) && cp -R ../debian . && (cd debian && sed -e 's/pd-faustgen2/$(pd)-faustgen2/g' -i changelog && sed -e 's/pd-faustgen2/$(pd)-faustgen2/g' -i control && sed -e 's/pd-faustgen2/$(pd)-faustgen2/g' -e 's|/usr/lib/pd|$(pdlibdir)|g' -i rules) && debuild $(DEBUILD_FLAGS)
# Get rid of the temporary build directory.
	rm -rf $(debdist)

debsrc:
	$(MAKE) deb DEBUILD_FLAGS="-S -d"

deb-us:
	$(MAKE) deb DEBUILD_FLAGS="-us -uc"

debsrc-us:
	$(MAKE) deb DEBUILD_FLAGS="-S -d -us -uc"

src=pd-faustgen2-$(version).tar.gz

$(debsrc):
	rm -rf $(debsrc) ../$(src)
	cd .. && ./make-dist.sh
	mv ../$(src) $(debsrc)
