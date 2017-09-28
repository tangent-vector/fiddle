/* skub.c */

#include "externals/lua/src/lua.h"

static void processFIle(
	char const* inputPath)
{
	

}

int main(
	int 	argc,
	char**	argv)
{
	/* TODO: parse options */

	char** argCursor = argv;
	char** argEnd = argv;

	char const* appName = "skub";
	if(argCursor != argEnd)
		appName = *argCursor++;

	while(argCursor != argEnd)
	{
		char const* inputPath = *argCursor++;
		processFile(inputPath);
	}

	return 0;
}

/*
We include the Lua implementation inline here,
so that our build script doesn't need to take
on any additional complexity.
*/

#include "external/lua/src/lapi.c"
#include "external/lua/src/lauxlib.c"
#include "external/lua/src/lbaselib.c"
#include "external/lua/src/lbitlib.c"
#include "external/lua/src/lcode.c"
#include "external/lua/src/lcorolib.c"
#include "external/lua/src/lctype.c"
#include "external/lua/src/ldblib.c"
#include "external/lua/src/ldebug.c"
#include "external/lua/src/ldo.c"
#include "external/lua/src/ldump.c"
#include "external/lua/src/lfunc.c"
#include "external/lua/src/lgc.c"
#include "external/lua/src/linit.c"
#include "external/lua/src/liolib.c"
#include "external/lua/src/llex.c"
#include "external/lua/src/lmathlib.c"
#include "external/lua/src/lmem.c"
#include "external/lua/src/loadlib.c"
#include "external/lua/src/lobject.c"
#include "external/lua/src/lopcodes.c"
#include "external/lua/src/loslib.c"
#include "external/lua/src/lparser.c"
#include "external/lua/src/lstate.c"
#include "external/lua/src/lstring.c"
#include "external/lua/src/lstrlib.c"
#include "external/lua/src/ltable.c"
#include "external/lua/src/ltablib.c"
#include "external/lua/src/ltm.c"
#include "external/lua/src/lundump.c"
#include "external/lua/src/lutf8lib.c"
#include "external/lua/src/lvm.c"
#include "external/lua/src/lzio.c"


