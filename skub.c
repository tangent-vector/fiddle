/* skub.c */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "external/lua/src/lua.h"
#include "external/lua/src/lualib.h"
#include "external/lua/src/lauxlib.h"

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

	case ':':
		child->flavor = flavor | SKUB_STMT;
		cursor++;
		child->body.begin = cursor;
		for(;;)
		{
			switch(*cursor)
			{
			default:
				cursor++;
				continue;

			case '\n': case '\r':
				break;

			case 0:
				if(cursor == end)
				{
					break;
				}
				cursor++;
				continue;
			}
			break;
		}
		child->body.end = cursor;
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

		int c = *cursor;
		switch(c)
		{
		default:
			/* ordinary text: just keep going */
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

static char const* findMatch(
	char const* pattern,
	char const* begin,
	char const* end)
{
	size_t inputSize = end - begin;
	size_t patternSize = strlen(pattern);

	if(inputSize < patternSize)
		return NULL;

	end -= (patternSize - 1);

	char const* cursor = begin;
	while(cursor != end)
	{
		if(strncmp(cursor, pattern, patternSize) == 0)
			return cursor;

		cursor++;
	}
	return NULL;
}

static char const* findMatchInLine(
	char const* pattern,
	StringSpan 	line)
{
	return findMatch(pattern, line.begin, line.end);
}

static void addTextNode(
	SkubNode*** ioLink,
	char const* begin,
	char const* end)
{
	if(begin == end)
		return;

	SkubNode* node = allocateNode();
	node->text.begin = begin;
	node->text.end = end;

	*(*ioLink) = node;
	(*ioLink) = &node->next;
}

typedef struct SkubChunk SkubChunk;
struct SkubChunk
{
	StringSpan prefix;
	StringSpan code;
	StringSpan outputSpan;
	SkubNode* codeNode;

	SkubChunk*	next;
};

StringSpan readLine(char const** ioCursor, char const*end)
{
	StringSpan span;
	char const* cursor = *ioCursor;

	span.begin = cursor;

	for(;;)
	{
		span.end = cursor;
		if(cursor == end)
			break;

		int c = *cursor++;
		switch(c)
		{
		default:
			continue;

		case '\r': case '\n':
			{
				int d = *cursor;
				if( (c ^ d) == ('\r' ^ '\n'))
				{
					cursor++;
				}
			}
			break;
		}
		break;
	}

//	fprintf(stderr, "LINE: '%.*s'\n", (int) (span.end - span.begin), span.begin);	

	*ioCursor = cursor;
	return span;
}

static SkubChunk* allocateChunk()
{
	SkubChunk* chunk = (SkubChunk*) malloc(sizeof(SkubChunk));
	memset(chunk, 0, sizeof(SkubChunk));
	return chunk;
}

static SkubChunk* parseFile(
	char const* begin,
	char const* end)
{
	SkubChunk* chunks = NULL;
	SkubChunk** link = &chunks;

	char const* cursor = begin;
	char const* rawBegin = cursor;

	char const* openTagPattern = "[[[skub:";
	char const* closeTagPattern = "]]]";
	char const* endTagPattern = "[[[end]]]";


	SkubChunk* chunk = allocateChunk();
	*link = chunk;
	link = &chunk->next;

	chunk->prefix.begin = cursor;

	while(cursor != end)
	{

		while(cursor != end)
		{
			StringSpan line = readLine(&cursor, end);

			char const* openTag = findMatchInLine(openTagPattern, line);
			if(!openTag)
				continue;

			StringSpan codeSpan;
			codeSpan.begin = cursor;

			StringSpan closeSpan;

			// keep reading code lines until we see
			// a line with an closing tag
			for(;;)
			{
				if(cursor == end)
				{
					assert(0);
				}

				codeSpan.end = cursor;

				line = readLine(&cursor, end);
				char const* closeTag = findMatchInLine(closeTagPattern, line);
				if(closeTag)
				{
					closeSpan = line;
					closeSpan.end = cursor;
					break;
				}
			}

			// keep reading output lines until we
			// see a line with an ending tag

			StringSpan outputSpan;
			outputSpan.begin = cursor;
			for(;;)
			{
				if(cursor == end)
				{
					assert(0);
				}

				outputSpan.end = cursor;

				line = readLine(&cursor, end);
				char const* endTag = findMatchInLine(endTagPattern, line);
				if(endTag)
					break;
			}

			// Okay, we've found everything
			chunk->codeNode = processSpan(
				codeSpan.begin,
				codeSpan.end);

			chunk->prefix.end = closeSpan.end;
			chunk->code = codeSpan;
			chunk->outputSpan = outputSpan;

			chunk = allocateChunk();
			*link = chunk;
			link = &chunk->next;

			chunk->prefix.begin = outputSpan.end;
		}
	}

	chunk->prefix.end = end;
	chunk->code.begin = end;
	chunk->code.end = end;
	chunk->outputSpan.begin = end;
	chunk->outputSpan.end = end;
	chunk->codeNode = 0;

	return chunks;
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
	char const* suffix = ".out.cpp";
	size_t suffixSize = strlen(suffix);

	size_t inputSize = strlen(inputPath);

	size_t concatSize = inputSize + suffixSize;

	char* buffer = (char*) malloc(concatSize + 1);
	if(!buffer)
		return NULL;

	memcpy(buffer, inputPath, inputSize);
	memcpy(buffer + inputSize, suffix, suffixSize);
	buffer[concatSize] = 0;

	return buffer;
}

typedef struct SkubWriter
{
	char* cursor;
	char* begin;
	char* end;
} SkubWriter;

static void writeRaw(
	SkubWriter*	writer,
	char const* begin,
	char const* end)
{
	size_t len = end - begin;
	char* c = writer->cursor;
	char* e = writer->end;
	if(c + len > e)
	{
		char* b = writer->begin;

		size_t oldSize = e - b;
		size_t newSize = oldSize ? oldSize * 2 : 1024; 

		char* n = (char*) realloc(b, newSize);

		writer->begin = n;
		writer->end = n + newSize;
		c = n + (c - b);
	}

	memcpy(c, begin, len);
	writer->cursor = c + len;
}

static void writeRawT(
	SkubWriter*	writer,
	char const* begin)
{
	writeRaw(writer, begin, begin + strlen(begin));
}

static void emitRaw(
	SkubWriter*	writer,
	char const* begin,
	char const* end)
{
	if(begin == end)
		return;

	if(*begin == '\n')
	{
		writeRawT(writer, " _RAW(\"\\n\");");
	}

	if(begin == end)
		return;

	writeRawT(writer, " _RAW([==[");
	writeRaw(writer, begin, end);
	writeRawT(writer, "]==]);");
}

static void emitRawX(
	SkubWriter*	writer,
	char const* begin,
	char const* end)
{
	if(begin == end)
		return;

	writeRawT(writer, " _RAW([==[");

	char const* cursor = begin;
	while(cursor != end)
	{
		char c = *cursor++;
		switch(c)
		{
		default:
			{
				char buf[2] = { c, 0 };
				writeRawT(writer, buf);
			}
			break;

		case '\n':
			writeRawT(writer, "]==]);_RAW(\"\\n\");_RAW([==[");
			break;
		}

	}
	writeRawT(writer, "]==]);");
}

// Emit text as comments
static void emitRawComment(
	SkubWriter*	writer,
	char const* begin,
	char const* end)
{
	if(begin == end)
		return;

	writeRawT(writer, "--[==[");
	writeRaw(writer, begin, end);
	writeRawT(writer, "]==]");
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

static void emitNodeQ(
	SkubWriter*	writer,
	SkubNode*	node);

static void emitNodeS(
	SkubWriter*	writer,
	SkubNode*	node);

static void emitSpliceStmtNode(
	SkubWriter*	writer,
	SkubNode*	node);

static void emitChunks(
	SkubWriter* writer,
	SkubChunk*	chunks)
{
	SkubChunk* chunk = chunks;
	while(chunk)
	{
		emitRaw(writer, chunk->prefix.begin, chunk->code.begin);
		emitRawX(writer, chunk->code.begin, chunk->prefix.end);

		if(chunk->codeNode)
		{		
			emitSpliceStmtNode(writer, chunk->codeNode);
		}

		emitRawComment(writer, chunk->code.end, chunk->prefix.end);

		emitRawComment(writer, chunk->outputSpan.begin, chunk->outputSpan.end);

		chunk = chunk->next;
	}
}

static void emitQuoteExprNode(
	SkubWriter*	writer,
	SkubNode*	node)
{
	writeRawT(writer, "_QUOTE(function() ");
	emitNodeS(writer, node);
	writeRawT(writer, "end)");
}

static void emitQuoteStmtNode(
	SkubWriter*	writer,
	SkubNode*	node)
{
	emitNodeS(writer, node);
}

static void emitQuoteNode(
	SkubWriter*	writer,
	SkubNode*	node)
{
	StringSpan span = node->text;

	switch(node->flavor)
	{
	case SKUB_QUOTE_EXPR:
		{
			emitQuoteExprNode(writer, node);
		}
		break;

	case SKUB_QUOTE_STMT:
		{
			// Just a body -> raw statement
			emitQuoteStmtNode(writer, node);
		}
		break;

	default:
		fprintf(stderr, "skub: unexpected quote flavor 0x%x\n", node->flavor);
		assert(0);

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
	SkubWriter*	writer,
	SkubNode*	node)
{
	writeRawT(writer, " _SPLICE(");

	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		writeRaw(writer, cursor, nn->text.begin);

		// Embedded nodes represent quotes that
		// transition from Lua->C++
		emitQuoteNode(writer, nn);

		cursor = nn->text.end;
	}
	writeRaw(writer, cursor, node->body.end);

	writeRawT(writer, "); ");
}

static void emitSpliceStmtNode(
	SkubWriter*	writer,
	SkubNode*	node)
{
	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		writeRaw(writer, cursor, nn->text.begin);

		// Embedded nodes represent quotes that
		// transition from Lua->C++
		emitQuoteNode(writer, nn);

		cursor = nn->text.end;
	}
	writeRaw(writer, cursor, node->body.end);
}

// Emit a node that "escapes" from
// quoted text back into Lua
static void emitSpliceNode(
	SkubWriter*	writer,
	SkubNode*	node)
{
	StringSpan span = node->text;

	SkubNodeFlavor flavor = node->flavor;
	switch(flavor)
	{
	case SKUB_SPLICE_EXPR:
		{
			// Just args -> splice of expr
			emitSpliceExprNode(writer, node);
		}
		break;

	case SKUB_SPLICE_STMT:
		{
			// Just a body -> raw statement
			emitSpliceStmtNode(writer, node);
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
static void emitNodeQ(
	SkubWriter*	writer,
	SkubNode*	node)
{
	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		emitRaw(writer, cursor, nn->text.begin);

		emitQuoteNode(writer, nn);

		cursor = nn->text.end;
	}
	emitRaw(writer, cursor, node->body.end);
}

static void emitNodeS(
	SkubWriter*	writer,
	SkubNode*	node)
{
	char const* cursor = node->body.begin;
	for(SkubNode* nn = node->firstChild; nn; nn = nn->next)
	{
		emitRaw(writer, cursor, nn->text.begin);

		emitSpliceNode(writer, nn);

		cursor = nn->text.end;
	}
	emitRaw(writer, cursor, node->body.end);
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
//	fprintf(stderr, "rawCallback[[[\n");
	SkubWriter* writer = (SkubWriter*) lua_touserdata(L, lua_upvalueindex(1));

	size_t len = 0;
	char const* text = luaL_tolstring(L, 1, &len);

	writeRaw(writer, text, text + len);

//	fprintf(stderr, "%.*s]]]\n", (int)(len), text);

	return 0;
}

static int luaSpliceCallback(lua_State* L)
{
//	fprintf(stderr, "spliceCallback[[[\n");
	SkubWriter* writer = (SkubWriter*) lua_touserdata(L, lua_upvalueindex(1));

	size_t len = 0;
	char const* text = luaL_tolstring(L, 1, &len);

	writeRaw(writer, text, text + len);

//	fprintf(stderr, "%.*s]]]\n", (int)(len), text);

	return 0;
}

char const* gIncludePath;
char const* gOutputPath;

static void processFile(
	lua_State* 	L,
	char const* inputPath)
{
	StringSpan span;
	char const* outputPath;

	/* Parse file to generate a template,
	which we will evaluate using Lua */

	/* Determine path for output file to generate */
	if(gOutputPath)
	{
		outputPath = gOutputPath;
	}
	else
	{
		#if 0
		outputPath = pickOutputPath(inputPath);
		if(!outputPath)
		{
			fprintf(stderr,
				"skub: cannot pick output path based on input path '%s'\n"
				"      skub expects input path of the form '*.skub'\n",
				inputPath);
			return;
		}		
		#else
		outputPath = inputPath;
		#endif
	}

//	fprintf(stderr, "skubbing '%s' -> '%s'\n", inputPath, outputPath);

	/* Slurp whole file in at once */
	span = readFile(inputPath);
	if(!span.begin)
	{
		return;		
	}

	SkubChunk* chunks = parseFile(span.begin, span.end);

	SkubWriter writer = { 0, 0, 0 };
	writeRawT(&writer,
		"local _RAW, _SPLICE = ...; ");

	emitChunks(&writer, chunks);
	char const* empty = "";
	writeRaw(&writer, empty, empty + 1);

	StringSpan processed;
	processed.begin = writer.begin;
	processed.end = writer.cursor - 1;

	{
		FILE* dump = fopen("dump.lua", "w");	
		fprintf(dump, "%.*s\n", (int)(processed.end - processed.begin), processed.begin);
		fclose(dump);
//		exit(0);
	}

	char* luaFileName = (char*)
		malloc(strlen(inputPath) + 2);
	luaFileName[0] = '@';
	memcpy(luaFileName + 1, inputPath, strlen(inputPath) + 1);

	StringSpan readerState = processed;
	int err = lua_load(
		L,
		&luaReadCallback,
		(void*) &readerState,
		luaFileName,
		0);
	if(err != LUA_OK)
	{
		char const* message = lua_tostring(L, -1);
		fprintf(stderr, "skub: %s\n", message);
		exit(1);
	}

	SkubWriter outputWriter = { 0, 0, 0 };


	lua_pushlightuserdata(L, &outputWriter);
	lua_pushcclosure(L, &luaRawCallback, 1);

	lua_pushlightuserdata(L, &outputWriter);
	lua_pushcclosure(L, &luaSpliceCallback, 1);

	err = lua_pcall(L, 2, 0, 0);
	if(err != LUA_OK)
	{
		char const* message = lua_tostring(L, -1);
		fprintf(stderr, "skub: %s\n", message);		
		exit(1);
	}
	writeRaw(&outputWriter, empty, empty + 1);

	StringSpan outputText;
	outputText.begin = outputWriter.begin;
	outputText.end = outputWriter.cursor - 1;

	FILE* output = fopen(outputPath, "w");
	if(!output)
	{
		fprintf(stderr,
			"skub: cannot open '%s' for writing\n",
			outputPath);
		return;
	}
	fprintf(output, "%.*s", (int) (outputText.end - outputText.begin), outputText.begin);

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

char const* readArg(
	char const* opt,
	char*** ioArgCursor,
	char** argEnd)
{
	if(*ioArgCursor == argEnd)
	{
		fprintf(stderr, "skub: expected argument for option '%s'\n", opt);
		exit(1);					
	}
	else
	{
		return *(*ioArgCursor)++;
	}
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

	char** writeCursor = argv;
	while(argCursor != argEnd)
	{
		char const* arg = *argCursor++;
		if(arg[0] == '-')
		{
			if(arg[1] == '-' && arg[2] == 0)
			{
				break;
			}
			else if(arg[1] == 'I')
			{
				char const* path = arg + 2;
				if(*path == 0)
				{
					path = readArg(arg, &argCursor, argEnd);
				}

				gIncludePath = path;
			}
			else if(strcmp(arg, "-o") == 0)
			{
				gOutputPath = readArg(arg, &argCursor, argEnd);
			}
			else
			{
				fprintf(stderr, "skub: unknown option '%s'\n", arg);
				exit(1);
			}

		}
		else
		{
			*writeCursor++ = (char*) arg;
		}
	}
	while(argCursor != argEnd)
	{
		*writeCursor++ = *argCursor++;
	}

	argEnd = argv + (writeCursor - argv);
	argCursor = argv;


	lua_State* L = lua_newstate(&allocatorForLua, 0);
	if(!L)
	{
		fprintf(stderr, "skub: failed to create Lua state\n");
		return 1;
	}

	luaL_openlibs(L);

	if(gIncludePath)
	{
		lua_getglobal(L, "package");
		lua_pushstring(L, gIncludePath);
		lua_pushstring(L, "/?.lua");
		lua_concat(L, 2);
		lua_setfield(L, -2, "path");		
	}



	while(argCursor != argEnd)
	{
		char const* inputPath = *argCursor++;
		processFile(L, inputPath);
	}

	exit(0);

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


