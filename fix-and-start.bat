@echo off
echo ========================================
echo   Zypher - Fresh Start & Login Fix
echo ========================================
echo.

echo [1/4] Stopping existing services...
taskkill /F /FI "WINDOWTITLE eq Zypher Auth*" 2>nul
taskkill /F /FI "WINDOWTITLE eq Zypher Web*" 2>nul
taskkill /F /FI "WINDOWTITLE eq Zypher*" 2>nul
timeout /t 2 /nobreak >nul

echo [2/4] Resetting auth database...
if exist "C:\Users\silen\Downloads\zypher\auth-server\zypher_db.json" (
    del "C:\Users\silen\Downloads\zypher\auth-server\zypher_db.json"
    echo Deleted old database
) else (
    echo No database to delete
)
echo.

echo [3/4] Starting Auth Server...
start "Zypher Auth Server" cmd /k "cd /d C:\Users\silen\Downloads\zypher\auth-server && node server.js"
timeout /t 3 /nobreak >nul

echo [4/4] Starting Website...
start "Zypher Website" cmd /k "cd /d C:\Users\silen\Downloads\zypher\website && node server.js"
timeout /t 2 /nobreak >nul

echo.
echo ========================================
echo   Services Started!
echo ========================================
echo.
echo Local URLs:
echo   Website:    http://localhost:4000
echo   Auth Admin: http://localhost:3000
echo.
echo Login Credentials:
echo   Username: admin
echo   Password: admin123
echo.
echo To make public:
echo   ngrok http 4000
echo.
echo ========================================
echo.
echo Opening browser...
timeout /t 3 /nobreak >nul
start http://localhost:3000
echo.
echo Press any key to exit...
pause >nul
