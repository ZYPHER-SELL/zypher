@echo off
echo ====================================
echo   Zypher - Public Access Setup
echo ====================================
echo.

echo Step 1: Installing ngrok...
npm install -g ngrok 2>nul
if %errorlevel% neq 0 (
    echo ngrok not found in npm. Downloading manually...
    echo Please download ngrok from: https://ngrok.com/download
    echo Extract it and add to your PATH
    pause
    exit /b
)

echo.
echo Step 2: Starting services...
echo Starting Auth Server on port 3000...
start "Zypher Auth" cmd /k "cd /d C:\Users\silen\Downloads\zypher\auth-server && node server.js"
timeout /t 2 /nobreak >nul

echo Starting Website on port 4000...
start "Zypher Web" cmd /k "cd /d C:\Users\silen\Downloads\zypher\website && node server.js"
timeout /t 2 /nobreak >nul

echo.
echo Step 3: Creating public tunnel...
echo.
echo Starting ngrok tunnel to port 4000...
echo Your public URL will appear below:
echo.
echo ====================================
echo.

ngrok http 4000 --log=stdout

pause
