# NppPrintPreview for Notepad++ をビルドする（x64）
# 使い方：pwsh -File build.ps1（このファイルのあるフォルダで）
# 出力：build\x64\NppPrintPreview.dll（このファイルのあるフォルダの下）
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio の C++ ツールが見つかりません' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$out = Join-Path $root 'build\x64'
New-Item -ItemType Directory -Force $out | Out-Null
$src = Join-Path $root 'src\NppPrintPreview.cpp'
# /MT：C ランタイムを DLL に含める（配布先に VC++ ランタイムが無くても動くように）
# 版の情報（NppPrintPreview.rc）を .res にして一緒にリンクする
$rcSrc = Join-Path $root 'src\NppPrintPreview.rc'
$res = Join-Path $out 'NppPrintPreview.res'
$rc = "rc /nologo /fo `"$res`" `"$rcSrc`""
$cl = "cl /nologo /utf-8 /std:c++17 /O2 /W4 /EHsc /MT /LD /DUNICODE /D_UNICODE `"$src`" `"$res`" /Fo`"$out\\`" /Fe`"$out\NppPrintPreview.dll`" /link user32.lib gdi32.lib comdlg32.lib winspool.lib"
cmd /c "call `"$vcvars`" >nul && $rc && $cl"
if ($LASTEXITCODE -ne 0) { throw "ビルドに失敗しました（$LASTEXITCODE）" }
Get-Item "$out\NppPrintPreview.dll" | Select-Object FullName, Length, LastWriteTime
