[CmdletBinding()]
param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Args
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$windres = Join-Path $scriptDir 'rc.cmd'
if (-not (Test-Path -Path $windres)) {
  throw "WINDRES wrapper not found at $windres"
}
$ar = Join-Path $scriptDir 'llvm-ar-no-thin.cmd'
if (-not (Test-Path -Path $ar)) {
  throw "AR wrapper not found at $ar"
}

$env:WINDRES = $windres
$env:AR = $ar
& meson @Args
exit $LASTEXITCODE
