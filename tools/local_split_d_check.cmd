@echo off
rem Local C++ check for the Split-D top level.
rem
rem This is NOT a substitute for the official Vitis flow and it is NOT a test
rem wrapper around run_hls.sh. It only compiles the same sources with MinGW g++
rem against the vendored Vitis HLS headers plus the repository's local math
rem stubs, and runs the existing C++ testbench. It proves that the sources and
rem the testbench compile and that the C-level numerical path still passes; it
rem does NOT prove synthesis, II, timing or RTL behaviour.
rem The testbench now also checks a strict official Vitis bit baseline.
rem Local math stubs are not bit-identical for every case: inspect math-summary
rem and bit-summary separately. Any bit mismatch still returns a nonzero exit.
rem
rem Usage:  tools\local_split_d_check.cmd [PE_DIM] [HEAD_DIM]
rem Defaults: 4 16
setlocal enabledelayedexpansion

set PE_DIM=%~1
set HEAD_DIM=%~2
if "%PE_DIM%"=="" set PE_DIM=4
if "%HEAD_DIM%"=="" set HEAD_DIM=16

set SCRIPT_DIR=%~dp0
set ROOT=%SCRIPT_DIR%..
pushd "%ROOT%"

set OUTDIR=build\local_split_d_check
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set SOURCES=src\stream\split_d\split_d_dma.cpp ^
 src\stream\split_d\split_d_compute.cpp ^
 src\stream\split_d\split_d_controller.cpp ^
 src\stream\split_d\fsa_stream_split_d.cpp ^
 src\stream\arithmetic.cpp ^
 src\stream\local_math_stubs.cpp ^
 src\stream\dma.cpp ^
 src\stream\accumulator.cpp ^
 src\stream\pe_raw_fma.cpp ^
 src\stream\fp32_raw_fma.cpp ^
 tests\stream\test_fsa_stream_split_d.cpp

echo [local-check] PE_DIM=%PE_DIM% HEAD_DIM=%HEAD_DIM%
g++ -std=c++14 -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter ^
 -Iinclude -isystem third_party\vitis_hls\include ^
 -DFSA_LOCAL_MATH_STUBS -DHLS_NO_XIL_FPO_LIB ^
 -DFSA_SPLIT_D_PE_DIM=%PE_DIM% -DFSA_SPLIT_D_HEAD_DIM=%HEAD_DIM% ^
 %SOURCES% ^
 -o "%OUTDIR%\split_d_check_%PE_DIM%x%HEAD_DIM%.exe"
if errorlevel 1 (
    echo [local-check] COMPILE FAILED
    popd
    exit /b 1
)

"%OUTDIR%\split_d_check_%PE_DIM%x%HEAD_DIM%.exe"
set RC=%ERRORLEVEL%
echo [local-check] run exit=%RC%
popd
exit /b %RC%
