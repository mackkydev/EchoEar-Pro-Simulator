param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        "parked",
        "charging",
        "low_battery",
        "door_open",
        "cloud_stale",
        "cloud_offline",
        "driving_gps"
    )]
    [string]$Scenario
)

$ErrorActionPreference = "Stop"
$Source = Join-Path $PSScriptRoot "mock\scenarios\$Scenario.txt"
$Destination = Join-Path $PSScriptRoot "mock\state.txt"

if (-not (Test-Path -LiteralPath $Source)) {
    throw "Scenario file not found: $Source"
}

Copy-Item -LiteralPath $Source -Destination $Destination -Force
Write-Host "EchoEar preview scenario changed to: $Scenario"
Write-Host "The running simulator will reload it within about one second."
