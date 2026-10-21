# Builds OpenChatMix and packages the release assets into dist\.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

& "$root\build.bat"
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$version = (Select-String -Path "$root\src\version.h" -Pattern 'APP_VERSION_STRING "(.+)"').Matches[0].Groups[1].Value
$dist = Join-Path $root 'dist'
$staging = Join-Path $dist "OpenChatMix-$version"
$zip = Join-Path $dist "OpenChatMix-$version-win-x64.zip"

if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force $staging | Out-Null
Copy-Item "$root\build\OpenChatMix.exe" $dist -Force
# The optional DeepFilterNet plugin (tools\deepfilter\build.bat) goes with every release when it is built,
# so its latest/download link keeps working.
foreach ($file in 'DeepFilterNet-OpenChatMix.dll', 'DeepFilterNet-NOTICES.txt') {
    if (Test-Path "$root\build\deepfilter\$file") {
        Copy-Item "$root\build\deepfilter\$file" $dist -Force
    }
}
# The release's symbols stay here for reading dumps from that build; they are not published.
Copy-Item "$root\build\OpenChatMix.pdb" (Join-Path $dist "OpenChatMix-$version.pdb") -Force
Copy-Item "$root\build\OpenChatMix.exe", "$root\README.md", "$root\README.uk.md", "$root\LICENSE", "$root\THIRD_PARTY_NOTICES.md" $staging
Compress-Archive -Path "$staging\*" -DestinationPath $zip -Force
Remove-Item $staging -Recurse -Force

$hashes = Get-ChildItem $dist -File -Include 'OpenChatMix.exe', '*.zip', 'DeepFilterNet-OpenChatMix.dll' -Recurse |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name }
Set-Content -Path (Join-Path $dist 'SHA256SUMS.txt') -Value $hashes -Encoding ascii

Write-Host "Packaged OpenChatMix $version into $dist"
