# embed_images.ps1
#
# Converts images (PNG, JPG, BMP, WEBP, etc.) into a C++ header of
# byte arrays so the loader can ship images baked into the EXE -- no
# external files, no disk reads at runtime.
#
# stb_image (used by the framework) only handles PNG/JPG/BMP/TGA/GIF.
# So WebP/AVIF inputs are auto-converted to PNG using Windows' built-in
# WIC codecs (System.Drawing) before we embed them.
#
# Usage examples:
#   powershell -ExecutionPolicy Bypass -File embed_images.ps1 dbd.png apex.png dayz.png
#
#   # Or rename on the fly: "alias:path" makes the C array g_<alias>_data.
#   # This is how you map "Horizon.webp" onto the "apex" slot the UI looks for.
#   powershell -ExecutionPolicy Bypass -File embed_images.ps1 "apex:$env:USERPROFILE\Downloads\Horizon.webp"
#
# Each input becomes:
#   inline constexpr unsigned char g_<alias>_data[] = { ... };
#   inline constexpr size_t        g_<alias>_data_size = sizeof(g_<alias>_data);
#   #define GORDO_HAVE_GAME_IMAGE_<ALIAS>
#
# Output is game_images_data.h next to this script.

param(
    [Parameter(Mandatory=$true, ValueFromRemainingArguments=$true)]
    [string[]] $Inputs
)

Add-Type -AssemblyName System.Drawing

$outPath = Join-Path $PSScriptRoot "game_images_data.h"

# Parse each input -- accept either "alias:path" or just "path".
$resolved = @()
foreach ($in in $Inputs) {
    $alias = $null
    $path  = $in
    if ($in -match '^([A-Za-z][A-Za-z0-9_]*):(.+)$') {
        $alias = $Matches[1].ToLower()
        $path  = $Matches[2]
    }

    # Expand env vars in the path (~, %USERPROFILE%, etc.)
    $path = [Environment]::ExpandEnvironmentVariables($path)
    if ($path.StartsWith('~')) {
        $path = Join-Path $env:USERPROFILE $path.Substring(1).TrimStart('\','/')
    }

    if (-not (Test-Path $path)) {
        Write-Warning "Skipping missing file: $path"
        continue
    }

    if (-not $alias) {
        $alias = [System.IO.Path]::GetFileNameWithoutExtension($path).ToLower()
    }

    $resolved += @{ alias = $alias; path = (Resolve-Path $path).Path }
}

if ($resolved.Count -eq 0) {
    Write-Error "No valid inputs."
    exit 1
}

function Get-EmbeddableBytes {
    param([string]$Path)
    $raw = [System.IO.File]::ReadAllBytes($Path)
    if ($raw.Length -lt 12) {
        Write-Error "File too small to be a real image: $Path"
        return $null
    }

    # Detect the actual format from magic bytes -- the extension is a
    # lie if someone just renamed a .webp to .png.
    $isPng  = ($raw[0] -eq 0x89 -and $raw[1] -eq 0x50 -and $raw[2] -eq 0x4E -and $raw[3] -eq 0x47)
    $isJpg  = ($raw[0] -eq 0xFF -and $raw[1] -eq 0xD8 -and $raw[2] -eq 0xFF)
    $isBmp  = ($raw[0] -eq 0x42 -and $raw[1] -eq 0x4D)
    $isGif  = ($raw[0] -eq 0x47 -and $raw[1] -eq 0x49 -and $raw[2] -eq 0x46)
    $isWebp = ($raw[0] -eq 0x52 -and $raw[1] -eq 0x49 -and $raw[2] -eq 0x46 -and $raw[3] -eq 0x46 -and
               $raw[8] -eq 0x57 -and $raw[9] -eq 0x45 -and $raw[10] -eq 0x42 -and $raw[11] -eq 0x50)

    # stb_image (the framework's decoder) handles PNG/JPG/BMP/GIF directly.
    if ($isPng -or $isJpg -or $isBmp -or $isGif) {
        $stbFmt = if ($isPng) { 'PNG' } elseif ($isJpg) { 'JPEG' } elseif ($isBmp) { 'BMP' } else { 'GIF' }
        Write-Host "    detected $stbFmt -- embedding raw bytes ($($raw.Length) bytes)"
        return $raw
    }

    # WebP / AVIF / HEIC / unknown: re-encode through Windows' WIC
    # codec layer to PNG so stb_image can read it.
    $detected = if ($isWebp) { 'WebP' } else { 'unknown format' }
    Write-Host "    detected $detected -- converting to PNG via WIC..."

    # First try System.Drawing.Image.FromFile (works if the WebP/HEIC
    # codec is installed at the OS level).
    $img = $null
    try {
        $img = [System.Drawing.Image]::FromFile($Path)
    } catch {
        # Fallback: feed the raw bytes through a MemoryStream so the
        # codec lookup goes by content rather than extension.
        try {
            $msIn = New-Object System.IO.MemoryStream(,$raw)
            $img  = [System.Drawing.Image]::FromStream($msIn)
        } catch {
            Write-Error @"
Failed to decode $detected file. Windows does not have a codec for it.
Install Microsoft's "Webp Image Extensions" from the Store, OR convert
the file to PNG manually (https://convertio.co/webp-png/) and pass the
.png path to this script.
Error was: $_
"@
            return $null
        }
    }

    try {
        $msOut = New-Object System.IO.MemoryStream
        $img.Save($msOut, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "    converted to PNG ($($msOut.Length) bytes)"
        return $msOut.ToArray()
    } catch {
        Write-Error "Re-encode to PNG failed: $_"
        return $null
    } finally {
        if ($img) { $img.Dispose() }
    }
}

# Decode every input FIRST. We only emit the #define + array for
# successful decodes so the header never references a missing symbol.
$ok = @()
foreach ($r in $resolved) {
    $bytes = Get-EmbeddableBytes -Path $r.path
    if (-not $bytes -or $bytes.Length -eq 0) {
        Write-Warning ("  {0,-10} <- {1}   FAILED to decode -- skipping" -f $r.alias, $r.path)
        continue
    }
    Write-Host ("  {0,-10} <- {1,-50} {2:N0} bytes" -f $r.alias, $r.path, $bytes.Length)
    $ok += @{ alias = $r.alias; path = $r.path; bytes = $bytes }
}

if ($ok.Count -eq 0) {
    Write-Error "No images decoded successfully -- not touching game_images_data.h."
    Write-Host ""
    Write-Host "If you tried a .webp file, your Windows install may not have the"
    Write-Host "WebP codec registered for System.Drawing. Easiest workaround:"
    Write-Host "convert it to PNG manually (e.g. https://convertio.co/webp-png/) and"
    Write-Host "re-run with the .png path."
    exit 1
}

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("// Auto-generated by embed_images.ps1 on $(Get-Date -Format 'u')")
[void]$sb.AppendLine("// Do not edit by hand -- re-run the script after replacing sources.")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("#include <cstddef>")
[void]$sb.AppendLine("")

# Defines for each alias that game_images.h checks.
foreach ($r in $ok) {
    [void]$sb.AppendLine("#define GORDO_HAVE_GAME_IMAGE_$($r.alias.ToUpper())")
}
[void]$sb.AppendLine("")

foreach ($r in $ok) {
    $bytes = $r.bytes
    $arr   = "g_$($r.alias)_data"
    [void]$sb.AppendLine("// === $($r.alias) <- $($r.path) ($($bytes.Length) bytes) ===")
    [void]$sb.Append("inline constexpr unsigned char $arr[] = {")
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($i % 16 -eq 0) { [void]$sb.AppendLine(); [void]$sb.Append("    ") }
        [void]$sb.Append(("0x{0:X2}," -f $bytes[$i]))
        if ($i % 16 -ne 15) { [void]$sb.Append(" ") }
    }
    [void]$sb.AppendLine()
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("inline constexpr size_t ${arr}_size = sizeof($arr);")
    [void]$sb.AppendLine("")
}

Set-Content -Path $outPath -Value $sb.ToString() -Encoding ASCII
Write-Host ""
Write-Host "Wrote: $outPath"
Write-Host "Rebuild the solution to bake the new bytes in."
