param(
  [Parameter(Mandatory = $true)]
  [string]$SourceDir,
  [Parameter(Mandatory = $true)]
  [string]$OutputDir,
  [Parameter(Mandatory = $true)]
  [Alias("Version")]
  [string]$ProductVersion,
  [string]$ProductName = "WORR",
  [string]$Manufacturer = "DarkMatter Productions",
  [string]$UpgradeCode = "{0AA7041A-0DC8-43A8-90E7-87D83A07B696}",
  [string]$MsiName = "worr-win64.msi"
)

$ErrorActionPreference = "Stop"

function Invoke-NativeCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [Parameter(Mandatory = $true)]
    [string[]]$Arguments
  )

  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
  }
}

if ($ProductVersion -notmatch '^\d+\.\d+\.\d+$') {
  throw "Version must be Major.Minor.Patch for MSI. Got: $ProductVersion"
}

$heat = Get-Command heat.exe -ErrorAction SilentlyContinue
$candle = Get-Command candle.exe -ErrorAction SilentlyContinue
$light = Get-Command light.exe -ErrorAction SilentlyContinue
if (-not $heat -or -not $candle -or -not $light) {
  throw "WiX Toolset executables not found in PATH."
}

$source = (Resolve-Path $SourceDir).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$output = (Resolve-Path $OutputDir).Path

$tempRoot = Join-Path $env:TEMP ("worr_msi_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

$wxsTemplate = Join-Path $PSScriptRoot "installer\worr.wxs"
$productWxs = Join-Path $tempRoot "Product.wxs"
$harvestWxs = Join-Path $tempRoot "Harvest.wxs"

try {
  Copy-Item $wxsTemplate $productWxs

  Invoke-NativeCommand -FilePath $heat.Source -Arguments @(
    "dir"
    $source
    "-cg"
    "AppComponents"
    "-dr"
    "INSTALLDIR"
    "-gg"
    "-g1"
    "-scom"
    "-sreg"
    "-srd"
    "-var"
    "var.SourceDir"
    "-out"
    $harvestWxs
  )

  Invoke-NativeCommand -FilePath $candle.Source -Arguments @(
    "-nologo"
    "-dProductVersion=$ProductVersion"
    "-dProductName=$ProductName"
    "-dManufacturer=$Manufacturer"
    "-dUpgradeCode=$UpgradeCode"
    "-dSourceDir=$source"
    "-out"
    "$tempRoot\"
    $productWxs
    $harvestWxs
  )

  $msiPath = Join-Path $output $MsiName
  Invoke-NativeCommand -FilePath $light.Source -Arguments @(
    "-nologo"
    "-ext"
    "WixUIExtension"
    "-out"
    $msiPath
    (Join-Path $tempRoot "Product.wixobj")
    (Join-Path $tempRoot "Harvest.wixobj")
  )

  Write-Host "Wrote $msiPath"
}
finally {
  if (Test-Path $tempRoot) {
    Remove-Item -Recurse -Force $tempRoot
  }
}
