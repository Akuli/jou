#
# On Windows, "jou --update" runs this script.
#

# Disable Progress Bar
$ProgressPreference = 'SilentlyContinue'

# Stop on error
$ErrorActionPreference = 'Stop'

# Clean up anything that this script leaves when it crashes mid-way.
Write-Output "Cleaning up..."
Remove-Item jou_update -Force -Recurse -ErrorAction Ignore
Remove-Item jou_update.zip -Force -ErrorAction Ignore
Remove-Item jou_update.json -Force -ErrorAction Ignore
Get-ChildItem . -Filter *.old | ForEach-Object { Remove-Item $_ -Recurse }

Write-Output "Finding latest version..."
# For some reason, Invoke-WebRequest works for me only if I specify -OutFile.
Invoke-WebRequest -Uri https://api.github.com/repos/Akuli/jou/releases/latest -OutFile jou_update_info.json
$version = Get-Content jou_update_info.json | ConvertFrom-Json | Select-Object -ExpandProperty name
$download_link = Get-Content jou_update_info.json | ConvertFrom-Json | Select-Object -ExpandProperty assets | Select-Object -ExpandProperty browser_download_url

Write-Output "Downloading Jou $version from GitHub..."
Invoke-WebRequest -Uri $download_link -OutFile jou_update.zip

Write-Output "Extracting..."
Expand-Archive jou_update.zip -DestinationPath jou_update

# jou.exe and some dll files are currently running, so they cannot be
# deleted, overwritten, or moved. But they can be renamed.
#
# Most files can still be deleted, so we delete as much as we can.
Write-Output "Deleting old Jou..."
Get-ChildItem . -Exclude jou_update | ForEach-Object { Rename-Item "$_" "$_.old" }
Get-ChildItem . -Filter *.old | ForEach-Object { Remove-Item $_ -Recurse -ErrorAction Ignore }

Write-Output "Installing new Jou..."
Copy-Item ./jou_update/jou/* -Destination . -Recurse

Write-Output "Cleaning up..."
Remove-Item jou_update -Recurse
Remove-Item jou_update.zip
Remove-Item jou_update.json
