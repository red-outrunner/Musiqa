param([string]$ExecutablePath = (Join-Path $PSScriptRoot 'musicplayer.exe'))
$ErrorActionPreference = 'Stop'
$ExecutablePath = (Resolve-Path -LiteralPath $ExecutablePath).Path
if ([IO.Path]::GetFileName($ExecutablePath) -ne 'musicplayer.exe') {
    throw 'Choose the installed Musica musicplayer.exe.'
}
$extensions = @('.mp3', '.flac', '.ogg', '.opus', '.wav', '.m4a', '.mp4', '.aac', '.aif', '.aiff', '.wma')
$classes = 'HKCU:\Software\Classes'
$programId = 'Musica.LocalMusicPlayer.AudioFile'
$owner = 'red-outrunner/musica'
$command = '"' + $ExecutablePath + '" "%1"'
function Ensure-RegistryKey([string]$Key) {
    if (-not $Key.StartsWith('HKCU:\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Musica registration only writes to the current user registry.'
    }
    # CreateSubKey preserves existing keys and values while creating absent
    # parents. New-Item -Force would clear other applications' registrations.
    $handle = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($Key.Substring(6))
    if ($null -eq $handle) { throw "Could not open registry key: $Key" }
    $handle.Dispose()
}
function Set-DefaultValue([string]$Key, [string]$Value) {
    Ensure-RegistryKey $Key
    Set-Item -LiteralPath $Key -Value $Value
}
Set-DefaultValue "$classes\$programId" 'Musica audio file'
Set-DefaultValue "$classes\$programId\shell\open\command" $command
New-ItemProperty -Path "$classes\$programId" -Name MusicaOwner -Value $owner -PropertyType String -Force | Out-Null
$application = "$classes\Applications\musicplayer.exe"
$applicationOwner = Get-ItemPropertyValue -LiteralPath $application -Name MusicaOwner -ErrorAction SilentlyContinue
$registerApplication = -not (Test-Path -LiteralPath $application) -or $applicationOwner -eq $owner
if ($registerApplication) {
    Set-DefaultValue "$application\shell\open\command" $command
    New-ItemProperty -Path $application -Name MusicaOwner -Value $owner -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $application -Name FriendlyAppName -Value Musica -PropertyType String -Force | Out-Null
    Ensure-RegistryKey "$application\SupportedTypes"
}
$capabilities = 'HKCU:\Software\Musica\LocalMusicPlayer\Capabilities'
Ensure-RegistryKey "$capabilities\FileAssociations"
New-ItemProperty -Path $capabilities -Name MusicaOwner -Value $owner -PropertyType String -Force | Out-Null
New-ItemProperty -Path $capabilities -Name ApplicationName -Value Musica -PropertyType String -Force | Out-Null
New-ItemProperty -Path $capabilities -Name ApplicationDescription -Value 'Play local audio files' -PropertyType String -Force | Out-Null
foreach ($extension in $extensions) {
    if ($registerApplication) {
        New-ItemProperty -Path "$application\SupportedTypes" -Name $extension -Value '' -PropertyType String -Force | Out-Null
    }
    Ensure-RegistryKey "$classes\$extension\OpenWithProgids"
    New-ItemProperty -Path "$classes\$extension\OpenWithProgids" -Name $programId -Value '' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path "$capabilities\FileAssociations" -Name $extension -Value $programId -PropertyType String -Force | Out-Null
}
Ensure-RegistryKey 'HKCU:\Software\RegisteredApplications'
New-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -Name 'Musica.LocalMusicPlayer' -Value 'Software\Musica\LocalMusicPlayer\Capabilities' -PropertyType String -Force | Out-Null
Write-Output 'Musica is available in Open with for this user. Default apps were not changed.'
