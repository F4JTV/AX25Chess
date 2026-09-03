<#
    build_windows.ps1 - Construit AX25Chess et son installateur.

        .\build_windows.ps1                  build complet
        .\build_windows.ps1 full             idem, explicitement
        .\build_windows.ps1 app              build portable seul, sans installateur
        .\build_windows.ps1 installer        compile l'installateur depuis dist\
        .\build_windows.ps1 clean            efface les sorties puis reconstruit

        .\build_windows.ps1 -InnoSetupPath "D:\Inno\ISCC.exe"

    Prerequis : Python 3.10 ou superieur en 64 bits, et Inno Setup 6 ou 7 pour
    l'installateur.
#>

[CmdletBinding()]
param(
    # Position explicite : sans elle, PowerShell rend positionnels TOUS les
    # parametres declares, et un mot isole comme « full » se serait lie a
    # -InnoSetupPath. Le script aurait alors cru qu'on lui donnait un chemin,
    # saute toute la recherche d'Inno Setup, et echoue sur un « introuvable »
    # parfaitement trompeur.
    [Parameter(Position = 0)]
    [ValidateSet("full", "app", "installer", "clean")]
    [string]$Mode = "full",

    [switch]$Clean,
    [switch]$SkipInstaller,
    [string]$InnoSetupPath
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Write-Step($text) {
    Write-Host ""
    Write-Host "==> $text" -ForegroundColor Cyan
}
function Fail($text) {
    Write-Host ""
    Write-Host "ERREUR: $text" -ForegroundColor Red
    exit 1
}

# --- interpretation du mode ------------------------------------------------

# Filet de securite. La regle de positionnement des parametres a varie selon
# les versions de PowerShell : si un mot de mode s'est malgre tout lie a
# -InnoSetupPath, on le remet ou il doit aller plutot que d'echouer plus loin
# sur un « ISCC.exe introuvable » qui n'expliquerait rien.
$knownModes = @("full", "app", "installer", "clean")
if ($InnoSetupPath -and ($knownModes -contains $InnoSetupPath.ToLower())) {
    $Mode = $InnoSetupPath.ToLower()
    $InnoSetupPath = ""
}

$SkipBuild = $false
switch ($Mode) {
    "clean"     { $Clean = $true }
    "app"       { $SkipInstaller = $true }
    "installer" { $SkipBuild = $true }
}

# ---------------------------------------------------------------------------
#  Recherche d'Inno Setup
# ---------------------------------------------------------------------------
function Resolve-Iscc([string]$candidate) {
    <#  Accepte un chemin vers ISCC.exe ou vers le dossier qui le contient. #>
    if (-not $candidate) { return $null }
    if (Test-Path -PathType Leaf $candidate) {
        if ([System.IO.Path]::GetFileName($candidate) -ieq "ISCC.exe") {
            return (Resolve-Path $candidate).Path
        }
        # Compil32.exe ou autre executable du meme dossier
        $sibling = Join-Path (Split-Path -Parent $candidate) "ISCC.exe"
        if (Test-Path -PathType Leaf $sibling) { return $sibling }
        return $null
    }
    if (Test-Path -PathType Container $candidate) {
        $inside = Join-Path $candidate "ISCC.exe"
        if (Test-Path -PathType Leaf $inside) { return $inside }
    }
    return $null
}

function Find-InnoSetup {
    <#
        Cherche ISCC.exe par couches, de la plus fiable a la plus generale.
        Aucun chemin n'est code en dur sur un numero de version : Inno Setup 7
        existe en 32 et en 64 bits, donc sous « Program Files » comme sous
        « Program Files (x86) », et il peut aussi etre installe par
        utilisateur.
    #>
    $searched = New-Object System.Collections.Generic.List[string]

    # 1. dans le PATH
    $searched.Add("le PATH")
    $cmd = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return @{ Path = $cmd.Source; Searched = $searched } }

    # 2. cle de desinstallation, ou l'installateur inscrit InstallLocation
    $uninstall = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall")
    foreach ($root in $uninstall) {
        $searched.Add($root)
        if (-not (Test-Path $root)) { continue }
        $keys = Get-ChildItem $root -ErrorAction SilentlyContinue |
            Where-Object { $_.PSChildName -like "Inno Setup*" }
        foreach ($key in $keys) {
            $location = (Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue).InstallLocation
            $found = Resolve-Iscc $location
            if ($found) { return @{ Path = $found; Searched = $searched } }
        }
    }

    # 3. association de fichier .iss, qui pointe sur Compil32.exe
    $assoc = "Registry::HKEY_CLASSES_ROOT\InnoSetupScriptFile\Shell\Compile\Command"
    $searched.Add("l'association des fichiers .iss")
    if (Test-Path $assoc) {
        $command = (Get-ItemProperty $assoc -ErrorAction SilentlyContinue)."(default)"
        if ($command -match '"?([^"]+\.exe)"?') {
            $found = Resolve-Iscc $Matches[1]
            if ($found) { return @{ Path = $found; Searched = $searched } }
        }
    }

    # 4. emplacements d'installation habituels, toutes versions confondues
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)},
               (Join-Path $env:LOCALAPPDATA "Programs")) |
        Where-Object { $_ -and (Test-Path $_) }
    foreach ($root in $roots) {
        $searched.Add((Join-Path $root "Inno Setup*"))
        $hits = Get-ChildItem $root -Directory -Filter "Inno Setup*" `
                    -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        foreach ($hit in $hits) {
            $found = Resolve-Iscc $hit.FullName
            if ($found) { return @{ Path = $found; Searched = $searched } }
        }
    }

    return @{ Path = $null; Searched = $searched }
}

# ---------------------------------------------------------------------------

Write-Step "Verification de Python"
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { Fail "Python est introuvable dans le PATH." }
$arch = & python -c "import struct; print(struct.calcsize('P') * 8)"
if ($arch.Trim() -ne "64") {
    Fail "Python $($arch.Trim()) bits detecte ; un Python 64 bits est requis."
}
Write-Host "    $(& python --version) ($($arch.Trim()) bits)"
Write-Host "    mode : $Mode"

if ($Clean) {
    Write-Step "Nettoyage"
    foreach ($dir in @("build", "dist", "Output", ".venv-build")) {
        if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    }
    Write-Host "    build, dist, Output et .venv-build supprimes"
}

$exe = "dist\AX25Chess\AX25Chess.exe"

if (-not $SkipBuild) {
    Write-Step "Environnement de construction"
    if (-not (Test-Path ".venv-build")) {
        & python -m venv .venv-build
        if ($LASTEXITCODE -ne 0) { Fail "Creation de l'environnement virtuel echouee." }
    }
    $venvPy = Join-Path $PSScriptRoot ".venv-build\Scripts\python.exe"
    if (-not (Test-Path $venvPy)) { Fail "Python de l'environnement virtuel introuvable." }
    & $venvPy -m pip install --upgrade pip --quiet
    & $venvPy -m pip install --quiet PyQt6 pyinstaller pillow
    if ($LASTEXITCODE -ne 0) { Fail "Installation des dependances echouee." }
    Write-Host "    PyQt6, PyInstaller et Pillow installes"

    Write-Step "Icone et fichiers de version"
    & $venvPy make_icon.py
    if ($LASTEXITCODE -ne 0) { Fail "Generation de l'icone echouee." }
    & $venvPy make_version.py
    if ($LASTEXITCODE -ne 0) { Fail "Generation des fichiers de version echouee." }

    Write-Step "Construction de l'application"
    & $venvPy -m PyInstaller --noconfirm --clean AX25Chess.spec
    if ($LASTEXITCODE -ne 0) { Fail "PyInstaller a echoue." }

    if (-not (Test-Path $exe)) { Fail "$exe n'a pas ete produit." }
    $mb = [math]::Round((Get-ChildItem "dist\AX25Chess" -Recurse |
        Measure-Object -Property Length -Sum).Sum / 1MB, 1)
    Write-Host "    dist\AX25Chess ($mb Mo)"

    Write-Step "Essai de demarrage"
    # --version sort immediatement : cela verifie que l'executable gele resout
    # ses imports, sans ouvrir de fenetre.
    $proc = Start-Process -FilePath $exe -ArgumentList "--version" -PassThru `
        -Wait -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        Fail "L'executable construit s'est termine avec le code $($proc.ExitCode)."
    }
    Write-Host "    l'executable demarre"
} else {
    Write-Step "Construction ignoree (mode installer)"
    if (-not (Test-Path $exe)) {
        Fail "$exe est absent. Lancez d'abord un build complet."
    }
    Write-Host "    reutilisation de dist\AX25Chess"
}

if ($SkipInstaller) {
    Write-Step "Termine (installateur ignore)"
    Write-Host "    Build portable : dist\AX25Chess\"
    exit 0
}

Write-Step "Recherche d'Inno Setup"
if ($InnoSetupPath) {
    $iscc = Resolve-Iscc $InnoSetupPath
    if (-not $iscc) {
        Fail ("-InnoSetupPath vaut « $InnoSetupPath », qui ne designe ni " +
              "ISCC.exe ni un dossier le contenant. Corrigez ce chemin, ou " +
              "omettez -InnoSetupPath pour laisser le script chercher.")
    }
} else {
    $result = Find-InnoSetup
    $iscc = $result.Path
    if (-not $iscc) {
        Write-Host "    Emplacements consultes :" -ForegroundColor Yellow
        foreach ($place in $result.Searched) {
            Write-Host "      - $place" -ForegroundColor Yellow
        }
        Fail ("ISCC.exe est introuvable. Installez Inno Setup depuis " +
              "https://jrsoftware.org/isdl.php, ou indiquez son emplacement " +
              "avec -InnoSetupPath. Pour un build sans installateur : " +
              ".\build_windows.ps1 app")
    }
}
Write-Host "    $iscc"

foreach ($required in @("LICENSE.txt", "INSTALL-NOTES.en.txt",
                        "INSTALL-NOTES.fr.txt", "version.iss")) {
    if (-not (Test-Path $required)) {
        Fail "$required est absent alors que installer.iss l'exige."
    }
}

# Direwolf est facultatif. Quand le dossier est present il est empaquete, ce
# qui entraine une obligation de licence dont mieux vaut etre averti ici
# qu'apres publication.
if (Test-Path "direwolf") {
    $dwExe = Get-ChildItem "direwolf" -Recurse -Filter "direwolf.exe" |
        Select-Object -First 1
    if (-not $dwExe) {
        Fail ("Le dossier direwolf existe mais ne contient pas direwolf.exe. " +
              "Placez-y le dossier telecharge depuis la page des versions de " +
              "Direwolf, ou supprimez-le pour construire sans Direwolf.")
    }
    if (-not (Test-Path "DIREWOLF-NOTICE.txt")) {
        Fail "DIREWOLF-NOTICE.txt est absent ; il doit accompagner le binaire."
    }
    $dwSize = [math]::Round((Get-ChildItem "direwolf" -Recurse |
        Measure-Object -Property Length -Sum).Sum / 1MB, 1)
    Write-Host "    Direwolf empaquete depuis .\direwolf ($dwSize Mo)"
    Write-Host ("    Direwolf est en GPL-2.0 : si vous publiez cet " +
                "installateur, gardez l'archive") -ForegroundColor Yellow
    Write-Host ("    des sources de la meme version disponible. Voyez " +
                "DIREWOLF-NOTICE.txt.") -ForegroundColor Yellow
} else {
    Write-Host "    Pas de dossier .\direwolf ; construction sans Direwolf."
}

Write-Step "Compilation de l'installateur"
& $iscc "installer.iss"
if ($LASTEXITCODE -ne 0) { Fail "La compilation Inno Setup a echoue." }

$setup = Get-ChildItem "Output\*.exe" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Step "Termine"
if ($setup) {
    $smb = [math]::Round($setup.Length / 1MB, 1)
    Write-Host "    Installateur   : $($setup.FullName)  ($smb Mo)"
}
Write-Host "    Build portable : dist\AX25Chess\"
