Fiddle
======

Fiddle writes code so you don't have to.

Overview
--------

Fiddle is a command-line tool for generating code using
*templates* (in the sense of a "template engine" and not in
the sense of C++ templates).

The basic idea is that you write code like this:

    %for _,name in ipairs({"A", "B"}) do
    struct %(name)
    {
        // ...
    };
    %end

and Fiddle generates output code like:

    struct A
    {
        // ...
    };
    struct B
    {
        // ...
    };

This can be used to implement code generation idioms
that are beyond the capabilities of C macros or C++
templates.

The "meta" code in Fiddle is all written in Lua, and
templates can pull in other Lua code:

    %local utils = require "utils"
    %utils.foreach(someTypes, function(type)
    struct %(type.name) { /* ... */ };
    %end)

Why Use Fiddle?
---------------

You probably already know whether your project needs
a code generation tool, and there are plenty of options
out there to choose from.

The goals of Fiddle are:

* Minimal requirements. You don't need to ensure that all
  your developers install the right version of Python or
  some other scripting language. Just drop the Fiddle
  sources into your repository and get going.

* Easy to integrate into a simple build. Adding a source
  code generation step typically makes your build process
  far more complex. Fiddle tries to optimize for allowing
  you to do something very simple that Just Works.

* Can add code generation into existing source files. When
  you realize a file needs to use code generation, you don't
  want to have to rename it, futz around with build setting,
  etc. Fiddle tries to make it as easy as possible to
  incrementally add (and remove) use of code generation in
  a codebase.

* Support complex data descriptions. The code generation
  community seems to call this the "model," but the point is
  that in some cases you just have a list of string and want
  to generate code from those, and other times you have a
  complex bunch of objects representing an entire class
  hierarchy you want to generate. Having a full programming
  language backing up your templates (and one that is good
  for describing data) is important.

Getting Started
---------------

### Adding Fiddle to Your Project

Just clone the Fiddle repository and add it somewhere in
your source tree (or set it up as a Git submodule).

Fiddle includes wrapper scripts `fiddle.bat` (for Windows)
and `fiddle.sh` (for everything else) that you can use
to invoke the tool. These should build an executable on
demand, if you've got a typical developer setup on your machine.

If the default behavior doesn't match what you need for
your build, then feel free to just build `fiddle.c` into an
executable using the compiler and build setup of your choice.

### Deciding How to Write Your Templates

There are two main ways you can use Fiddle:

* You can write stand-along template files with a `.fiddle`
  suffix. These will be processed to generate a separate
  file that has the same name without the `.fiddle`. So
  if you process `awesome.c.fiddle` you'll get `awesome.c`

* You can embed your templates directly into your existing
  source files (typically hiding them in comments) and
  Fiddle will output the expansion into the same file right
  after the template. This mode is similar to the Python-based
  code generation tool "Cog."

Which option is the best fit will depend a lot on the task
and how you like to to build and share your project.

### Invoking Fiddle

If you've got a stand-alone template, then invoke:

    fiddle/fiddle.sh awesome.c.fiddle

This will output `awesome.c` (or give you an error message if anything
went wrong in your meta code).

If you have an embedded template, then invoke:

    fiddle/fiddle.sh awesome.c

This will update `awesome.c` in place (or give an error).

You can also pass multiple files to Fiddle at the same time,
and it will process each of them in turn. Note that when
processing multiple files, Fiddle uses a single Lua state
for all of them, so it is possible for globals set by one
file to affect another (you should avoid relying on this).

Note: a future version of Fiddle may allow you to pass a directory
name and then will recursively look for files which appear to
be templates.

Fiddle Templates
----------------

### The Basics

We'll start by discussing the syntax for stand-alone template
files. Information about embedded templates will come next.

Lines where `%` is the first non-whitespace character are Lua code.
Other lines are text in the target language, and will pass through
unchanged:

    struct A
    %if superCool then -- this line is lua
        : SuperCool    // this line is C++
    %end               -- this line is lua

You can "splice" a Lua expression into a line of code by enclosing
it in `$()`:

    class $(name) { /* ... */ };

Target-language lines can come in the middle of a Lua function
(or anywhere that a Lua statement is allowed), so you can define
re-usable function to emit something:

    %function emitAClass(C)
    class $(C.name)
    %  if C.base then
        : $(C.base.name)
    %  end
    {
    };
    %end

In practice, target-language code lines are transformed into
calls to the `fiddle_write()` function in Lua. The two examples below
are therefore equivalent:

    %for _,name in pairs(allClasses) do
        class $(name) {};
    %end

    %for _,name in pairs(allClasses) do
    %    fiddle_write("class " .. tostring(name) .. " {};\n");
    %end

### Embedded Templates

In order to embed a Fiddle template into an existing source
file (without changing its extension, etc.), the template
must be placed between specific marker lines:

    /* FIDDLE TEMPLATE:
    %for _,C in ipairs(allClasses) do
    struct $(C.name) {};
    %end
    FIDDLE OUTPUT: */
    struct Old {};
    /* FIDDLE END */

The lines that include `FIDDLE` here are marker lines.
Fiddle doesn't care about other text on these lines, so you
can embed them in comments in the original source file
(this example is using a C-style block comment).

The lines between `FIDDLE TEMPLATE` and `FIDDLE OUTPUT` define
the template for the code to be generated.
The template follows the same syntax described above, so
Lua code still needs to be marked with `%`.

The lines between `FIDDLE OUTPUT` and `FIDDLE END` will be
*replaced* with the result of processing the template.
This means you can checkn in the source-file after running
Fiddle, and users will be able to compile the generated
code without depending on Fiddle at all.

Just remember not to manually edit those lines, or your
changes will be lost!

Fiddle also supports using line instead of block comments.
Here is the same example rewritten to use C/C++ line comments:

    // FIDDLE TEMPLATE:
    // %for _,C in ipairs(allClasses) do
    // struct $(C.name) {};
    // %end
    // FIDDLE OUTPUT:
    struct Old {};
    // FIDDLE END

If all the lines from `FIDDLE TEMPLATE` to `FIDDLE OUTPUT`
have a common prefix (in this case `// `), then that prefix
will be removed from the template before processing.

