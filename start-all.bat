@echo off
echo Starting Zypher Services...

echo.
echo [1/3] Starting Auth Server...
start "Zypher Auth Server" cmd /k "cd /d C:\Users\silen\Downloads\zypher\auth-server && node server.js"
timeout /t 3 /nobreak >nul

echo [2/3] Starting Website...
start "Zypher Website" cmd /k "cd /d C:\Users\silen\Downloads\zypher\website && node server.js"
timeout /t 3 /nobreak >nul

echo [3/3] Setting up public access with ngrok...
echo.
echo If you don't have ngrok installed:
echo 1. Download from: https://ngrok.com/download
echo 2. Run: ngrok config add-authtoken YOUR_TOKEN
echo 3. Then run: ngrok http 4000
echo.
echo Or use this PowerShell command to start ngrok:
echo start-process ngrok -ArgumentList "http 4000"
echo.
echo Services started! Press any key to continue...
pause >nul
