# Installs `bui`, the command that makes, runs, checks and updates BUI apps.
#
#   irm https://bui.preetham.top/install.ps1 | iex
#
# Windows (x86_64 and arm64), Windows PowerShell 5.1 and PowerShell 7. It downloads the newest release
# from GitHub, checks its checksum, puts bui.exe in %USERPROFILE%\.bui\bin and adds that folder to your
# user PATH.
#
#   $env:BUI_VERSION = 'v0.5.1'     install that release instead of the newest
#   $env:BUI_INSTALL_DIR = 'DIR'    install into DIR
#   $env:BUI_NO_MODIFY_PATH = '1'   do not change PATH
#
# For testing: BUI_LATEST_URL replaces the page whose redirect names the newest release, and
# BUI_INSTALLER_BASE_URL a folder that holds the archive and its .sha256.
#
# SPDX-License-Identifier: GPL-2.0-or-later

& {
    $ErrorActionPreference = 'Stop'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue' # Windows PowerShell 5.1 crawls with its own progress bar

    $repo = 'ppmpreetham/BUI'
    $installDir = if ($env:BUI_INSTALL_DIR) { $env:BUI_INSTALL_DIR } else { Join-Path $env:USERPROFILE '.bui\bin' }
    $baseUrl = $env:BUI_INSTALLER_BASE_URL # for testing: a folder that holds the archive and its .sha256
    $latestUrl = if ($env:BUI_LATEST_URL) { $env:BUI_LATEST_URL } else { "https://github.com/$repo/releases/latest" }

    $esc = [char]27
    $color = [Environment]::UserInteractive -and -not $env:NO_COLOR -and ($Host.UI.SupportsVirtualTerminal -or $env:WT_SESSION)
    function C($code, $text) { if ($color) { "$esc[${code}m$text$esc[0m" } else { $text } }
    # the glyphs are made from character codes: this file stays plain ASCII, which any PowerShell
    # reads the same whatever encoding it downloads it with
    $dot = [char]0x25CF; $tick = [char]0x2714; $cross = [char]0x2718
    function Glyphs($art) { # @ = full block, 1 2 3 4 5 6 = the box corners and lines of the logo
        $art.Replace('@', [string][char]0x2588).Replace('1', [string][char]0x2557).Replace('2', [string][char]0x2554).Replace('3', [string][char]0x255D).Replace('4', [string][char]0x255A).Replace('5', [string][char]0x2551).Replace('6', [string][char]0x2550)
    }
    # the Location of the page's redirect. Invoke-WebRequest cannot be asked for it the same way on both
    # PowerShells (5.1 fails without a response, 7 hands over HttpResponseHeaders, which has no string
    # indexer), so this asks HttpWebRequest, whose headers are a WebHeaderCollection on both.
    function LocationOf($url) {
        $request = [Net.HttpWebRequest]::Create($url)
        $request.AllowAutoRedirect = $false
        $request.UserAgent = 'bui-installer'
        $request.Timeout = 15000
        try {
            $response = $request.GetResponse()
        } catch [Net.WebException] {
            $response = $_.Exception.Response # a redirect is not an error everywhere
            if (-not $response) { return '' }
        }
        try { return [string]$response.Headers['Location'] } finally { $response.Close() }
    }
    function Step($text) { Write-Host "$(C '38;5;208' $dot) $text" }
    function Ok($text) { Write-Host "$(C '32' $tick) $text" }
    function Fail($text) { Write-Host "$(C '31' "$cross $text")"; throw $text }

    Write-Host ''
    Write-Host (C '38;5;208' (Glyphs '@@@@@@1 @@1   @@1@@1'))
    Write-Host (C '38;5;208' (Glyphs '@@266@@1@@5   @@5@@5'))
    Write-Host (C '38;5;208' (Glyphs '@@@@@@23@@5   @@5@@5'))
    Write-Host (C '38;5;208' (Glyphs '@@266@@1@@5   @@5@@5'))
    Write-Host "$(C '38;5;208' (Glyphs '@@@@@@234@@@@@@23@@5'))   $(C '1' 'installing bui')"
    Write-Host (C '38;5;208' (Glyphs '4666663  4666663 463'))
    Write-Host ''

    $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    $triple = switch ($arch) {
        'X64' { 'x86_64-pc-windows-msvc' }
        'Arm64' { 'aarch64-pc-windows-msvc' }
        default { Fail "there is no bui build for the $arch processor" }
    }

    $tag = $env:BUI_VERSION
    if (-not $tag) {
        Step 'Looking for the newest release'
        # the tag comes from the page's redirect (the API would rate-limit)
        $location = LocationOf $latestUrl
        if ($location -notmatch '/tag/(v\d[^/]*)$') { Fail "could not find the newest release of $repo (offline, or no release yet)" }
        $tag = $Matches[1]
    }
    if ($tag -notlike 'v*') { $tag = "v$tag" }

    $asset = "bui-$triple.zip"
    $url = if ($baseUrl) { "$baseUrl/$asset" } else { "https://github.com/$repo/releases/download/$tag/$asset" }
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("bui-install-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp | Out-Null
    try {
        Step "Downloading bui $($tag.TrimStart('v')) for $triple"
        Invoke-WebRequest -Uri $url -OutFile (Join-Path $tmp $asset) -UseBasicParsing
        Invoke-WebRequest -Uri "$url.sha256" -OutFile (Join-Path $tmp "$asset.sha256") -UseBasicParsing
        $want = ((Get-Content (Join-Path $tmp "$asset.sha256") -Raw).Trim() -split '\s+')[0].ToLower()
        $got = (Get-FileHash (Join-Path $tmp $asset) -Algorithm SHA256).Hash.ToLower()
        if ($want -ne $got) { Fail 'the download does not match its checksum: not installing it' }
        Ok 'Checksum matches'

        Expand-Archive -Path (Join-Path $tmp $asset) -DestinationPath $tmp -Force
        if (-not (Test-Path (Join-Path $tmp 'bui.exe'))) { Fail 'the download has no bui.exe in it' }
        New-Item -ItemType Directory -Path $installDir -Force | Out-Null
        # the new file goes in beside the old one first; a running bui cannot be overwritten, but it can be
        # renamed, and the old one comes back if anything fails before the new one is in place
        $target = Join-Path $installDir 'bui.exe'
        $staged = "$target.new"
        $backup = "$target.old"
        Copy-Item (Join-Path $tmp 'bui.exe') $staged -Force
        $hadOld = Test-Path $target
        try {
            if ($hadOld) { Move-Item $target $backup -Force }
            Move-Item $staged $target -Force
        } catch {
            $reason = $_.Exception.Message
            if ($hadOld -and -not (Test-Path $target) -and (Test-Path $backup)) { Move-Item $backup $target -Force }
            Remove-Item $staged -Force -ErrorAction SilentlyContinue
            Fail "could not replace $target ($reason): the old bui is as it was"
        }
        Remove-Item $backup -Force -ErrorAction SilentlyContinue
        Ok "Installed $target"
    } finally {
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $onPath = ($userPath -split ';') -contains $installDir
    if (-not $onPath -and -not $env:BUI_NO_MODIFY_PATH) {
        [Environment]::SetEnvironmentVariable('Path', "$installDir;$userPath", 'User')
        $env:Path = "$installDir;$env:Path"
        Ok "Added $installDir to your PATH"
    }

    Write-Host ''
    Write-Host "$(C '1;32' 'bui is ready.') Open a new terminal, then:"
    Write-Host ''
    Write-Host "  $(C '38;5;208' 'bui new my-app')   $(C '2' 'make an app')"
    Write-Host "  $(C '38;5;208' 'bui doctor')       $(C '2' 'check your setup')"
    Write-Host "  $(C '38;5;208' 'bui update')       $(C '2' 'get newer versions later')"
    Write-Host ''
}
