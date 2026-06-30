@echo off
echo ========================================
echo   Zypher - Deployment Helper
echo ========================================
echo.
echo Choose your deployment platform:
echo.
echo [1] Railway (Easiest - Recommended)
echo [2] Render (Simple)
echo [3] Vercel (Fast)
echo [4] DigitalOcean (Full Control - $5/mo)
echo [5] Fly.io (Global)
echo [6] View Comparison
echo [7] Exit
echo.

set /p choice="Enter your choice (1-7): "

if "%choice%"=="1" goto railway
if "%choice%"=="2" goto render
if "%choice%"=="3" goto vercel
if "%choice%"=="4" goto digitalocean
if "%choice%"=="5" goto flyio
if "%choice%"=="6" goto compare
if "%choice%"=="7" goto exit

echo Invalid choice. Please try again.
pause
goto start

:railway
echo.
echo ========================================
echo   Railway Deployment
echo ========================================
echo.
echo Railway is the easiest option!
echo.
echo Steps:
echo 1. Push your code to GitHub
echo 2. Go to https://railway.app
echo 3. Login with GitHub
echo 4. Import your repo
echo 5. Add environment variables
echo 6. Deploy!
echo.
echo Full guide: deploy-railway.md
echo.
echo Opening Railway...
start https://railway.app
pause
goto exit

:render
echo.
echo ========================================
echo   Render Deployment
echo ========================================
echo.
echo Render is simple and has a free tier!
echo.
echo Steps:
echo 1. Add render.yaml to your repo
echo 2. Push to GitHub
echo 3. Go to https://render.com
echo 4. Import blueprint
echo 5. Deploy!
echo.
echo Full guide: deploy-render.md
echo.
echo Opening Render...
start https://render.com
pause
goto exit

:vercel
echo.
echo ========================================
echo   Vercel Deployment
echo ========================================
echo.
echo Vercel has the best free tier!
echo.
echo Steps:
echo 1. Push to GitHub
echo 2. Go to https://vercel.com
echo 3. Import repo
echo 4. Deploy!
echo.
echo Full guide: deploy-vercel.md
echo.
echo Opening Vercel...
start https://vercel.com
pause
goto exit

:digitalocean
echo.
echo ========================================
echo   DigitalOcean Deployment
echo ========================================
echo.
echo DigitalOcean gives you full control!
echo Cost: $5/month
echo.
echo Steps:
echo 1. Create account at digitalocean.com
echo 2. Create droplet (Ubuntu)
echo 3. SSH in and setup
echo 4. Deploy!
echo.
echo Full guide: deploy-digitalocean.md
echo.
echo Opening DigitalOcean...
start https://digitalocean.com
pause
goto exit

:flyio
echo.
echo ========================================
echo   Fly.io Deployment
echo ========================================
echo.
echo Fly.io is great for global deployment!
echo.
echo Steps:
echo 1. Install flyctl
echo 2. Login
echo 3. Create apps
echo 4. Deploy!
echo.
echo Full guide: deploy-flyio.md
echo.
echo Opening Fly.io...
start https://fly.io
pause
goto exit

:compare
echo.
echo ========================================
echo   Platform Comparison
echo ========================================
echo.
echo Railway:   Easiest, DB included, 500hrs free
echo Render:    Simple, DB included, 750hrs free
echo Vercel:    Fastest, best free tier, no DB
echo DOcean:    Full control, $5/mo, reliable
echo Fly.io:    Global, 3 VMs free, fast
echo.
echo See CHOOSE_PLATFORM.md for full comparison
echo.
pause
goto exit

:exit
echo.
echo Good luck with your deployment!
echo.
pause
