# Sauvegarde du code : historique Git complet + fichiers pas encore enregistres (commit), sur un autre disque.
# Restaurer : git clone <fichier.bundle> dma ; puis, s'il y en a un, decompresser le .zip par-dessus.
param([Parameter(Mandatory = $true)][string]$Dest, [int]$Garder = 30)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..").Path
$null = New-Item -ItemType Directory -Force $Dest
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$bundle = Join-Path $Dest "dma_$stamp.bundle"
try {
    Set-Location $repo
    git -c core.longpaths=true bundle create --quiet $bundle HEAD --branches --tags
    if ($LASTEXITCODE) { throw 'git bundle a echoue' }
    git bundle verify --quiet $bundle
    if ($LASTEXITCODE) { throw 'sauvegarde illisible' }
    # Fichiers modifies ou nouveaux
    $files = @(git -c core.quotepath=false ls-files --modified --others --exclude-standard | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($files.Count) {
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::Open((Join-Path $Dest "dma_$stamp.zip"), 'Create')
        try { foreach ($f in $files) { $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $repo $f), $f) } } finally { $zip.Dispose() }
    }
    # Garde les plus recentes
    foreach ($ext in 'bundle', 'zip') {
        Get-ChildItem -LiteralPath $Dest -Filter "dma_*.$ext" | Sort-Object Name -Descending | Select-Object -Skip $Garder | Remove-Item
    }
    $msg = "OK $stamp, $($files.Count) fichier(s) non enregistre(s)"
    $code = 0
} catch {
    $msg = "ECHEC $stamp : $_"
    $code = 1
}
Add-Content -LiteralPath (Join-Path $Dest 'sauvegarde.log') -Value $msg -Encoding UTF8
$msg
exit $code
