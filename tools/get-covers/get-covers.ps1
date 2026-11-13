# get-covers.ps1 - downloads launcher covers from SteamGridDB.
#                  Скачивает обложки для лончера с SteamGridDB.
#
# Put state.ini and games.ini from the launcher machine next to this script
# and run get-covers.bat. Covers appear in the covers folder - copy it back
# next to the launcher.
# (RU) Положите рядом со скриптом state.ini и games.ini с машины с лончером
# и запустите get-covers.bat. Обложки появятся в папке covers.
#
# API key: steamgriddb.com -> profile -> Preferences -> API.
# The script asks for it on first run and saves it to sgdb_key.txt.
#
# If a wrong game was matched, add to the game's section in games.ini:
#   cover_search = another title to search for
#   sgdb = 12345        (exact SteamGridDB game id, from its page URL)
# The launcher ignores these lines.
#
# Parameters: -State <path>  -Games <path>  -Out <folder>  -ApiKey <key>
#             -Height 600 (saved cover height)  -Force (download all again)
#             -Lang ru|en (message language, default - from Windows)

param(
    [string]$State  = "",
    [string]$Games  = "",
    [string]$Out    = "",
    [string]$ApiKey = "",
    [int]$Height    = 600,
    [switch]$Force,
    [string]$Lang   = ""
)

$ErrorActionPreference = 'Stop'
if (-not $Lang) { $Lang = if ((Get-UICulture).TwoLetterISOLanguageName -in @('ru', 'uk', 'be')) { 'ru' } else { 'en' } }
function T([string]$ru, [string]$en) { if ($Lang -eq 'ru') { $ru } else { $en } }
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Drawing

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $State) { $State = Join-Path $here 'state.ini' }
if (-not $Games) { $Games = Join-Path $here 'games.ini' }
if (-not $Out)   { $Out   = Join-Path $here 'covers' }
$api = 'https://www.steamgriddb.com/api/v2'

# --- Read ini in any encoding, the same way the launcher does ---------------
function Read-TextAuto([string]$path) {
    $b = [IO.File]::ReadAllBytes($path)
    if ($b.Length -ge 2 -and $b[0] -eq 0xFF -and $b[1] -eq 0xFE) {
        return [Text.Encoding]::Unicode.GetString($b, 2, $b.Length - 2)
    }
    if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
        return [Text.Encoding]::UTF8.GetString($b, 3, $b.Length - 3)
    }
    try {
        $strict = New-Object Text.UTF8Encoding($false, $true)
        return $strict.GetString($b)
    } catch {
        return [Text.Encoding]::GetEncoding(1251).GetString($b)
    }
}

function Read-Ini([string]$path) {
    $res = [ordered]@{}
    if (-not (Test-Path -LiteralPath $path)) { return $res }
    $sec = $null
    foreach ($line in ((Read-TextAuto $path) -split "`r?`n")) {
        $t = $line.Trim()
        if (-not $t -or $t.StartsWith(';') -or $t.StartsWith('#')) { continue }
        if ($t.StartsWith('[')) {
            $e = $t.IndexOf(']')
            if ($e -gt 0) {
                $sec = $t.Substring(1, $e - 1).Trim()
                if (-not $res.Contains($sec)) { $res[$sec] = @{} }
            }
            continue
        }
        $eq = $t.IndexOf('=')
        if ($eq -lt 0 -or -not $sec) { continue }
        $k = $t.Substring(0, $eq).Trim().ToLower()
        $v = $t.Substring($eq + 1).Trim().Trim('"')
        $res[$sec][$k] = $v
    }
    return $res
}

# Same as SanitizeId in the launcher
function Get-GameId([string]$s) {
    $id = ($s -replace '[\[\]=;]', '_').Trim()
    if ($id.Length -gt 59) { $id = $id.Substring(0, 59).Trim() }
    return $id
}

function Clean-Term([string]$s) {
    $s = $s -replace '~\d+$', ''
    $s = $s -replace '\([^)]*\)', ' ' -replace '\[[^\]]*\]', ' '
    $s = $s -replace '[_\.]', ' '
    return ($s -replace '\s+', ' ').Trim()
}

# --- API key ------------------------------------------------------------------
$keyFile = Join-Path $here 'sgdb_key.txt'
if (-not $ApiKey) { $ApiKey = $env:STEAMGRIDDB_KEY }
if (-not $ApiKey -and (Test-Path -LiteralPath $keyFile)) { $ApiKey = (Get-Content -LiteralPath $keyFile -Raw).Trim() }
if (-not $ApiKey) {
    $ApiKey = (Read-Host (T 'Ключ API SteamGridDB' 'SteamGridDB API key')).Trim()
    if (-not $ApiKey) { Write-Host (T 'Ключ не указан.' 'No key given.'); exit 1 }
    Set-Content -LiteralPath $keyFile -Value $ApiKey -Encoding ASCII
}
$headers = @{ Authorization = "Bearer $ApiKey" }

function Invoke-Api([string]$path) {
    return Invoke-RestMethod -Uri ($api + $path) -Headers $headers -UseBasicParsing
}

# --- Cover: downscale and save as JPEG (faster to decode on a P3) ------------
$jpegCodec = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }

function Save-Cover([string]$src, [string]$dst, [int]$maxH) {
    $img = [Drawing.Image]::FromFile($src)
    try {
        $nh = [Math]::Min($maxH, $img.Height)
        $nw = [int][Math]::Round($img.Width * $nh / $img.Height)
        $bmp = New-Object Drawing.Bitmap($nw, $nh)
        $gr = [Drawing.Graphics]::FromImage($bmp)
        $gr.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $gr.PixelOffsetMode   = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $gr.Clear([Drawing.Color]::Black)
        $ia = New-Object Drawing.Imaging.ImageAttributes
        $ia.SetWrapMode([Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $rect = New-Object Drawing.Rectangle(0, 0, $nw, $nh)
        $gr.DrawImage($img, $rect, 0, 0, $img.Width, $img.Height, [Drawing.GraphicsUnit]::Pixel, $ia)
        $gr.Dispose()
        $ep = New-Object Drawing.Imaging.EncoderParameters(1)
        $ep.Param[0] = New-Object Drawing.Imaging.EncoderParameter([Drawing.Imaging.Encoder]::Quality, [long]90)
        $bmp.Save($dst, $jpegCodec, $ep)
        $bmp.Dispose()
    } finally {
        $img.Dispose()
    }
}

function Find-Grid($gameId) {
    $q = 'types=static&nsfw=false&humor=false&mimes=image/jpeg,image/png'
    foreach ($dim in @('dimensions=600x900', 'dimensions=342x482,660x930', '')) {
        $p = "/grids/game/$($gameId)?$q"
        if ($dim) { $p += "&$dim" }
        $r = Invoke-Api $p
        if ($r.success -and $r.data -and @($r.data).Count -gt 0) { return @($r.data)[0] }
    }
    return $null
}

# --- Game list ----------------------------------------------------------------
if (-not (Test-Path -LiteralPath $State) -and -not (Test-Path -LiteralPath $Games)) {
    Write-Host ((T 'Не найдены ни {0}, ни {1}' 'Neither {0} nor {1} was found') -f $State, $Games)
    exit 1
}
$st = Read-Ini $State
$gi = Read-Ini $Games

$list = [ordered]@{}
foreach ($sec in $st.Keys) { if (-not $sec.StartsWith('@')) { $list[$sec] = $true } }
foreach ($sec in $gi.Keys) {
    if ($sec -ieq 'launcher') { continue }
    $list[(Get-GameId $sec)] = $true
}

if (-not (Test-Path -LiteralPath $Out)) { New-Item -ItemType Directory -Path $Out | Out-Null }
$tmp = Join-Path $env:TEMP 'sgdb_cover.tmp'
$exts = @('.jpg', '.jpeg', '.png', '.bmp', '.gif')
$log = New-Object Collections.Generic.List[string]
$miss = New-Object Collections.Generic.List[string]
$ok = 0; $skip = 0

foreach ($id in $list.Keys) {
    $s = if ($st.Contains($id)) { $st[$id] } else { @{} }
    $g = if ($gi.Contains($id)) { $gi[$id] } else { @{} }
    if ($s['hidden'] -eq '1') { continue }

    $exists = $false
    foreach ($e in $exts) { if (Test-Path -LiteralPath (Join-Path $Out ($id + $e))) { $exists = $true } }
    if ($exists -and -not $Force) { $skip++; continue }

    $name = if ($g['name']) { $g['name'] } else { $id }
    $term = if ($g['cover_search']) { $g['cover_search'] } else { Clean-Term $name }

    try {
        $gameId = $g['sgdb']
        $found = "id $gameId"
        if (-not $gameId) {
            $r = Invoke-Api ('/search/autocomplete/' + [Uri]::EscapeDataString($term))
            if (-not $r.success -or -not $r.data -or @($r.data).Count -eq 0) {
                Write-Host ((T '  нет     {0}   (искал: {1})' '  none    {0}   (searched: {1})') -f $id, $term) -ForegroundColor Yellow
                $miss.Add("$id`t" + (T 'не найдена игра' 'game not found') + "`t$term")
                continue
            }
            $hit = @($r.data)[0]
            $gameId = $hit.id
            $found = "$($hit.name) [sgdb=$gameId]"
        }
        $grid = Find-Grid $gameId
        if (-not $grid) {
            Write-Host ((T '  нет     {0}   (нет обложек у {1})' '  none    {0}   (no covers for {1})') -f $id, $found) -ForegroundColor Yellow
            $miss.Add("$id`t" + (T 'нет обложек' 'no covers') + "`t$found")
            continue
        }
        Invoke-WebRequest -Uri $grid.url -OutFile $tmp -UseBasicParsing
        Save-Cover $tmp (Join-Path $Out ($id + '.jpg')) $Height
        Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
        $ok++
        Write-Host ("  ok      {0}  ->  {1}" -f $id, $found) -ForegroundColor Green
        $log.Add("$id`t$found")
    } catch {
        $msg = $_.Exception.Message
        if ($msg -match '401|Unauthorized') {
            Write-Host (T 'Ключ API не принят. Проверьте sgdb_key.txt.' 'The API key was rejected. Check sgdb_key.txt.') -ForegroundColor Red
            exit 1
        }
        Write-Host ((T '  ошибка  {0}: {1}' '  error   {0}: {1}') -f $id, $msg) -ForegroundColor Red
        $miss.Add("$id`t" + (T 'ошибка' 'error') + "`t$msg")
    }
    Start-Sleep -Milliseconds 250
}

$log  | Set-Content -LiteralPath (Join-Path $here 'covers-found.txt')   -Encoding UTF8
$miss | Set-Content -LiteralPath (Join-Path $here 'covers-missing.txt') -Encoding UTF8
Write-Host ''
Write-Host ((T 'Скачано: {0}, уже были: {1}, не найдено: {2}' 'Downloaded: {0}, already there: {1}, not found: {2}') -f $ok, $skip, $miss.Count)
Write-Host (T 'Что с чем сопоставилось — covers-found.txt, проблемные — covers-missing.txt' 'Matches are in covers-found.txt, problems in covers-missing.txt')
