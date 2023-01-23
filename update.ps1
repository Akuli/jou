# Disable Progress Bar
$ProgressPreference = 'SilentlyContinue'

try {
    Write-Output "Downloading Jou Update from GitHub..."
    $download_link = Invoke-WebRequest -Uri https://api.github.com/repos/Akuli/jou/releases/latest | ConvertFrom-Json | Select-Object -ExpandProperty assets | Select-Object -ExpandProperty browser_download_url
    Invoke-WebRequest -Uri $download_link -OutFile jou_update.zip
    Write-Output "Extracting Jou Update..."
    Expand-Archive jou_update.zip -DestinationPath jou_update
    Write-Output "Copying Jou Update..."
    Get-ChildItem -Path "./jou_update" -Recurse | Copy-Item -Destination "./" -Recurse -Force
    Write-Output "Cleaning up..."
    Remove-Item jou_update.zip -Force
    Remove-Item jou_update -Force -Recurse
    Write-Output "Done updating Jou!"
} catch {
    Write-Output "Failed to update Jou."
}