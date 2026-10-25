$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot

$serverVersion = & docker version --format '{{.Server.Version}}' 2>$null
if ($LASTEXITCODE -ne 0 -or -not $serverVersion) {
    throw "Docker Engine is not running. Start Docker Desktop, then run tools\start_mosquitto.ps1 again."
}

& docker compose up -d
if ($LASTEXITCODE -ne 0) {
    throw "Could not start the Mosquitto container. See the Docker Compose error above."
}

Write-Host ""
Write-Host "Mosquitto container status:"
& docker compose ps

$lanAddressList = [System.Collections.Generic.List[string]]::new()
foreach ($adapter in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
    if ($adapter.OperationalStatus -ne "Up" -or $adapter.NetworkInterfaceType -eq "Loopback") {
        continue
    }

    $properties = $adapter.GetIPProperties()
    $gateway = $properties.GatewayAddresses |
        Where-Object { $_.Address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork } |
        Select-Object -First 1
    if (-not $gateway) { continue }

    foreach ($address in $properties.UnicastAddresses) {
        if ($address.Address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
            continue
        }
        $candidate = $address.Address.IPAddressToString
        if ($candidate -and $candidate -ne "127.0.0.1" -and $candidate -notlike "169.254.*") {
            $lanAddressList.Add($candidate)
        }
    }
}
$lanAddresses = @($lanAddressList | Select-Object -Unique)

Write-Host ""
Write-Host "Broker listeners:"
Write-Host "  MQTT:       <this-PC-LAN-IP>:1883"
Write-Host "  WebSocket:  ws://<this-PC-LAN-IP>:9001/mqtt"
Write-Host "  Topic:      planko/imu (created automatically when first used)"
if ($lanAddresses.Count -gt 0) {
    Write-Host "LAN IPv4 address candidates on this PC:"
    $lanAddresses | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""
Write-Host "Gateway must use this PC's LAN IP on the same Wi-Fi."
Write-Host "Index on this PC can use localhost; add ?broker=<LAN-IP> if it runs on another device."
Write-Host "Follow logs with: docker compose logs -f mosquitto"
