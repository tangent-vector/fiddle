:: Fiddle Wrapper Script
:: =====================
::
:: This file implements a wrapper script for invoking `fiddle`
:: without needing to integrate it into a build system.
::
:: The script will try to compile a `fiddle.exe` executable
:: on demand from `fiddle.c`, before forwarding the
:: arguments it is given to that executable.
::
:: The executable will be rebuilt whenever `fiddle.c` is
:: newer than `fiddle.exe`, so that users can pull a new copy
:: of the Fiddle repository without having to delete
:: their cached executable.
::
:: For right now, the actual build is performed using a
:: single version of Visual Studio. More VS versions will
:: be added over time, but if you need to build with a
:: different compiler than you probably either want
:: `fiddle.sh`, or you should be building things yourself.
::
:: Implementation
:: --------------
::
:: We will start by turning off the default echoing of commands.
::
@echo off
::
:: Next, let's capture the path of the directory that
:: contains this script as `%FIDDLEPATH$, and move
:: into that directory.
::
set FIDDLEPATH=%~dp0
pushd "%FIDDLEPATH%"
::
:: Next, we are going to determine whether the source
:: file or the executable is newer. We do this by using
:: `dir` to list the two files, from oldest to newest,
:: and then iterate over the results and track the
:: last file we see.
::
:: In the case where the executable doesn't exist, this
:: will only list the source file, so it will be deterined
:: to be newer, which is what we want.
::
set SRCFILE=fiddle.c
set EXEFILE=fiddle.exe
set NEWFILE=
for /F %%F in ('dir /B /O:D %SRCFILE% %EXEFILE%') do set NEWFILE=%%F
::
:: Now based on what we found, we decide what to do next:
::
::
:: * If the source file is newer, then we need to build.
::
::
:: * If the executable file is newer, then we can just run it.
::
::
:: * If neither of these is true, something went wrong in
::   our build logic (maybe the source file is missing?).
::
if "%NEWFILE%"=="%SRCFILE%" (
	goto Build
) else if "%NEWFILE%"=="%EXEFILE%" (
	goto Run
) else (
	echo "fiddle: error: couldn't find fiddle.exe or fiddle.c"
	goto Error
)
::
:: In the case where we decide to build, we will look for
:: the `VSVARS32.bat` script, which will hopefully set things
:: up for us to be able to invoke the `cl` compiler.
:: We are only going to do 32-bit builds here, because they
:: should work on any Windows version, and we don't want to
:: deal with extra complexity.
::
:Build
::
:: For right now, we only check a single VS version, and we
:: don't even check if the appropriate environment variable
:: is even set. This needs work:
::
call "%VS150COMNTOOLS%VSVARS32.bat" 2>nul
if %errorlevel% EQU 0 ( goto Compile )
call "%VS140COMNTOOLS%VSVARS32.bat" 2>nul
if %errorlevel% EQU 0 ( goto Compile )
call "%VS130COMNTOOLS%VSVARS32.bat" 2>nul
if %errorlevel% EQU 0 ( goto Compile )
call "%VS120COMNTOOLS%VSVARS32.bat" 2>nul
if %errorlevel% EQU 0 ( goto Compile )

echo "Failed to find a Visual Studio version installed"
goto Exit
::
:: Invokeing the compiler is done in a pretty standard way.
::
:Compile
cl /nologo fiddle.c /link setargv.obj /out:fiddle.exe 1>nul
::
:: If we need to debug we can do the following instead:
::
::     cl /nologo /Z7 fiddle.c /link /debug /out:fiddle.exe
::
:: We check if the compile failed, and skip over trying
:: to run the executable if it did.
::
if %errorlevel% NEQ 0 (
	goto Exit
)
::
:: Just to keep things tidy, we'll delete the `.obj` file
:: that the compiler spits out:
::
del fiddle.obj
::
:: If the build succeeded, or if we determined we didn't
:: need to buidl at all, we need to run the executable.
::
:Run
::
:: We start by restoring the directory to where it was
:: when the script was first invoked, this ensures that
:: relative paths in the arguments mean what the user expects.
::
popd
::
:: Next we just invoke our build executable and pass along
:: all the arguments that were passed to this script.
::
"%FIDDLEPATH%/fiddle.exe" %*
goto Exit
::
:: If an error occured along the way, we make sure to
:: set the error level before exiting, so that the invocation
:: of `fiddle.bat` itself fails.
:Error
set errorlevel=1
::
:: Once we get here, we are all done.
::
:Exit