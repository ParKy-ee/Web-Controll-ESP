$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

Write-Host "Watching MQTT topic planko/imu on the local Docker broker. Press Ctrl+C to stop."
& docker compose exec mosquitto mosquitto_sub -h 127.0.0.1 -p 1883 -t "planko/imu" -v
if ($LASTEXITCODE -ne 0) {
    throw "Could not subscribe. Start the broker first with tools\start_mosquitto.ps1."
}
