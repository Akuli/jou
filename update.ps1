#
# On Windows, "jou --update" runs this script.
#

# Disable Progress Bar
$ProgressPreference = 'SilentlyContinue'

# Stop on error
$ErrorActionPreference = 'Stop'

# Clean up anything that this script leaves when it either succeeds or crashes mid-way.
Write-Output "Cleaning up..."
Remove-Item jou_update -Force -Recurse -ErrorAction Ignore
Remove-Item jou_update.zip -Force -ErrorAction Ignore
Remove-Item jou_update_info.json -Force -ErrorAction Ignore
Get-ChildItem . -Filter *.old | ForEach-Object { Remove-Item $_ }

Write-Output "Finding latest version..."
# For some reason, Invoke-WebRequest works for me only if I specify -OutFile.
Invoke-WebRequest -Uri https://api.github.com/repos/Akuli/jou/releases/latest -OutFile jou_update_info.json
$version = Get-Content jou_update_info.json | ConvertFrom-Json | Select-Object -ExpandProperty name
$download_link = Get-Content jou_update_info.json | ConvertFrom-Json | Select-Object -ExpandProperty assets | Select-Object -ExpandProperty browser_download_url
Remove-Item jou_update_info.json

Write-Output "Downloading Jou $version from GitHub..."
Invoke-WebRequest -Uri $download_link -OutFile jou_update.zip

Write-Output "Extracting..."
Expand-Archive jou_update.zip -DestinationPath jou_update
Remove-Item jou_update.zip

# jou.exe and some dll files are currently running, so they cannot be
# deleted, overwritten, or moved. But they can be renamed.
#
# Most files (e.g. all of stdlib) can be deleted as usual.
#
# We can't erase the whole installation directory because the user might
# put their files there. It isn't a good idea anyway, but we really
# shouldn't wipe the user's files.
Write-Output "Deleting old Jou..."
Remove-Item stdlib -Recurse
Remove-Item zig -Recurse
Rename-Item jou.exe jou.exe.old
foreach ($dll in Get-ChildItem . -Filter *.dll) {
    try {
        Remove-Item $dll
    } catch {
        Rename-Item $dll "$dll.old"
    }
}

Write-Output "Installing new Jou..."
Copy-Item jou_update/jou/* -Destination . -Recurse
Remove-Item jou_update -Recurse
