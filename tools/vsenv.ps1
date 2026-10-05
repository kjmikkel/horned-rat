# Dot-source to enter an x86 MSVC developer environment in the current shell:
#   . ./tools/vsenv.ps1
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ x86/x64 tools was found.' }
# VsDevCmd looks for vswhere on PATH and prints a spurious error if it is missing.
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
