<#
.SYNOPSIS
    Paints a rectangular region of a PNG solid black (opaque).

.DESCRIPTION
    Useful as a pre-processing step before bgremove.py: paint a watermark or
    icon the same black as the background so the BFS flood-fill can erase it
    along with the rest of the background in one pass.

    Loads the PNG via MemoryStream to avoid GDI+ file-lock issues when
    writing back to the same path.

.PARAMETER InputPath
    Path to the input PNG.

.PARAMETER OutputPath
    Path to save the result. Can be the same as InputPath (in-place).

.PARAMETER X1
    Left edge of the region to paint black (inclusive).

.PARAMETER Y1
    Top edge of the region to paint black (inclusive).

.PARAMETER X2
    Right edge of the region to paint black (inclusive).

.PARAMETER Y2
    Bottom edge of the region to paint black (inclusive).

.EXAMPLE
    .\paintblack.ps1 -InputPath logo.png -OutputPath logo.png -X1 1870 -Y1 1870 -X2 2010 -Y2 2010
#>

param(
    [Parameter(Mandatory)][string]$InputPath,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][int]$X1,
    [Parameter(Mandatory)][int]$Y1,
    [Parameter(Mandatory)][int]$X2,
    [Parameter(Mandatory)][int]$Y2
)

Add-Type -AssemblyName System.Drawing

$absInput  = [System.IO.Path]::GetFullPath($InputPath)
$absOutput = [System.IO.Path]::GetFullPath($OutputPath)

# Copy source to destination first (no-op if same path), then edit in-place.
# This avoids GDI+ errors when saving to a different path from a MemoryStream.
[System.IO.File]::Copy($absInput, $absOutput, $true)

$bytes = [System.IO.File]::ReadAllBytes($absOutput)
$ms    = [System.IO.MemoryStream]::new($bytes)
$bmp   = [System.Drawing.Bitmap]::new($ms)

$black = [System.Drawing.Color]::FromArgb(255, 0, 0, 0)
$count = 0

for ($py = $Y1; $py -le $Y2; $py++) {
    for ($px = $X1; $px -le $X2; $px++) {
        $bmp.SetPixel($px, $py, $black)
        $count++
    }
}

$bmp.Save($absOutput, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$ms.Dispose()

Write-Host "Painted $count pixels black in region ($X1,$Y1)-($X2,$Y2)."
Write-Host "Saved: $absOutput"
