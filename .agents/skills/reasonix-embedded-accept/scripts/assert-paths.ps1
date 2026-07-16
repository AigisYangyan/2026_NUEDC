[CmdletBinding()]
param(
    [string[]]$MustExist = @(),
    [string[]]$MustNotExist = @()
)

$failures = @()
$requiredPaths = @($MustExist | ForEach-Object { $_ -split ',' } |
    Where-Object { $_ -ne '' })
$forbiddenPaths = @($MustNotExist | ForEach-Object { $_ -split ',' } |
    Where-Object { $_ -ne '' })

foreach ($path in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        $failures += "Expected path to exist: $path"
    }
}

foreach ($path in $forbiddenPaths) {
    if (Test-Path -LiteralPath $path) {
        $failures += "Expected path to be absent: $path"
    }
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "Path postconditions satisfied."
exit 0
