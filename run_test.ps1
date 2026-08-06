<#
.SYNOPSIS
    Build and run one or all FSA-HLS C++ tests.

.PARAMETER Test
    Test name such as pe, test_pe, core_step_smoke, or all.
    The default value is all.

.EXAMPLE
    .\run_test.ps1 pe

.EXAMPLE
    .\run_test.ps1 all
#>

param(
    [string]$Test = "all"
)

# PSScriptRoot makes the script independent of the caller's current directory.
$ProjectRoot = $PSScriptRoot
$IncludeDirectory = Join-Path $ProjectRoot "include"
$SourceDirectory = Join-Path $ProjectRoot "src\core"
$TestDirectory = Join-Path $ProjectRoot "tests"
$BuildDirectory = Join-Path $ProjectRoot "build\tests"

if(-not (Get-Command g++ -ErrorAction SilentlyContinue)){
    Write-Host "[ERROR] g++ was not found. Add g++ to PATH first." -ForegroundColor Red
    exit 1
}

if(-not (Test-Path -LiteralPath $TestDirectory)){
    Write-Host "[ERROR] Test directory was not found: $TestDirectory" -ForegroundColor Red
    exit 1
}

# Every test is linked with all current core source files. This keeps the script
# generic when a new core module or test file is added.
$CoreSources = @(
    Get-ChildItem -LiteralPath $SourceDirectory -Filter "*.cpp" -File |
        Sort-Object Name |
        ForEach-Object FullName
)

$AllTests = @(
    Get-ChildItem -LiteralPath $TestDirectory -Filter "*.cpp" -File |
        Sort-Object Name
)

if($AllTests.Count -eq 0){
    Write-Host "[ERROR] No C++ test file was found." -ForegroundColor Red
    exit 1
}

if($Test -eq "all"){
    $SelectedTests = $AllTests
}else{
    # Accept pe, test_pe, and test_pe.cpp.
    $RequestedName = [System.IO.Path]::GetFileNameWithoutExtension($Test)
    $SelectedTests = @(
        $AllTests | Where-Object {
            $_.BaseName -eq $RequestedName -or
            $_.BaseName -eq "test_$RequestedName"
        }
    )
}

if($SelectedTests.Count -eq 0){
    Write-Host "[ERROR] Test was not found: $Test" -ForegroundColor Red
    Write-Host "Available tests:" -ForegroundColor Yellow
    foreach($TestFile in $AllTests){
        Write-Host "  $($TestFile.BaseName)"
    }
    exit 1
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

$CompilerOptions = @(
    "-std=c++14",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    "-I$IncludeDirectory"
)

foreach($TestFile in $SelectedTests){
    $TestName = $TestFile.BaseName
    $Executable = Join-Path $BuildDirectory "$TestName.exe"

    Write-Host "[BUILD] $TestName" -ForegroundColor Cyan

    & g++ @CompilerOptions @CoreSources $TestFile.FullName -o $Executable
    if($LASTEXITCODE -ne 0){
        Write-Host "[FAIL] $TestName failed to build." -ForegroundColor Red
        exit $LASTEXITCODE
    }

    Write-Host "[RUN]   $TestName" -ForegroundColor Cyan

    & $Executable
    if($LASTEXITCODE -ne 0){
        Write-Host "[FAIL] $TestName failed while running." -ForegroundColor Red
        exit $LASTEXITCODE
    }

    Write-Host "[PASS]  $TestName" -ForegroundColor Green
}

Write-Host "All selected tests passed." -ForegroundColor Green
