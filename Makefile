# Makefile for GNU Emacs Lisp Package Archive.

EMACS=emacs

ARCHIVE_TMP=archive-tmp
SITE_DIR=site

.PHONY: archive-tmp process-archive archive-full org-fetch clean all do-it

## Set up the source files for direct usage, by pointing
## `package-directory-list' to the site/ directory.
site: packages
	mkdir -p $(SITE_DIR)
	$(EMACS) -batch -l $(CURDIR)/admin/archive-contents.el \
	  --eval "(batch-make-site-dir \"packages\" \"$(SITE_DIR)\")"

site/%: do-it
	$(EMACS) -batch -l $(CURDIR)/admin/archive-contents.el \
	  --eval "(progn (setq debug-on-error t) (batch-make-site-package \"$@\"))"

## Deploy the package archive to archive/, with packages in
## archive/packages/:
archive: archive-tmp
	$(MAKE) $(MFLAGS) process-archive

archive-tmp: packages
	mkdir -p $(ARCHIVE_TMP)
	cp -r packages $(ARCHIVE_TMP)

process-archive:
	cd $(ARCHIVE_TMP)/packages; $(EMACS) -batch -l $(CURDIR)/admin/archive-contents.el -f batch-make-archive
	@cd $(ARCHIVE_TMP)/packages; \
	for pt in *; do \
	    if [ -d $$pt ]; then \
		echo "Creating tarball $${pt}.tar" && \
		tar -cf $${pt}.tar $$pt --remove-files; \
	    fi; \
	done
	mkdir -p archive/packages
	mv archive/packages archive/packages-old
	mv $(ARCHIVE_TMP)/packages archive/packages
	chmod -R a+rX archive/packages
	rm -rf archive/packages-old
	rm -rf $(ARCHIVE_TMP)

## Deploy the package archive to archive/ including the Org daily and
## admin scripts:
archive-full: archive-tmp org-fetch
	$(MAKE) $(MFLAGS) process-archive
	mkdir -p archive/admin
	cp admin/* archive/admin/

org-fetch: archive-tmp
	cd archive-tmp/packages; \
	pkgname=`curl -s http://orgmode.org/elpa/|perl -ne 'push @f, $1 if m/(org-\d{8})\.tar/; END { @f = sort @f; print "$f[-1]\n"}'`; \
	wget -q http://orgmode.org/elpa/${pkgname}.tar -O ${pkgname}.tar; \
	if [ -f $${pkgname}.tar ]; then \
		tar xf $${pkgname}.tar; \
		rm -f $${pkgname}.tar; \
		mv $${pkgname} org; \
	fi

clean:
	rm -rf archive $(ARCHIVE_TMP) $(SITE_DIR)

all: site
