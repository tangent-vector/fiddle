#!/bin/bash

pushd `dirname $0` > /dev/null
SKUBPATH=`pwd`
popd > /dev/null


# Determine whcih is newer, between skub.c and skub
if [[ !( "$SKUBPATH/skub.c" -nt "$SKUBPATH/skub" ) ]]; then
	# If the binary is up to date, then just run it and move on
	"$SKUBPATH/skub" "$@"
	exit
fi

# Otherwise, try to build a binary from skub.c
: ${CC:="cc"}

pushd "$SKUBPATH" > /dev/null
$CC skub.c -o skub
popd > /dev/null

# And now that it has (hopefully) been built, we run it
"$SKUBPATH/skub" "$@"
