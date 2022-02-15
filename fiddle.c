	/* fiddle.c

Fiddle Implementation
=====================

This file provides the implementation of the `fiddle`
command-line tool. All of the code is in this file, with
the exception of the Lua implementation, which this
file slurps in via `#include`.

Dependencies
------------

### C Standard Library

We will use the C standard library for file input/outut,
string manipulation, and assertions:

	*/
	#include <assert.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	/*

### Lua

We will use Lua to provide the meta-language for our templates,
and also users to put code in stand-alone `.lua` files that
they import for use in templates.

	*/
	#include "external/lua/src/lua.h"
	#include "external/lua/src/lualib.h"
	#include "external/lua/src/lauxlib.h"
	/*

Utilities
---------

This section implementations utility code that we need to
do the job

	*/

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
			"fiddle: failed to open '%s' for reading\n",
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
			"fiddle: memory allocation failed\n");
		fclose(file);
		return span;
	}

	if(fread(buffer, size, 1, file) != 1)
	{
		fprintf(stderr,
			"fiddle: failed to read from '%s'\n",
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


typedef enum TemplateNodeFlavor
{
	kTemplateNodeFlavor_Text,
	kTemplateNodeFlavor_TextAndNewline,
	kTemplateNodeFlavor_Escape,
	kTemplateNodeFlavor_EscapeExpr,
} TemplateNodeFlavor;

typedef struct TemplateNode TemplateNode;
struct TemplateNode
{
	TemplateNodeFlavor flavor;

	/* Full (raw) text of the node */
	StringSpan text;

	TemplateNode*	firstChild;
	TemplateNode*	next;
};

static TemplateNode* allocateNode()
{
	TemplateNode* node = (TemplateNode*) malloc(sizeof(TemplateNode));
	memset(node, 0, sizeof(TemplateNode));
	return node;
}

static char const* isEscapeLine(
	StringSpan line)
{
	char const* cursor = line.begin;
	char const* end = line.end;

	for(;;)
	{
		if(cursor == end)
			return 0;

		switch(*cursor)
		{
		case ' ': case '\t':
			cursor++;
			continue;

		default:
			break;
		}
		break;
	}

	if(*cursor == '%')
		return cursor + 1;
	return 0;
}

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

static TemplateNode* addTextNode(
	TemplateNode*** ioLink,
	char const* begin,
	char const* end)
{
	TemplateNode* node = allocateNode();
	node->flavor = kTemplateNodeFlavor_Text;
	node->text.begin = begin;
	node->text.end = end;

	*(*ioLink) = node;
	(*ioLink) = &node->next;

	return node;
}

static TemplateNode* maybeAddTextNode(
	TemplateNode*** ioLink,
	char const* begin,
	char const* end)
{
	if(begin == end)
		return 0;

	return addTextNode(ioLink, begin, end);
}

static int gErrorCount = 0;
static void fiddle_error(char const* message, ...)
{
	gErrorCount++;
	va_list args;
	va_start(args, message);
	fprintf(stderr, "fiddle: error: ");
	vfprintf(stderr, message, args);
	fprintf(stderr, "\n");
	va_end(args);
}

typedef enum TemplateParseState
{
	kTemplateParseState_Default,
	kTemplateParseState_InExprEscape,
} TemplateParseState;

static TemplateNode* parseTemplate(
	StringSpan 	templateLines,
	StringSpan 	prefix)
{
	TemplateNode* nodes = 0;
	TemplateNode** link = &nodes;

	TemplateNode* spliceNode = 0;

	TemplateParseState state = kTemplateParseState_Default;

	size_t prefixSize = prefix.end - prefix.begin;
	char const* cursor = templateLines.begin;
	char const* end = templateLines.end;
	for(;;)
	{
		if(cursor == end)
			break;

		StringSpan line = readLine(&cursor, end);
		line.begin += prefixSize;
		/*

		First, we'll check if this line is a full
		on lua code line.

		*/
		char const* escapeBegin = isEscapeLine(line);
		if(escapeBegin)
		{
			switch(state)
			{
			case kTemplateParseState_Default:
				{
					TemplateNode* node = allocateNode();
					node->flavor = kTemplateNodeFlavor_Escape;
					node->text.begin = escapeBegin;
					node->text.end = line.end;

					*link = node;
					link = &node->next;
				}
				break;

			case kTemplateParseState_InExprEscape:
				fiddle_error("unterminated escape\n");
				return 0;
			}
		}
		else
		{
			char const* cc = line.begin;
			char const* spanBegin = cc;
			for(;;)
			{
				if(cc == line.end)
					break;

				char const* spanEnd = cc;

				int c = *cc++;
				switch(state)
				{
				case kTemplateParseState_Default:
					if(c == '$')
					{
						if(cc != line.end
							&& *cc == '{')
						{
							cc++;
							maybeAddTextNode(&link, spanBegin, spanEnd);
							/*

							We create a node to represent
							the splice.

							*/
							spliceNode = allocateNode();
							spliceNode->flavor = kTemplateNodeFlavor_EscapeExpr;

							*link = spliceNode;
							link = &spliceNode->firstChild;

							spanBegin = cc;
							state = kTemplateParseState_InExprEscape;
						}
					}
					break;

				case kTemplateParseState_InExprEscape:
					if(*cc == '}')
					{
						spanEnd = cc;
						cc++;
						maybeAddTextNode(&link, spanBegin, spanEnd);

						link = &spliceNode->next;
						spliceNode = 0;

						spanBegin = cc;
						state = kTemplateParseState_Default;
					}
					break;
				}
			}
			addTextNode(&link, spanBegin, line.end)->flavor = kTemplateNodeFlavor_TextAndNewline;
		}
	}

	return nodes;
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

/*

A `Chunk` represents the large-scale structure of
an input file, which is composed of raw text spans
and templates.

A template file will be parsed into a single chunk,
while a source file with embedded tempaltes may
have one or more chunks.

*/
typedef struct Chunk Chunk;
struct Chunk
{
	StringSpan prefix;
	StringSpan linePrefix;
	StringSpan code;
	StringSpan outputSpan;
	TemplateNode* codeNode;

	Chunk*	next;
};

static Chunk* allocateChunk()
{
	Chunk* chunk = (Chunk*) malloc(sizeof(Chunk));
	memset(chunk, 0, sizeof(Chunk));
	return chunk;
}

StringSpan commonPrefix(
	StringSpan left,
	StringSpan right)
{
	char const* ll = left.begin;
	char const* rr = right.begin;

	StringSpan prefix;
	prefix.begin = ll;

	for(;;)
	{
		if(ll == left.end)
			break;
		if(rr == right.end)
			break;

		if(*ll != *rr)
			break;

		ll++;
		rr++;
	}

	prefix.end = ll;
	return prefix;
}

static Chunk* parseTemplateFile(
	char const* begin,
	char const* end)
{
	Chunk* chunk = allocateChunk();

	StringSpan code;
	code.begin = begin;
	code.end = end;

	StringSpan prefix = emptyStringSpan();

	chunk->codeNode = parseTemplate(
		code,
		prefix);

	return chunk;
}

/*

The parsing of a source file with embedded templates
uses a little state machine, so that we can issue
useful error messages when people leave out directives
(or misspell them, etc.).

*/
typedef enum SourceFileParseState SourceFileParseState;
enum SourceFileParseState
{
	kSourceFileParseState_Initial,
	kSourceFileParseState_Default,
	kSourceFileParseState_InTemplateCode,
	kSourceFileParseState_InTemplateOutput,
};
/*

The `parseSourceFile()` function is responsible for
reading the text of a source file (possibly with embedded
templates) into a sequence of "chunks".

*/
static Chunk* parseSourceFile(
	char const* begin,
	char const* end)
{
	/*

	We are going to build up a linked list of chunks
	of the file.

	*/
	Chunk* chunks = NULL;
	Chunk** link = &chunks;
	/*

	We will use the variable `cursor` to track
	our progress through the input text.

	*/
	SourceFileParseState state = kSourceFileParseState_Initial;
	char const* cursor = begin;
	/*

	There will always be at least one chunk,
	even in a file with no templates, so
	we'll start the first one here.

	*/
	Chunk* chunk = allocateChunk();
	chunk->prefix.begin = cursor;

	*link = chunk;
	link = &chunk->next;
	/*

	The patterns we are looking for on the lines
	are fixed.

	*/
	char const* openTagPattern 	= "FIDDLE TEMPLATE";
	char const* closeTagPattern = "FIDDLE OUTPUT";
	char const* endTagPattern	= "FIDDLE END";
	/*

	With the preliminaries out of the way, we are
	going to start scaning through the file.

	*/
	for(;;)
	{
		/*
	
		If we've reached the end of the input text,
		then we are done parsing, and need to bail
		out of this loop.

		*/
		if(cursor == end)
			break;
		/*

		Otherwise, we'll read in the next line of
		the file, and see if it looks like the start
		of a template.

		*/
		StringSpan line = readLine(&cursor, end);
		char const* openLoc = findMatchInLine(openTagPattern, line);
		if(openLoc)
		{
			/*

			

			*/
			switch(state)
			{
			case kSourceFileParseState_Initial:
			case kSourceFileParseState_Default:
				chunk->code.begin = cursor;
				chunk->linePrefix = line;
				state = kSourceFileParseState_InTemplateCode;
				break;

			case kSourceFileParseState_InTemplateCode:
			case kSourceFileParseState_InTemplateOutput:
				fprintf(stderr, "fiddle: error: starting new template without ending previous one\n");
				return 0;
			}

			continue;
		}

		char const* closeLoc = findMatchInLine(closeTagPattern, line);
		if(closeLoc)
		{
			/*
			*/
			switch(state)
			{
			case kSourceFileParseState_InTemplateCode:
				chunk->code.end = line.begin;
				chunk->prefix.end = cursor;
				chunk->outputSpan.begin = cursor;
				chunk->linePrefix = commonPrefix(line, chunk->linePrefix);
				state = kSourceFileParseState_InTemplateOutput;
				break;

			case kSourceFileParseState_Initial:
			case kSourceFileParseState_Default:
			case kSourceFileParseState_InTemplateOutput:
				fprintf(stderr, "fiddle: error: 'OUTPUT' tag without 'TEMPLATE'\n");
				return 0;

			}
		}

		char const* endLoc = findMatchInLine(endTagPattern, line);
		if(endLoc)
		{
			/*
			*/
			switch(state)
			{
			case kSourceFileParseState_InTemplateOutput:
				/*

				Okay, we've reached the end of an embedded
				template, and we need to finish up
				the current chunk and start a new one.

				*/
				chunk->outputSpan.end = line.begin;
				chunk->codeNode = parseTemplate(
					chunk->code,
					chunk->linePrefix);
				/*

				Failure to parse the template should
				count as a failure for the overall
				file.

				*/
				if(!chunk->codeNode)
				{
					return 0;
				}

				/*

				State a new chunk.

				*/
				chunk = allocateChunk();
				*link = chunk;
				link = &chunk->next;
				chunk->prefix.begin = line.begin;
				state = kSourceFileParseState_Default;
				break;

			case kSourceFileParseState_Initial:
			case kSourceFileParseState_Default:
				fprintf(stderr, "fiddle: error: 'END' tag without 'TEMPLATE'\n");
				return 0;
			case kSourceFileParseState_InTemplateCode:
				fprintf(stderr, "fiddle: error: 'END' tag without 'OUTPUT'\n");
				return 0;

			}
		}
		/*

		If none of the above cases matched, then this
		is an "ordinary" line.

		*/
		switch(state)
		{
		default:
			break;

		case kSourceFileParseState_InTemplateCode:
			chunk->linePrefix = commonPrefix(line, chunk->linePrefix);
			break;
		}
	}
	/*

	We've reached the end of the file.
	If we never ran into any embedded templates,
	then there is no point in processing this file
	further, and we should bail.

	*/
	if(state == kSourceFileParseState_Initial)
		return 0;
	/*

	Otherwise we need to terminate the last
	chunk, and then return the whole list
	of chunks.

	*/
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
	char const* inputPath,
	char const* suffix)
{
	assert(stringEndsWith(inputPath, suffix));

	size_t inputSize = strlen(inputPath);
	size_t suffixSize = strlen(suffix);

	size_t trimmedSize = inputSize - suffixSize;

	char* buffer = (char*) malloc(trimmedSize + 1);
	if(!buffer)
		return NULL;

	memcpy(buffer, inputPath, trimmedSize);
	buffer[trimmedSize] = 0;

	return buffer;
}

typedef struct SkubWriter
{
	char* cursor;
	char* begin;
	char* end;
} SkubWriter;

static void writeRawByte(
	SkubWriter*	writer,
	char 		val)
{
	char* c = writer->cursor;
	char* e = writer->end;
	if(c == e)
	{
		char* b = writer->begin;

        size_t oldOffset = c - b;
		size_t oldSize = e - b;
		size_t newSize = oldSize ? oldSize * 2 : 1024; 

		char* n = (char*) realloc(b, newSize);

        c = n + oldOffset;
        e = n + newSize;
        
        writer->begin = n;
		writer->end = e;
	}

	*c++ = val;
	writer->cursor = c;
}

static void writeRaw(
	SkubWriter*	writer,
	char const* begin,
	char const* end)
{
	char const* cc = begin;
	while(cc != end)
	{
		char c = *cc++;
		switch(c)
		{
		case '\r': case '\n':
			if(cc != end)
			{
				char d = *cc;
				if((c ^ d) == ('\n' ^ '\r'))
					cc++;
			}
			writeRawByte(writer, '\n');
			break;

		default:
			writeRawByte(writer, c);
			break;
		}
	}
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

	if(begin == end)
		return;

	switch(*begin)
	{
	default:
		break;

	case '\r': case '\n':
		writeRawT(writer, " _RAW(\"\\n\");");
		break;
	}

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

		case '\r': case '\n':
			if(cursor != end)
			{
				char d = *cursor;
				if((c ^ d) == ('\r' ^ '\n'))
					cursor++;
			}
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

static void emitSpliceExpr(
	SkubWriter*		writer,
	TemplateNode*	node)
{
	for(TemplateNode* nn = node; nn; nn = nn->next)
	{
		switch(nn->flavor)
		{
		case kTemplateNodeFlavor_Text:
			writeRaw(
				writer,
				nn->text.begin,
				nn->text.end);
			break;

		case kTemplateNodeFlavor_TextAndNewline:
			writeRaw(
				writer,
				nn->text.begin,
				nn->text.end);
			writeRawT(
				writer,
				"\n");
			break;

		default:
			assert(!"unimplemented");
			break;
		}
	}
}

static void emitTemplate(
	SkubWriter*	writer,
	TemplateNode*	node)
{
	for(TemplateNode* nn = node; nn; nn = nn->next)
	{
		switch(nn->flavor)
		{
		case kTemplateNodeFlavor_Text:
			emitRaw(
				writer,
				nn->text.begin,
				nn->text.end);
			break;

		case kTemplateNodeFlavor_TextAndNewline:
			emitRaw(
				writer,
				nn->text.begin,
				nn->text.end);
			writeRawT(
				writer,
				"_RAW(\"\\n\");\n");
			break;

		case kTemplateNodeFlavor_Escape:
			writeRaw(
				writer,
				nn->text.begin,
				nn->text.end);
			writeRawT(
				writer,
				"\n");
			break;

		case kTemplateNodeFlavor_EscapeExpr:
			writeRawT(writer, "_SPLICE(");
			emitSpliceExpr(writer, nn->firstChild);
			writeRawT(writer, "); ");
			break;

		default:
			assert(!"unimplemented");
			break;
		}
	}
}

static void emitChunks(
	SkubWriter* writer,
	Chunk*		chunks)
{
	Chunk* chunk = chunks;
	while(chunk)
	{
		emitRaw(writer, chunk->prefix.begin, chunk->code.begin);
		emitRawX(writer, chunk->code.begin, chunk->prefix.end);

		if(chunk->codeNode)
		{		
			emitTemplate(writer, chunk->codeNode);
		}

		emitRawComment(writer, chunk->code.end, chunk->prefix.end);

		emitRawComment(writer, chunk->outputSpan.begin, chunk->outputSpan.end);

		chunk = chunk->next;
	}
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
	char const* outputPath = 0;
	/*

	We read in the whole input file at once,
	so that we can process it in memory.
	If we fail to read the file, then we bail
	out here and now.

	*/
	StringSpan span = readFile(inputPath);
	if(!span.begin)
	{
		return;		
	}
	/*

	The input file will need tobe parsed
	differently based on whether it is a
	template file, or a source file with
	embedded templates. This will also
	determine how we derive a default
	output path.

	*/
	Chunk* chunks = 0;
	char const* templateSuffix = ".fiddle";
	char const* literateSuffix = ".md";
	if(stringEndsWith(inputPath, templateSuffix))
	{
		// Need to trim the end of the path
		outputPath = pickOutputPath(inputPath, templateSuffix);

		chunks = parseTemplateFile(span.begin, span.end);
	}
	else if(stringEndsWith(inputPath, literateSuffix))
	{
		outputPath = pickOutputPath(inputPath, literateSuffix);
		assert(!"literate mode not implemented");
	}
	else
	{
		/*

		If the input is a source file, then by
		default output will be written to the
		same file.

		*/
		outputPath = inputPath;
		/*

		We need to parse the input text into
		an AST representing the file and any
		embedded templates.

		*/
		chunks = parseSourceFile(span.begin, span.end);
	}
	/*

	If we encountered any errors parsing the source
	file, or if there were no embedded templates found
	in a source file, then `chunks` will be null.
	In that case there is nothing to be done with
	this file, and we skip the output generation steps.

	*/
	if(!chunks)
		return;
	/*

	Regardless of the output path we would choose
	by default, the user can override it with the `-o`
	command line option, which sets `gOutputPath`

	*/
	if(gOutputPath)
	{
		outputPath = gOutputPath;
	}
	/*

	Once we've parsed the input into an AST, we will
	generate Lua source code to perform the actual
	code generation logic for this file.

	*/
	SkubWriter writer = { 0, 0, 0 };
	writeRawT(&writer,
		"local _RAW, _SPLICE = ...; ");
	writeRawT(&writer,
		"fiddle_write = _RAW; ");

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
		fprintf(stderr, "fiddle: %s\n", message);
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
		fprintf(stderr, "fiddle: %s\n", message);		
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
			"fiddle: cannot open '%s' for writing\n",
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
		fprintf(stderr, "fiddle: expected argument for option '%s'\n", opt);
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

	char const* appName = "fiddle";
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
				fprintf(stderr, "fiddle: unknown option '%s'\n", arg);
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
		fprintf(stderr, "fiddle: failed to create Lua state\n");
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

	if(gErrorCount != 0)
	{
		exit(1);
	}
	return 0;
}

/*
We include the Lua implementation inline here,
so that our build script doesn't need to take
on any additional complexity.
*/

/* TODO: Figure out when we can enable this...
*/
#ifdef _WIN32
#include <Windows.h>
#undef LoadString
#define LoadString lua_LoadString
#else
#define LUA_USE_POSIX
#endif

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


