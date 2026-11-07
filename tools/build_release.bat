@echo off
rem Builds the Release DLL in build-release, running the core tests first.
rem Run from the "x86 Native Tools Command Prompt for VS". Optional: set STS_TEST_PF and
rem STS_TEST_RANDOM_PF to profile files first, so the tests also check those (see tests/test_main.c).
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    echo cl was not found. Run this from the x86 Native Tools Command Prompt.
    exit /b 1
)
cl 2>&1 | findstr /C:"for x86" >nul
if errorlevel 1 (
    echo This is not the x86 compiler. Run this from the x86 Native Tools Command Prompt.
    exit /b 1
)

cd /d "%~dp0.."
if not exist lib\SokuLib\CMakeLists.txt (
    echo lib\SokuLib is missing. Run: git submodule update --init
    exit /b 1
)
if not exist build-release mkdir build-release
cd build-release
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release >nul
if errorlevel 1 exit /b 1

cmake --build . --target sts_tests
if errorlevel 1 exit /b 1
tests\sts_tests.exe
if errorlevel 1 (
    echo The tests failed, so the DLL was not built.
    exit /b 1
)

cmake --build . --target SlayTheScarlet
if errorlevel 1 exit /b 1

echo.
echo Checks on the DLL: it must be x86, export the loader's three functions, and import
echo nothing beyond what Windows itself provides.
dumpbin -nologo -headers mod\SlayTheScarlet.dll | findstr /C:"machine ("
dumpbin -nologo -exports mod\SlayTheScarlet.dll | findstr /C:"CheckVersion" /C:"Initialize" /C:"getPriority"
dumpbin -nologo -dependents mod\SlayTheScarlet.dll | findstr /I /C:".dll"
echo.
echo Built build-release\mod\SlayTheScarlet.dll. Next: python tools\package_release.py ^<version^>
exit /b 0
