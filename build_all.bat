@echo off
rem ===========================================================================
rem  AX25Chess - complete Windows build
rem
rem  Fetches the Dire Wolf sources, configures, compiles, deploys the Qt
rem  runtime with the QML modules, bundles the Visual C++ runtime and builds
rem  the installer, in one go.
rem
rem  Run it from the project root, in a "x64 Native Tools Command Prompt
rem  for VS 2022" (or 2026). Needs, installed once:
rem    - Visual Studio with "Desktop development with C++" AND the individual
rem      component "C++ Clang Compiler for Windows" (clang-cl): the Dire Wolf
rem      sources use GCC extensions that cl.exe does not accept. CMake does
rem      not let one project mix clang-cl and cl, so clang-cl compiles
rem      everything, C and C++ alike; it speaks the MSVC ABI and runtime,
rem      which is what the Qt MSVC binaries expect.
rem    - Qt 6 for MSVC 64-bit, the base installation: Qt Quick and Qt Quick
rem      Controls are part of it; no add-on module is needed
rem    - git (to fetch Dire Wolf)
rem    - Inno Setup 6, for the installer
rem
rem  Options:
rem    /deps         fetch and patch the Dire Wolf sources if missing
rem    /clean        wipe the build directory first
rem    /nobuild      skip configure and compile, deploy and package only
rem    /noinstaller  stop after staging, do not run Inno Setup
rem    /test         run the unit tests after compiling
rem    /help         show this help (/? too)
rem ===========================================================================

setlocal enabledelayedexpansion
title AX25Chess - complete build

rem ------------------------------------------------------- paths to adjust
set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"

set "BUILD_DIR=build"
set "DIST=installer\dist"
set "DIREWOLF_DIR=external\direwolf"
set "DIREWOLF_REF=1.8"

rem ------------------------------------------------------------- arguments
set DO_DEPS=0
set DO_CLEAN=0
set DO_BUILD=1
set DO_INSTALLER=1
set DO_TEST=0

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="/deps"        set DO_DEPS=1&       shift & goto parse
if /i "%~1"=="/clean"       set DO_CLEAN=1&      shift & goto parse
if /i "%~1"=="/nobuild"     set DO_BUILD=0&      shift & goto parse
if /i "%~1"=="/noinstaller" set DO_INSTALLER=0&  shift & goto parse
if /i "%~1"=="/test"        set DO_TEST=1&       shift & goto parse
if /i "%~1"=="/?"           goto usage
if /i "%~1"=="/help"        goto usage
echo Unknown option: %~1
goto usage
:parsed

if not exist "CMakeLists.txt" (
    echo [X] Run this script from the project root, next to CMakeLists.txt.
    goto fail
)

rem The application version, read from CMakeLists.txt and passed to Inno
rem Setup. This line stays at top level: the searched string holds a
rem parenthesis, which would break a parenthesised block.
set "APP_VERSION="
for /f "tokens=1-3" %%A in ('findstr /b /c:"project(AX25Chess" CMakeLists.txt') do set "APP_VERSION=%%C"
if not defined APP_VERSION for /f "tokens=2" %%A in ('findstr /r /c:"^  VERSION [0-9]" CMakeLists.txt') do set "APP_VERSION=%%A"
if not defined APP_VERSION set "APP_VERSION=0.0.0"

rem ---------------------------------------------------------- prerequisites
echo.
echo === Checking prerequisites ===

where cmake >nul 2>&1
if errorlevel 1 (
    echo [X] cmake not found in PATH. Open a "x64 Native Tools Command Prompt".
    goto fail
)
where ninja >nul 2>&1
if errorlevel 1 (
    echo [X] ninja not found in PATH. It comes with the "C++ CMake tools for
    echo     Windows" component of Visual Studio; open a "x64 Native Tools
    echo     Command Prompt".
    goto fail
)
where cl >nul 2>&1
if errorlevel 1 (
    echo [X] cl.exe not found. This must run in a "x64 Native Tools Command Prompt".
    goto fail
)
rem clang-cl: in PATH when the Clang component is installed, otherwise looked
rem up in the Visual Studio this prompt belongs to.
set "CLANG_CL="
where clang-cl >nul 2>&1
if not errorlevel 1 set "CLANG_CL=clang-cl"
if not defined CLANG_CL if defined VCINSTALLDIR if exist "%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-cl.exe" set "CLANG_CL=%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-cl.exe"
if not defined CLANG_CL (
    echo [X] clang-cl.exe not found. The Dire Wolf core needs it: install the
    echo     "C++ Clang Compiler for Windows" component with the Visual Studio
    echo     Installer, under Individual components, then open a new prompt.
    goto fail
)
echo [ok] cl and clang-cl (!CLANG_CL!)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [X] Qt not found at %QT_DIR%
    echo     Edit QT_DIR at the top of this script.
    goto fail
)
echo [ok] Qt          %QT_DIR%

rem Inno Setup, looked up in the usual places then in PATH.
set "ISCC="
for %%P in (
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
    "%ProgramFiles%\Inno Setup 7\ISCC.exe"
) do if not defined ISCC if exist %%P set "ISCC=%%~P"
if not defined ISCC for /f "delims=" %%P in ('where ISCC 2^>nul') do if not defined ISCC set "ISCC=%%P"

if "%DO_INSTALLER%"=="1" (
    if defined ISCC (
        echo [ok] Inno Setup  !ISCC!
    ) else (
        echo [--] Inno Setup not found, the installer step will be skipped.
        set DO_INSTALLER=0
    )
)

rem --------------------------------------------------------------- deps
if "%DO_DEPS%"=="1" call :deps
if errorlevel 1 goto fail

if not exist "%DIREWOLF_DIR%\src\direwolf.h" (
    echo [X] Dire Wolf sources not found in %DIREWOLF_DIR%. Run build_all.bat /deps
    goto fail
)
rem Every run: a tree patched by an older release gets the patches added
rem since; those already in place are recognised and left alone.
call :patches
if errorlevel 1 goto fail
echo [ok] Dire Wolf   %DIREWOLF_DIR%

rem ------------------------------------------------------------- timestamps
rem Files dated later than this computer's clock make Ninja re-run CMake
rem without end: build.ninja is never newer than its inputs. The dates in
rem the archive are right, the clock is not - typically Windows two hours
rem behind on a PC that also runs Linux. scripts\fix_timestamps.ps1 sets
rem such files to now and says so.
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\fix_timestamps.ps1"

rem ------------------------------------------------------------------ clean
if "%DO_CLEAN%"=="1" (
    echo.
    echo === Cleaning ===
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%DIST%"      rmdir /s /q "%DIST%"
    if exist "installer\output" rmdir /s /q "installer\output"
)

rem -------------------------------------------------------------- configure
if "%DO_BUILD%"=="1" (
    echo.
    echo === Configuring ===
    cmake -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER="!CLANG_CL:\=/!" ^
        -DCMAKE_CXX_COMPILER="!CLANG_CL:\=/!" ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%"
    if errorlevel 1 (
        echo [X] Configuration failed.
        goto fail
    )

    rem A build.ninja already out of date right after CMake wrote it would
    rem make the build loop on Re-running CMake: ask Ninja why, and stop.
    ninja -C "%BUILD_DIR%" -n -d explain build.ninja > "%BUILD_DIR%\ninja-explain.txt" 2>&1
    findstr /c:"ninja explain" "%BUILD_DIR%\ninja-explain.txt" >nul 2>&1
    if not errorlevel 1 (
        echo [X] Ninja finds build.ninja out of date right after CMake wrote it,
        echo     so the build would re-run CMake without end. Its reasons:
        type "%BUILD_DIR%\ninja-explain.txt"
        goto fail
    )

    echo.
    echo === Compiling ===
    cmake --build "%BUILD_DIR%"
    if errorlevel 1 (
        echo [X] Compilation failed.
        goto fail
    )
)

if "%DO_TEST%"=="1" (
    echo.
    echo === Tests ===
    set "QT_QPA_PLATFORM=offscreen"
    ctest --test-dir "%BUILD_DIR%" --output-on-failure
    if errorlevel 1 (
        echo [X] A test failed.
        goto fail
    )
    set "QT_QPA_PLATFORM="
)

set "OUT=%BUILD_DIR%"
if not exist "%OUT%\ax25chess.exe" (
    echo [X] %OUT%\ax25chess.exe missing. Compile without /nobuild first.
    goto fail
)

rem ------------------------------------------------------------------ stage
echo.
echo === Gathering into %DIST% ===
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%" 2>nul

copy /y "%OUT%\ax25chess.exe" "%DIST%\" >nul

rem --qmldir: the QML of the program is compiled into it, so windeployqt
rem cannot see its imports in the executable; it reads them from the
rem sources. Without it the Material style and the layouts are missing and
rem the window never opens.
echo   Qt runtime and QML modules...
"%QT_DIR%\bin\windeployqt.exe" --release --qmldir src\qml --no-system-d3d-compiler --no-opengl-sw ^
    "%DIST%\ax25chess.exe" >nul
if errorlevel 1 (
    echo [X] windeployqt failed.
    goto fail
)

rem Dire Wolf's data files (tocalls.yaml, symbols-new.txt) are compiled into
rem the program and written to its data folder at start-up: nothing to copy.

rem The Visual C++ runtime. A fresh Windows does not have it, and the
rem program then stops at start-up on a missing MSVCP140.dll. windeployqt
rem usually drops the redistributable next to the program; if not, it comes
rem from the Visual Studio this prompt belongs to. The installer runs it.
echo   Visual C++ runtime...
if not exist "%DIST%\vc_redist.x64.exe" (
    if defined VCToolsRedistDir if exist "%VCToolsRedistDir%vc_redist.x64.exe" copy /y "%VCToolsRedistDir%vc_redist.x64.exe" "%DIST%\" >nul
)
if exist "%DIST%\vc_redist.x64.exe" (
    echo [ok] vc_redist.x64.exe bundled
) else (
    echo [--] vc_redist.x64.exe not found: the installer will rely on the runtime
    echo      already being present on the target machine.
)

copy /y "README.md" "%DIST%\README.md" >nul
copy /y "LICENSE.txt" "%DIST%\LICENSE.txt" >nul

rem -------------------------------------------------------------- installer
if "%DO_INSTALLER%"=="1" (
    echo.
    echo === Building the installer, version %APP_VERSION% ===
    "!ISCC!" /Qp /DAppVersion=%APP_VERSION% "installer\AX25Chess.iss"
    if errorlevel 1 (
        echo [X] Inno Setup failed.
        goto fail
    )
    echo.
    echo [ok] Installer: installer\output\AX25Chess-%APP_VERSION%-setup.exe
) else (
    echo.
    echo [ok] Staged in %DIST%; run ax25chess.exe from there, or build the
    echo      installer with: "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DAppVersion=%APP_VERSION% installer\AX25Chess.iss
)

echo.
echo Done.
endlocal
exit /b 0

:deps
rem The Dire Wolf sources (tag %DIREWOLF_REF% plus our patch). Qt, Visual
rem Studio and Inno Setup have installers of their own and are not handled
rem here.
echo.
echo === Dependencies ===
if not exist "%DIREWOLF_DIR%\src\direwolf.h" (
    where git >nul 2>&1
    if errorlevel 1 (
        echo [X] git is needed to fetch Dire Wolf: https://git-scm.com/download/win
        exit /b 1
    )
    echo   Cloning Dire Wolf %DIREWOLF_REF% into %DIREWOLF_DIR%...
    if exist "%DIREWOLF_DIR%" rmdir /s /q "%DIREWOLF_DIR%"
    git clone --depth 1 --branch %DIREWOLF_REF% https://github.com/wb2osz/direwolf.git "%DIREWOLF_DIR%"
    if errorlevel 1 (
        echo [X] Cloning Dire Wolf failed.
        exit /b 1
    )
)
call :patches
if errorlevel 1 exit /b 1
echo [ok] Dire Wolf sources ready
exit /b 0

rem Applies each patch of patches\direwolf that the tree does not carry yet:
rem one whose reverse applies cleanly is in place already.
:patches
where git >nul 2>&1
if errorlevel 1 (
    echo [X] git is needed to patch the Dire Wolf sources: https://git-scm.com/download/win
    exit /b 1
)
for %%P in (patches\direwolf\*.patch) do (
    git -C "%DIREWOLF_DIR%" apply --reverse --check --ignore-whitespace "%CD%\%%P" >nul 2>&1
    if errorlevel 1 (
        echo   Applying %%~nxP
        git -C "%DIREWOLF_DIR%" apply --ignore-whitespace "%CD%\%%P"
        if errorlevel 1 (
            echo [X] %%~nxP does not apply to the sources in %DIREWOLF_DIR%.
            exit /b 1
        )
    )
)
exit /b 0

:usage
echo.
echo Usage: build_all.bat [/deps] [/clean] [/nobuild] [/noinstaller] [/test]
echo.
echo   /deps         fetch and patch the Dire Wolf sources
echo   /clean        wipe the build directory first
echo   /nobuild      skip configure and compile; deploy and package only
echo   /noinstaller  stop after staging installer\dist
echo   /test         run the unit tests after compiling
echo.
echo Adjust QT_DIR at the top of the file.
endlocal
exit /b 2

:fail
echo.
echo Build aborted.
endlocal
exit /b 1
