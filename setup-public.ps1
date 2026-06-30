# Zypher Public Access Setup Script
# This script will make your website accessible from anywhere

Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  Zypher Public Access Setup" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""

# Check if ngrok is installed
$ngrokInstalled = Get-Command ngrok -ErrorAction SilentlyContinue

if (-not $ngrokInstalled) {
    Write-Host "ngrok is not installed." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To install ngrok:" -ForegroundColor White
    Write-Host "1. Download from: https://ngrok.com/download" -ForegroundColor Gray
    Write-Host "2. Extract and add to PATH" -ForegroundColor Gray
    Write-Host "3. Sign up and get your authtoken" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or install via npm:" -ForegroundColor White
    Write-Host "  npm install -g ngrok" -ForegroundColor Gray
    Write-Host ""
    
    $continue = Read-Host "Do you want to continue anyway? (y/n)"
    if ($continue -ne 'y') {
        exit
    }
} else {
    Write-Host "✓ ngrok is installed" -ForegroundColor Green
}

Write-Host ""
Write-Host "Starting services..." -ForegroundColor Cyan

# Start Auth Server
Write-Host "Starting Auth Server on port 3000..." -ForegroundColor Yellow
$authProcess = Start-Process -FilePath "node" -ArgumentList "server.js" -WorkingDirectory "C:\Users\silen\Downloads\zypher\auth-server" -WindowStyle Normal -PassThru
Start-Sleep -Seconds 2

# Start Website
Write-Host "Starting Website on port 4000..." -ForegroundColor Yellow
$webProcess = Start-Process -FilePath "node" -ArgumentList "server.js" -WorkingDirectory "C:\Users\silen\Downloads\zypher\website" -WindowStyle Normal -PassThru
Start-Sleep -Seconds 2

Write-Host ""
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  Services Started!" -ForegroundColor Green
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Local Access:" -ForegroundColor White
Write-Host "  Website:    http://localhost:4000" -ForegroundColor Gray
Write-Host "  Auth Admin: http://localhost:3000" -ForegroundColor Gray
Write-Host ""

if ($ngrokInstalled) {
    Write-Host "To make it public, run:" -ForegroundColor Yellow
    Write-Host "  ngrok http 4000" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Then share the HTTPS URL ngrok gives you!" -ForegroundColor Green
    
    $startNgrok = Read-Host "Start ngrok now? (y/n)"
    if ($startNgrok -eq 'y') {
        Write-Host ""
        Write-Host "Starting ngrok..." -ForegroundColor Cyan
        ngrok http 4000
    }
} else {
    Write-Host "Install ngrok to make your site public:" -ForegroundColor Yellow
    Write-Host "  1. Download: https://ngrok.com/download" -ForegroundColor Gray
    Write-Host "  2. Run: ngrok http 4000" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or use port forwarding on your router to expose port 4000" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Press any key to exit (services will keep running)..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
