#!/bin/bash

# Fiddle Wraper Script
# ====================
#
# This file implements a wrapper script for invoking `fiddle`
# without needing to integrate it into a build system.
#
# The script will try to compile a `fiddle` executable
# on demand from `fiddle.c`, before forwarding the
# arguments it is given to that executable.
#
# The executable will be rebuilt whenever `fiddle.c` is
# newer than `fiddle`, so that users can pull a new copy
# of the Fiddle repository without having to delete
# their cached executable.
#
# The actual build is performed using `$CC`, which I hope
# is good enough for all simple scenarios. If you need to
# build Fiddle in a more complex way, then you probably
# need to build it yourself.
#
# Implementation
# --------------
#
# First, we are going to capture the directory path
# that contains this script into the `$FIDDLEPATH`
# variable.
#
pushd `dirname $0` > /dev/null
FIDDLEPATH=`pwd`
popd > /dev/null
#
# Next, we will check whether `fiddle.c` is not newer
# than the `fiddle` executable. If the executable
# doesn't exist, then this test will fail.
#
# Note: we do *not* currently check if this file
# (`fiddle.sh`) is newer than the executable, so
# if you plan to iterate on it, you might get
# surprised.
#
if [[ !( "$FIDDLEPATH/fiddle.c" -nt "$FIDDLEPATH/fiddle" ) ]]; then
#
# If we find that the executable is up to date, then
# we simply invoke it with the arguments that were
# passed to the script, and exit. This should be the
# steady state for any user of the script.
#
	"$FIDDLEPATH/fiddle" "$@"
	exit
fi
#
# Otherwise, we need to build a binary from `fiddle.c`.
# We are going to use whatever compiler the user
# has specified as `$CC`, or fall back to `cc` if
# that variable isn't defined.
#
: ${CC:="cc"}
#
# For the actual build step, we will move into the
# directory that contains the source code, so that
# any ancillary outputs that get dumped by the build
# step don't clutter up the user's code.
#
pushd "$FIDDLEPATH" > /dev/null
#
# Our actual build command is as simple as we can
# manage, in order to try to build cleanly on
# as many platforms as possible.
#
$CC fiddle.c -o fiddle
#
# Whether or not the build succeeds, restore the
# path to what it was.
popd > /dev/null
#
# And now that `fiddle` has hopefully been built,
# we will run it just like we would have if it was
# up to date in the first place.
# 
# This step should fail if the build step failed
# to produce an executable. This ideally won't happen
# to end users, but can easily happen to somebody
# who is hacking on the Fiddle implementation.
#
# This step is the last in teh script, so that its
# success or failure should determin the exit code
# of the script itself.
#
"$FIDDLEPATH/fiddle" "$@"
