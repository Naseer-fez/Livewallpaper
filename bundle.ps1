param(
    [string]$ConfigFile = "bundle_config.json"
)

$ErrorActionPreference = 'Stop'

# Ensure ConfigFile is an absolute path based on script directory if not absolute
if (-not [System.IO.Path]::IsPathRooted($ConfigFile)) {
    $ConfigFile = Join-Path $PSScriptRoot $ConfigFile
}

if (!(Test-Path $ConfigFile)) {
    Write-Host "[ERROR] Configuration file not found: $ConfigFile" -ForegroundColor Red
    exit 1
}

$Config = Get-Content $ConfigFile -Raw | ConvertFrom-Json

$EnigmaConsole = Join-Path $Config.enigma_dir "enigmavbconsole.exe"
if (!(Test-Path $EnigmaConsole)) {
    Write-Host "[ERROR] Enigma Virtual Box console not found at: $EnigmaConsole" -ForegroundColor Red
    Write-Host "Please update enigma_dir in $ConfigFile"
    exit 1
}

$ProjectDir = $PSScriptRoot
$BuildDir = Join-Path $ProjectDir $Config.build_dir
$InputExe = Join-Path $BuildDir $Config.input_exe
$OutputExe = Join-Path $BuildDir $Config.output_exe

if (!(Test-Path $InputExe)) {
    Write-Host "[ERROR] Input executable not found: $InputExe" -ForegroundColor Red
    exit 1
}

# Dynamically generate the .evb XML string
$evbXml = @"
<?xml version="1.0" encoding="windows-1251"?>
<EnigmaVirtualBox>
  <InputFile>$($Config.build_dir)\$($Config.input_exe)</InputFile>
  <OutputFile>$($Config.build_dir)\$($Config.output_exe)</OutputFile>
  <Files>
"@

foreach ($dll in $Config.dlls) {
    $evbXml += @"
    <File>
      <Name>$dll</Name>
      <Path>$($Config.build_dir)\$dll</Path>
      <FileVirtualPath>%DEFAULT FOLDER%\$dll</FileVirtualPath>
      <RegisterAsActiveX>false</RegisterAsActiveX>
      <Compression>true</Compression>
    </File>
"@
}

$evbXml += @"
  </Files>
  <Registry></Registry>
  <Options>
    <AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>
    <DeleteExtractedOnExit>true</DeleteExtractedOnExit>
  </Options>
</EnigmaVirtualBox>
"@

$EvbFile = Join-Path $ProjectDir "temp_bundle.evb"
Set-Content -Path $EvbFile -Value $evbXml -Encoding UTF8

Write-Host "[INFO] Packing executable with Enigma Virtual Box..."
& $EnigmaConsole $EvbFile

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Failed to pack executable." -ForegroundColor Red
    Remove-Item -Path $EvbFile -Force
    exit $LASTEXITCODE
}

Write-Host "[INFO] Replacing original executable with the bundled version..."
Remove-Item -Path $InputExe -Force
Move-Item -Path $OutputExe -Destination $InputExe -Force

Write-Host "[INFO] Cleaning up virtualized DLLs from the folder..."
foreach ($dll in $Config.dlls) {
    $DllPath = Join-Path $BuildDir $dll
    if (Test-Path $DllPath) {
        Remove-Item -Path $DllPath -Force
    }
}

Remove-Item -Path $EvbFile -Force

Write-Host "==========================================" -ForegroundColor Green
Write-Host "[SUCCESS] Bundling complete!" -ForegroundColor Green
Write-Host "The directory '$($Config.build_dir)' now contains the single-file $($Config.input_exe)" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
