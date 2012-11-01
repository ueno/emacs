#!/bin/sh -x

batchmode=no

export LANG=C
case "$1" in
    "--batch") batchmode=yes ;;
esac

# Return on STDOUT the files that don't seem to have the needed copyright
# notice, or that have a copyright notice that looks suspicious.
copyright_notices () {
    find . -name '*.el' -print0 |
        xargs -0 grep -L 'Free Software Foundation, Inc' |
        grep -v '.-\(pkg\|autoloads\)\.el$'

    find . -name '*.el' -print |
        while read f; do
            sed -n -e '/[Cc]opyright.*, *[1-9][-0-9]*,\?$/N' \
                -e '/Free Software Foundation/d' \
                -e "s|^\\(.*[Cc]opyright\\)|$(echo $f | tr '|' '_'):\\1|p" "$f"
        done
}

# Send an email to warn about a problem.
# Takes the body on STDIN and the subject as argument.
signal_error () {
    title="$*"
    if [ "no" = "$batchmode" ]; then
        cat -
        echo "Error: $title"
    else
        mx_gnu_org="$(host -t mx gnu.org | sed 's/.*[ 	]//')"
        (sleep 5; echo "HELO elpa.gnu.org"
         sleep 1; echo "MAIL FROM: <elpa@elpa.gnu.org>"
         sleep 1; echo "RCPT TO: <emacs-elpa-diffs@gnu.org>"
         sleep 1; echo "DATA"
         sleep 1; cat <<ENDDOC
From: ELPA update <elpa@elpa.gnu.org>
To: emacs-elpa-diffs@gnu.org
Subject: $title

ENDDOC
         cat -
         echo "."; sleep 1) | telnet "$mx_gnu_org" smtp
    fi
}

check_copyright () {
    base="copyright_exceptions"
    (cd packages; copyright_notices) >"$base.new"
    if [ -r "$base.old" ] &&
       ! diff "$base.old" "$base.new" >/dev/null;
    then
        diff -u "$base.old" "$base.new" |
            signal_error "Copyright notices changed"
        exit 1
    else
        mv "$base.new" "$base.old"
    fi
}

cd ~elpa/build

(cd ~elpa/elpa; bzr up)

check_copyright

rm -rf archive                  # In case there's one left over!
make archive-full >make.log 2>&1 || {
    signal_error "make archive-full failed" <make.log
    exit 1
}
(cd archive
 tar zcf emacs-packages-latest.tgz packages)
(cd ~elpa
 # Not sure why we have `staging-old', but let's keep it for now.
 rm -rf staging-old
 cp -a staging staging-old
 # Move new files into place but don't throw out old package versions.
 mv build/archive/packages/* staging/packages/
 mv build/archive/* staging/ 2>/dev/null
 rm -rf build/archive)

# Make the HTML files.
(cd ~elpa/staging/packages
 emacs --batch -l ~elpa/build/admin/archive-contents.el \
       --eval '(batch-html-make-index)')

# "make archive-full" already does fetch the daily org build.
#admin/org-synch.sh ~elpa/staging/packages ~elpa/build/admin
