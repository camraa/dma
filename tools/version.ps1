# Cree dist\version.json, lu par l'application et le lanceur pour trouver les mises a jour
param([string]$Ver)
$ErrorActionPreference = 'Stop'
$utf8 = New-Object Text.UTF8Encoding $false
$notes = [IO.File]::ReadAllText("$PSScriptRoot\..\NOTES.md", $utf8) -replace "`r", ''
if ($notes -notmatch [regex]::Escape("Camra's Tool $Ver")) { throw "NOTES.md ne decrit pas la version $Ver" }
$fr, $en = $notes -split '### English', 2
function Get-Bullets([string]$t) {
    $l = $t -split "`n" | Where-Object { $_ -like '- *' } | ForEach-Object { $_.Replace('`', '').Replace('**', '').Replace('\', '\\').Replace('"', '\"') }
    return ($l -join '\n')
}
$assets = foreach ($f in 'Camras-Tool.exe', 'Camras-Tool-Lanceur.exe') {
    $h = (Get-FileHash "$PSScriptRoot\..\dist\$f" -Algorithm SHA256).Hash.ToLower()
    "{""name"": ""$f"", ""digest"": ""sha256:$h"", ""browser_download_url"": ""https://github.com/camraa/dma/releases/download/v$Ver/$f""}"
}
$json = "{""tag_name"": ""v$Ver"",`n ""notes_fr"": ""$(Get-Bullets $fr)"",`n ""notes_en"": ""$(Get-Bullets $en)"",`n ""assets"": [`n  $($assets -join ",`n  ")]}`n"
[IO.File]::WriteAllText("$PSScriptRoot\..\dist\version.json", $json, $utf8)
$null = $json | ConvertFrom-Json
