# Fiddle Makefile
# ===============
#
# We are going to use a pretty simple `Makefile`,
# just because we don't expect our build to take
# long, and complex dependency management would
# just be overkill.
# 
# Let's set some basic flags. We are always doing
# debug builds for now, since the tool is still
# in active development.
#
CFLAGS 		 := -g
LDFLAGS		 :=
#
# Next we do some `make`-related incantations to
# identify that our `clean` target doesn't name
# a file.
# 
.PHONY : clean
#
# We'll set up some variables to represent all
# the files out output should depend on. This
# is the source in `fiddle.c` itself, plus the
# code of the Lua implementation.
#
SOURCES := fiddle.c external/lua/src/*.c
HEADERS := external/lua/src/*.h
#
# Our default rule is also the main one: it
# just builds the `fiddle` executable itself.
#
fiddle: $(SOURCES) $(HEADERS)
	$(CC) $(LDFLAGS) -o $@ $(CFLAGS) fiddle.c
#
# We also add a `clean` rule, even though it
# is not any simpler for hte user than just
# deleting the binary manually.
#
clean:
	rm ./fiddle
