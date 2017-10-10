/* skub.c */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "external/lua/src/lua.h"
#include "external/lua/src/lualib.h"

typedef struct StringSpan
{
	char const* begin;
	char const* end;
} StringSpan;

StringSpan emptyStringSpan()
{
	StringSpan span = { 0 , 0 };
	return span;
}

StringSpan readFile(char const* path)
{
	size_t size = 0;
	FILE* file = NULL;
	char* buffer = NULL;
	StringSpan span = emptyStringSpan();

	/* Try to open the file */
	file = fopen(path, "rb");
	if(!file)
	{
		fprintf(stderr,
			"skub: failed to open '%s' for reading\n",
			path);
		return span;
	}

	/* Use `ftell` to determine size of file */
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Allocate buffer for result */
	buffer = (char*) malloc(size + 1);
	if(!buffer)
	{
		fprintf(stderr,
			"skub: memory allocation failed\n");
		fclose(file);
		return span;
	}

	if(fread(buffer, size, 1, file) != 1)
	{
		fprintf(stderr,
			"skub: failed to read from '%s'\n",
			path);
		fclose(file);
		return span;
	}

	fclose(file);

	buffer[size] = 0;
	span.begin = buffer;
	span.end = buffer + size;
	return span;
}

typedef struct SkubNode SkubNode;

typedef enum SkubNodeFlavor
{
	SKUB_SPLICE = 0x0,
	SKUB_QUOTE  = 0x2,
	SKUB_EXPR   = 0x0,
	SKUB_STMT   = 0x1,

	SKUB_SPLICE_EXPR = SKUB_SPLICE | SKUB_EXPR,
	SKUB_SPLICE_STMT = SKUB_SPLICE | SKUB_STMT,
	SKUB_QUOTE_EXPR  = SKUB_QUOTE | SKUB_EXPR,
	SKUB_QUOTE_STMT  = SKUB_QUOTE | SKUB_STMT,
} SkubNodeFlavor;

struct SkubNode
{
	SkubNodeFlavor flavor;

	/* Full (raw) text of the node */
	StringSpan text;

	/* The option `{}`-enclosed part */
	StringSpan body;

	/* A linked list connecting child nodes
		that were parsed inside the body. */
	SkubNode*	firstChild;
	SkubNode*	next;
};

static SkubNode* allocateNode()
{
	SkubNode* node = (SkubNode*) malloc(sizeof(SkubNode));
	memset(node, 0, sizeof(SkubNode));
	return node;
}

static int isNameChar(int c)
{
	return ((c >= 'a') && (c <= 'z'))
		|| ((c >= 'A') && (c <= 'Z'))
		|| ((c >= '0') && (c <= '9'))
		|| (c == '_');
}

static void readNodeBody(
	SkubNode* 		node,
	char const** 	ioCursor,
	char const* 	end,
	int 			openCount,
	char 			openDelim,
	char 			closeDelim);

static void readDelimetedBody(
	SkubNode* 		node,
	char const** 	ioCursor,
	char const* 	end,
	char 			openDelim,
	char 			closeDelim)
{
	char const* cursor = *ioCursor;
	int openCount = 0;
	while(*cursor == openDelim)
	{
		openCount++;
		cursor++;
	}
	readNodeBody(
		node,
		&cursor,
		end,
		openCount,
		openDelim,
		closeDelim);
	*ioCursor = cursor;
}

static void readChildNode(
	SkubNode*** 	ioChildLink,
	char const**	ioCursor,
	char const*		end,
	SkubNodeFlavor 	flavor)
{
	char const* cursor = *ioCursor;

	SkubNode* child = allocateNode();
	child->text.begin = cursor;

	*(*ioChildLink) = child;
	(*ioChildLink) = &child->next;

	/* skip the `$` or `` ` ``*/
	cursor++;

	/* Look at next character to decide
	what to do */

	switch(*cursor)
	{
	case '(':
		child->flavor = flavor | SKUB_EXPR;
		readDelimetedBody(
			child,
			&cursor,
			end,
			'(',
			')');
		break;

	case '{':
		child->flavor = flavor | SKUB_STMT;
		readDelimetedBody(
			child,
			&cursor,
			end,
			'{',
			'}');
		break;

	/* TODO: handle a `:` here, and read
	a body that spans to the end of the line */

	default:
		assert(0);
		break;
	}

	child->text.end = cursor;

	*ioCursor = cursor;
}

static void readNodeBody(
	SkubNode* 		node,
	char const** 	ioCursor,
	char const* 	end,
	int 			openCount,
	char 			openDelim,
	char 			closeDelim)
{
	char const* cursor = *ioCursor;
	node->body.begin = cursor;

	SkubNode** childLink = &node->firstChild;
	int nesting = 0;
	for(;;)
	{
		int c = *cursor;
		switch(c)
		{
		default:
			/* ordinary text: just keep going */
			cursor++;
			break;

		case 0:
			/* possible end of input */
			if(cursor == end)
			{
				/* Yep, we are at the end */
				if(openCount >= 1)
				{
					fprintf(stderr, "skub: unclosed '{' at end of file\n");
				}
				node->body.end = cursor;
				*ioCursor = cursor;
				return;
			}
			/* Not at the end. This is surprising,
			   but we will treat it as an ordinary
			   character */
			cursor++;
			break;

		case '{':
		case '(':
			if(c != openDelim)
			{
				cursor++;
				continue;
			}
			/* increase nesting depth */
			cursor++;
			nesting++;
			break;

		case '}':
		case ')':
			if(c != closeDelim)
			{
				cursor++;
				continue;
			}
			/* Potential closer */
			if(openCount > 1)
			{
				// count the number of closers
				char const* cc = cursor;
				int closeCount = 0;
				while(*cc == '}')
				{
					closeCount++;
					cc++;
				}

				if(closeCount >= openCount)
				{
					node->body.end = cursor;
					cursor += openCount;
					*ioCursor = cursor;
					return;
				}
			}
			else if(
				openCount == 1
				&& nesting == 0)
			{
				node->body.end = cursor;
				cursor++;
				*ioCursor = cursor;
				return;
			}
			cursor++;
			nesting--;
			break;

		case '$':
			readChildNode(&childLink, &cursor, end, SKUB_SPLICE);
			break;

		case '`':
			/*	This marks the start of a child node. */
			readChildNode(&childLink, &cursor, end, SKUB_QUOTE);
			break;
		}
	}

}

static SkubNode* processSpan(
	char const* begin,
	char const* end)
{
	SkubNode* node = allocateNode();
	node->text.begin = begin;
	node->text.end = end;

	char const* cursor = begin;
	readNodeBody(
		node,
		&cursor,
		end,
		0,
		0,
		0);

	assert(cursor == end);

	return node;
}

static int stringEndsWith(
	char const*	str,
	char const*	suffix)
{
	size_t strSize = strlen(str);
	size_t suffixSize = strlen(suffix);
	if(strSize < suffixSize)
		return 0;		

	return strcmp(
		str + strSize - suffixSize,
		suffix) == 0;
}

static char* pickOutputPath(
	char const* inputPath)
{
	size_t inputSize = strlen(inputPath);
	char const* skubExt = ".skub";
	size_t prefixSize = inputSize - strlen(skubExt);

	if(!stringEndsWith(inputPath, skubExt))
		return NULL;

	char* buffer = (char*) malloc(prefixSize + 1);
	if(!buffer)
		return NULL;

	memcpy(buffer, inputPath, prefixSize);
	buffer[prefixSize] = 0;

	return buffer;
}

static void writeRaw(
	lua_State* 	L,
	int*		ioCount,
	char const* begin,
	char const* end)
{
	lua_pushlstring(L, begin, end-begin);
	(*ioCount)++;
}

static void writeRawT(
	lua_State* 	L,
	int*		ioCount,
	char const* begin)
{
	writeRaw(L, ioCount, begin, begin + strlen(begin));
}

static void emitRaw(
	lua_State* 	L,
	int*		ioCount,
	char const* begin,
	char const* end)
{
	if(begin == end)
		return;

	writeRawT(L, ioCount, " _RAW([==[");
	if(*begin == '\n')
	{
		writeRawT(L, ioCount, "\n");
	}
	writeRaw(L, ioCount, begin, end);
	writeRawT(L, ioCount, "]==]);");
}

static int isEmpty(StringSpan span)
{
	return span.begin == span.end;
}

enum {
	HAS_NAME = 0x1,
	HAS_ARGS = 0x2,
	HAS_BODY = 0x4,
};

static void emitNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node);

static void emitQuoteExprNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	writeRawT(L, ioCount, "_QUOTE(function() ");
//	writeRaw(L, ioCount, node->args.begin, node->args.end);
	emitNode(L, ioCount, node);
	writeRawT(L, ioCount, "end)");
}

static void emitQuoteStmtNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	emitNode(L, ioCount, node);
}

static void emitQuoteNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	StringSpan span = node->text;

	switch(node->flavor)
	{
	case SKUB_QUOTE_EXPR:
		{
			emitQuoteExprNode(L, ioCount, node);
		}
		break;

	case SKUB_QUOTE_STMT:
		{
			// Just a body -> raw statement
			emitQuoteStmtNode(L, ioCount, node);
		}
		break;

	default:
		fprintf(stderr, "skub: unexpected quote flavor 0x%x\n", node->flavor);

		fprintf(stderr, "text(%.*s)",
			(int)(node->text.end - node->text.begin),
			node->text.begin);

		exit(1);
		break;
	}

}

// Here we are emitting an expression
// to be spliced into the output...
static void emitSpliceExprNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	writeRawT(L, ioCount, " _SPLICE(");

	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		writeRaw(L, ioCount, cursor, nn->text.begin);

		// Embedded nodes represent quotes that
		// transition from Lua->C++
		emitQuoteNode(L, ioCount, nn);

		cursor = nn->text.end;
	}
	writeRaw(L, ioCount, cursor, node->body.end);

	writeRawT(L, ioCount, "); ");
}

static void emitSpliceStmtNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		writeRaw(L, ioCount, cursor, nn->text.begin);

		// Embedded nodes represent quotes that
		// transition from Lua->C++
		emitQuoteNode(L, ioCount, nn);

		cursor = nn->text.end;
	}
	writeRaw(L, ioCount, cursor, node->body.end);
}

// Emit a node that "escapes" from
// quoted text back into Lua
static void emitEscapeNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	StringSpan span = node->text;

	SkubNodeFlavor flavor = node->flavor;
	switch(flavor)
	{
	case SKUB_SPLICE_EXPR:
		{
			// Just args -> splice of expr
			emitSpliceExprNode(L, ioCount, node);
		}
		break;

	case SKUB_SPLICE_STMT:
		{
			// Just a body -> raw statement
			emitSpliceStmtNode(L, ioCount, node);
		}
		break;

	default:
		fprintf(stderr, "skub: unexpected error 0x%x\n", flavor);

		fprintf(stderr, "text(%.*s)",
			(int)(node->text.end - node->text.begin),
			node->text.begin);

		exit(1);
		break;
	}
}

// Here we are emitting top-level text,
// which may have embedded escapes.
static void emitNode(
	lua_State* 	L,
	int*		ioCount,
	SkubNode*	node)
{
	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		emitRaw(L, ioCount, cursor, nn->text.begin);

		emitEscapeNode(L, ioCount, nn);

		cursor = nn->text.end;
	}
	emitRaw(L, ioCount, cursor, node->body.end);
}

static char const* luaReadCallback(
	lua_State* L,
	void* userData,
	size_t*	size)
{
	StringSpan* spanPtr = (StringSpan*) userData;
	StringSpan span = *spanPtr;

	spanPtr->begin = NULL;
	spanPtr->end = NULL;

	*size = span.end - span.begin;
	return span.begin;
}

static int luaRawCallback(lua_State* L)
{
	FILE* file = (FILE*) lua_touserdata(L, lua_upvalueindex(1));

	size_t len = 0;
	char const* text = lua_tolstring(L, 1, &len);
	fprintf(file, "%.*s", (int)(len), text);

	return 0;
}

static int luaSpliceCallback(lua_State* L)
{
	FILE* file = (FILE*) lua_touserdata(L, lua_upvalueindex(1));

	size_t len = 0;
	char const* text = lua_tolstring(L, 1, &len);
	fprintf(file, "%.*s", (int)(len), text);

	return 0;
}

static void processFile(
	lua_State* 	L,
	char const* inputPath)
{
	StringSpan span;
	char* outputPath;

	/* Parse file to generate a template,
	which we will evaluate using Lua */

	/* Determine path for output file to generate */
	outputPath = pickOutputPath(inputPath);
	if(!outputPath)
	{
		fprintf(stderr,
			"skub: cannot pick output path based on input path '%s'\n"
			"      skub expects input path of the form '*.skub'\n",
			inputPath);
		return;
	}

//	fprintf(stderr, "skubbing '%s' -> '%s'\n", inputPath, outputPath);

	/* Slurp whole file in at once */
	span = readFile(inputPath);
	if(!span.begin)
	{
		return;		
	}

	SkubNode* rootNode = processSpan(span.begin, span.end);

	int count = 0;

	writeRawT(L, &count,
		"local _RAW, _SPLICE = ...; "
		"local _ctxt = {}"
		"local function _QUOTE(f) "
			"local _saved_raw = _RAW; "
			"local _saved_splice = _SPLICE; "
			"local _strs = {}; "
			"_RAW = function(s) table.insert(_strs, tostring(s)); end; "
			"_SPLICE = function(s) table.insert(_strs, tostring(s)); end; "
			"f()"
			"_RAW = _saved_raw; "
			"_SPLICE = _saved_splice; "
			"print('QQ', table.concat(_strs), 'qq'); "
			"return table.concat(_strs); "
		"end; ");

	emitNode(L, &count, rootNode);

	lua_concat(L, count);

	size_t len;
	StringSpan processed;
	processed.begin = lua_tolstring(L, 1, &len);
	processed.end = processed.begin + len;

//	fprintf(stderr, "X{{{{%.*s}}}}\n", (int)(len), processed.begin);

	StringSpan readerState = processed;
	int err = lua_load(
		L,
		&luaReadCallback,
		(void*) &readerState,
		inputPath,
		0);
	if(err != LUA_OK)
	{
		char const* message = lua_tostring(L, -1);
		fprintf(stderr, "skub: %s\n", message);
		exit(1);
	}

	FILE* output = fopen(outputPath, "w");
	if(!output)
	{
		fprintf(stderr,
			"skub: cannot open '%s' for writing\n",
			outputPath);
		return;
	}

	lua_pushlightuserdata(L, output);
	lua_pushcclosure(L, &luaRawCallback, 1);

	lua_pushlightuserdata(L, output);
	lua_pushcclosure(L, &luaSpliceCallback, 1);

	err = lua_pcall(L, 2, 0, 0);
	if(err != LUA_OK)
	{
		char const* message = lua_tostring(L, -1);
		fprintf(stderr, "skub: %s\n", message);		
		exit(1);
	}

	fclose(output);

	// 

}

static void* allocatorForLua(
	void* userData,
	void* ptr,
	size_t oldSize,
	size_t newSize)
{
	return realloc(ptr, newSize);
}

int main(
	int 	argc,
	char**	argv)
{
	/* TODO: parse options */

	char** argCursor = argv;
	char** argEnd = argv + argc;

	char const* appName = "skub";
	if(argCursor != argEnd)
		appName = *argCursor++;

	lua_State* L = lua_newstate(&allocatorForLua, 0);
	if(!L)
	{
		fprintf(stderr, "skub: failed to create Lua state\n");
		return 1;
	}

	luaL_openlibs(L);

	while(argCursor != argEnd)
	{
		char const* inputPath = *argCursor++;
		processFile(L, inputPath);
	}

	lua_close(L);

	return 0;
}

/*
We include the Lua implementation inline here,
so that our build script doesn't need to take
on any additional complexity.
*/

/* TODO: Figure out when we can enable this...
*/
#define LUA_USE_POSIX

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

