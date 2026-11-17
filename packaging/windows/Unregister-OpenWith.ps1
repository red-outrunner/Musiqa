$ErrorActionPreference = 'Stop'
$classes = 'HKCU:\Software\Classes'
$programId = 'Musica.LocalMusicPlayer.AudioFile'
$owner = 'red-outrunner/musica'
$programOwner = Get-ItemPropertyValue -LiteralPath "$classes\$programId" -Name MusicaOwner -ErrorAction SilentlyContinue
if ($programOwner -eq $owner) {
    foreach ($extension in @('.mp3', '.flac', '.ogg', '.opus', '.wav', '.m4a', '.mp4', '.aac', '.aif', '.aiff', '.wma')) {
        Remove-ItemProperty -Path "$classes\$extension\OpenWithProgids" -Name $programId -ErrorAction SilentlyContinue
    }
}
foreach ($key in @("$classes\$programId", "$classes\Applications\musicplayer.exe", 'HKCU:\Software\Musica\LocalMusicPlayer\Capabilities')) {
    $keyOwner = Get-ItemPropertyValue -LiteralPath $key -Name MusicaOwner -ErrorAction SilentlyContinue
    if ($keyOwner -eq $owner) { Remove-Item -LiteralPath $key -Recurse -Force }
}
$registered = Get-ItemPropertyValue -LiteralPath 'HKCU:\Software\RegisteredApplications' -Name 'Musica.LocalMusicPlayer' -ErrorAction SilentlyContinue
if ($registered -eq 'Software\Musica\LocalMusicPlayer\Capabilities') {
    Remove-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -Name 'Musica.LocalMusicPlayer'
}
Write-Output 'Removed Musica Open with registration for this user.'
