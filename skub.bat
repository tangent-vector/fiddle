@echo off

set ROOT=%~dp0
pushd "%ROOT%"

:: Determine which is newer, between skub.c and skub.exe
set SRCFILE=skub.c
set EXEFILE=skub.exe
set NEWFILE=
for /F %%F in ('dir /B /O:D %SRCFILE% %EXEFILE%') do set NEWFILE=%%F

if "%NEWFILE%"=="%SRCFILE%" (
	goto Build
) else if "%NEWFILE%"=="%EXEFILE%" (
	goto Run
) else (
	echo error: couldn't find skub.exe or skub.c
	goto Error
)

:Build
:: Since we are on Windows, try to build Mangle using Visual Studio
:: TODO: Check for multiple VS versions
call "%VS120COMNTOOLS%VSVARS32.bat"
cl /nologo /I include /Z7 skub.c /link /debug /out:skub.exe
if %errorlevel% NEQ 0 (
	goto Exit
)
del skub.obj

:Run
popd
:: We either found skub.exe, or built it, so we can just invoke it.
"%ROOT%/skub.exe" %*
goto Exit

:Error
set errorlevel=1

:Exit