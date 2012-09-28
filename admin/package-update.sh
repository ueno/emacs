#/bin/bash
## package-update.sh -- Create a package archive from the elpa repository

## Usage: ./package-update.sh DEST [FULL-UPDATE]
##
## This creates a package archive beginning in DEST.
##
## The package archive itself is created in DEST/packages.  This dir
## contains the package files.
##
## If a second argument FULL-UPDATE is specified (whatever its value),
## also create the following:
##   - the archive admin scripts in DEST/admin
##   - a tarball containing the entire archive in
##     DEST/packages/emacs-packages-latest.tgz
##   - the Org mode daily package

PATH="/bin:/usr/bin:/usr/local/bin:${PATH}"
## Remove any trailing slash from DEST
DEST=${1%/}
FULL=$2

EMACS=emacs
BZR=bzr
TAR=tar
REPO_PACKAGES=packages

## Parse arguments
if [ -z $DEST ]; then
    echo "Syntax: $0 DEST [fetch-extras-boolean]"
    exit 1
elif [ -d $DEST ]; then
    cd $DEST
    DEST_FULL=$(pwd)
    PKGROOT=$DEST_FULL/packages
    TMP_PKGROOT=$DEST_FULL/packages-new
    echo "Installing into '$DEST_FULL'"
    if [ -z $FULL ]; then
	echo "Base archive update only (pass second arg for full update)."
    else
	echo "Performing full archive update."
	TARBALL=$PKGROOT/emacs-packages-latest.tgz
	TARBALL_ROOT="emacs-24.1-packages-`/bin/date +'%F'`"
	ADMINROOT=$DEST_FULL/admin
	REPO_ADMIN=admin
    fi
else
    echo "Sorry but $DEST is not a directory, aborting."
    exit 1
fi

## Change to the bzr root directory
REPO_ROOT_DIR=`$BZR root`;
if [ -z $REPO_ROOT_DIR ]; then
    "This script should be run from a bzr repository, aborting."
    exit 1
else
    cd $REPO_ROOT_DIR;
fi

## Create the working directory that will be the world-facing copy of
## the package archive base.
echo "Exporting packages to temporary working directory $TMP_PKGROOT"
rm -rf $TMP_PKGROOT
$BZR export $TMP_PKGROOT $REPO_PACKAGES

## If second arg is provided, copy in the admin directory.
if [ -z $FULL ]; then
    echo "Skipping admin directory"
else
    echo "Exporting admin scripts to $ADMINROOT"
    rm -rf $ADMINROOT
    $BZR export $ADMINROOT $REPO_ADMIN
fi


cd $TMP_PKGROOT


## If second arg is provided, grab the Org daily
if [ -z $FULL ]; then
    echo "Not fetching Org daily from orgmode.org"
else
    echo "Fetching Org daily from orgmode.org"
    pkgname=`curl -s http://orgmode.org/elpa/|perl -ne 'push @f, $1 if m/(org-\d{8})\.tar/; END { @f = sort @f; print "$f[-1]\n"}'`
    wget -q http://orgmode.org/elpa/${pkgname}.tar -O ${pkgname}.tar
    if [ -f ${pkgname}.tar ]; then
	tar xf ${pkgname}.tar
	rm -f ${pkgname}.tar
	mv ${pkgname} org
    fi
fi

## Call `batch-make-archive' to generate archive-contents, the readme
## files, etc.
$EMACS -batch -l $REPO_ROOT_DIR/admin/archive-contents.el -f batch-make-archive

## Tar up the multi-file packages.
echo "Creating multi-file package tarballs in $TMP_PKGROOT"
for pt in *; do
    if [ -d $pt ]; then
	echo "Creating tarball $TMP_PKGROOT/$pt.tar"
	tar -cf $pt.tar $pt --remove-files
    fi
done

## Move the working directory to its final location
cd ..
rm -rf $PKGROOT-old
if [ -d $PKGROOT ]; then
    mv $PKGROOT $PKGROOT-old
fi
mv $TMP_PKGROOT $PKGROOT
rm -rf $PKGROOT-old

## If doing a full update, make a tarball of the entire archive.
if [ -z $FULL ]; then
    echo "Skipping archive tarball"
else
    echo "Exporting packages into $TARBALL (root = $TARBALL_ROOT)"
    cd $REPO_ROOT_DIR
    $BZR export --format=tgz --root=$TARBALL_ROOT $TARBALL $REPO_PACKAGES
fi

chmod -R a+rX $PKGROOT
echo "Update complete at" `/bin/date`
