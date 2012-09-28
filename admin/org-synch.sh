#!/bin/sh

# this script expects $1 to be the download directory and $2 to have org-synch.el

PATH="/bin:/usr/bin:/usr/local/bin:${PATH}"

pkgname=`curl -s http://orgmode.org/elpa/|perl -ne 'push @f, $1 if m/(org-\d{8}\.tar)/; END { @f = sort @f; print "$f[-1]\n"}'`

cd $1
wget -q http://orgmode.org/elpa/${pkgname} -O ${pkgname}-tmp
if [ -f ${pkgname}-tmp ]; then
    rm -f org*.tar
    mv ${pkgname}-tmp ${pkgname} && \
    emacs -batch -l $2/org-synch.el --eval "(org-synch \"${pkgname}\")"
fi
