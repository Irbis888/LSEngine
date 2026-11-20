# Generates the render benchmark scenes used by RunRenderBenchmark.ps1.
#   Scenes/RenderBenchmark4096.json           4096 cubes, 64 inline materials in the legacy format
#                                             (readable by engine revisions before the material system)
#   Scenes/RenderBenchmark4096Instances.json  the same cubes and look, materials as 64 instances of one base
#   Assets/RenderBenchmark/*.lsmaterial       base material and its instances
$ErrorActionPreference = 'Stop'
$project = Join-Path (Split-Path $PSScriptRoot -Parent) 'Chapter 9 Texturing\Try2'
$gridSize = 64
$spacing = 1.6
$materialCount = 64
$culture = [System.Globalization.CultureInfo]::InvariantCulture

function Format-Number([double]$value) {
    return $value.ToString('0.####', $culture)
}

function Format-Vector([double[]]$values) {
    return '[' + (($values | ForEach-Object { Format-Number $_ }) -join ', ') + ']'
}

function Get-MaterialVariant([int]$index) {
    $hue = ($index * 0.618034) % 1.0
    $red = 0.25 + 0.65 * [Math]::Abs([Math]::Sin(6.2831853 * $hue))
    $green = 0.25 + 0.65 * [Math]::Abs([Math]::Sin(6.2831853 * ($hue + 0.33)))
    $blue = 0.25 + 0.65 * [Math]::Abs([Math]::Sin(6.2831853 * ($hue + 0.66)))
    $variant = [ordered]@{
        Color = @($red, $green, $blue)
        Roughness = 0.2 + 0.7 * (($index * 37) % 64) / 63.0
        Albedo = $null
        Normal = $null
    }
    if ($index % 4 -eq 0) {
        $variant.Albedo = 'Assets/MaterialDemo/Emissive.png'
    }
    if ($index % 8 -eq 1) {
        $variant.Normal = 'Assets/MaterialDemo/Normal.png'
    }
    return $variant
}

function Write-Utf8([string]$path, [string]$text) {
    New-Item -ItemType Directory -Force (Split-Path $path -Parent) | Out-Null
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

$header = @"
{
  "version": 1,
  "name": "__NAME__",
  "entities": [
    {
      "tag": "Camera",
      "transform": { "position": [0, 46, -64], "scale": [1, 1, 1], "rotation": [0.62, 0, 0] },
      "camera": { "fov": 1.05, "nearZ": 0.1, "farZ": 300, "aspectRatio": 1.6 }
    },
    {
      "tag": "Sun",
      "transform": { "position": [1, 5, -3], "scale": [1, 1, 1], "rotation": [0.65, -0.35, 0] },
      "directionalLight": { "intensity": 2.6, "color": [1, 0.91, 0.79] }
    },
    {
      "tag": "Fill",
      "transform": { "position": [-20, 12, -20], "scale": [1, 1, 1], "rotation": [0, 0, 0] },
      "pointLight": { "intensity": 2.4, "color": [0.55, 0.75, 1], "falloffStart": 5, "falloffEnd": 80 }
    },
    {
      "tag": "Ground",
      "transform": { "position": [0, -0.75, 0], "scale": [110, 1, 110], "rotation": [0, 0, 0] },
      "mesh": { "source": "primitive", "primitive": "plane",
        "material": { "name": "Ground", "color": [0.16, 0.18, 0.21], "roughness": 0.84 } }
    }
"@

$inline = New-Object System.Text.StringBuilder
$instances = New-Object System.Text.StringBuilder
[void]$inline.Append($header.Replace('__NAME__', 'Render benchmark 4096 (inline materials)'))
[void]$instances.Append($header.Replace('__NAME__', 'Render benchmark 4096 (material instances)'))
$half = ($gridSize - 1) * $spacing / 2
for ($row = 0; $row -lt $gridSize; ++$row) {
    for ($column = 0; $column -lt $gridSize; ++$column) {
        $index = $row * $gridSize + $column
        $materialIndex = ($row * 7 + $column * 3) % $materialCount
        $variant = Get-MaterialVariant $materialIndex
        $position = Format-Vector @(($column * $spacing - $half), 0.0, ($row * $spacing - $half))
        $tag = "Cube $index"
        $material = '"name": "Benchmark ' + $materialIndex + '", "color": ' + (Format-Vector $variant.Color) +
            ', "roughness": ' + (Format-Number $variant.Roughness)
        if ($variant.Albedo) {
            $material += ', "albedo": "' + $variant.Albedo + '"'
        }
        if ($variant.Normal) {
            $material += ', "normal": "' + $variant.Normal + '"'
        }
        $transform = '"transform": { "position": ' + $position + ', "scale": [1, 1, 1], "rotation": [0, 0, 0] }'
        [void]$inline.Append(",`n    { `"tag`": `"$tag`", $transform,`n      `"mesh`": { `"source`": `"primitive`", `"primitive`": `"cube`", `"material`": { $material } } }")
        $asset = 'Assets/RenderBenchmark/Instance{0:D2}.lsmaterial' -f $materialIndex
        [void]$instances.Append(",`n    { `"tag`": `"$tag`", $transform,`n      `"mesh`": { `"source`": `"primitive`", `"primitive`": `"cube`" },`n      `"materialComponent`": { `"asset`": `"$asset`" } }")
    }
}
[void]$inline.Append("`n  ]`n}`n")
[void]$instances.Append("`n  ]`n}`n")
Write-Utf8 (Join-Path $project 'Scenes\RenderBenchmark4096.json') $inline.ToString()
Write-Utf8 (Join-Path $project 'Scenes\RenderBenchmark4096Instances.json') $instances.ToString()

Write-Utf8 (Join-Path $project 'Assets\RenderBenchmark\Base.lsmaterial') @"
{
  "version": 1,
  "material": {
    "name": "Benchmark base",
    "color": [1, 1, 1],
    "roughness": 0.5,
    "surface": { "shader": "Shaders/Default.hlsl", "mode": "opaque" }
  }
}
"@
for ($materialIndex = 0; $materialIndex -lt $materialCount; ++$materialIndex) {
    $variant = Get-MaterialVariant $materialIndex
    $overrides = '"color": ' + (Format-Vector $variant.Color) + ', "roughness": ' + (Format-Number $variant.Roughness)
    if ($variant.Albedo) {
        $overrides += ', "albedo": "' + $variant.Albedo + '"'
    }
    if ($variant.Normal) {
        $overrides += ', "normal": "' + $variant.Normal + '"'
    }
    $file = Join-Path $project ('Assets\RenderBenchmark\Instance{0:D2}.lsmaterial' -f $materialIndex)
    Write-Utf8 $file @"
{
  "version": 1,
  "material": {
    "name": "Benchmark $materialIndex",
    "parent": "Assets/RenderBenchmark/Base.lsmaterial",
    "overrides": { $overrides }
  }
}
"@
}
Write-Output "Generated $($gridSize * $gridSize) cubes with $materialCount materials."
