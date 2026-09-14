#!/bin/sh

set -e  # Exit immediately if a command exits with a non-zero status

echo "Adding libtools."
libtoolize --force --automake

echo "Building macros."
aclocal -I m4 $ACLOCAL_FLAGS

echo "Building config header."
autoheader

echo "Building makefiles."
automake   --add-missing

echo "Building configure."
autoconf

echo 'run "configure; make"'
