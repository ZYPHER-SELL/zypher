# Zypher - Public Access Guide

## Quick Start (Make It Public)

### Option 1: Using ngrok (Easiest)

1. **Download ngrok**: https://ngrok.com/download
2. **Sign up** for a free account and get your authtoken
3. **Run the setup script**:
   ```
   C:\Users\silen\Downloads\zypher\make-public.bat
   ```

   Or manually:
   ```powershell
   # Start auth server
   cd C:\Users\silen\Downloads\zypher\auth-server
   node server.js
   
   # In another terminal, start website
   cd C:\Users\silen\Downloads\zypher\website
   node server.js
   
   # In another terminal, create public tunnel
   ngrok http 4000
   ```

4. **Copy the public URL** ngrok gives you (looks like: `https://abc123.ngrok.io`)
5. **Share that URL** with anyone - they can access your website!

### Option 2: Port Forwarding (Permanent)

1. **Get your public IP**: https://whatismyipaddress.com
2. **Forward ports** on your router:
   - Port 3000 → Your PC's local IP (auth server)
   - Port 4000 → Your PC's local IP (website)
3. **Access via**: `http://YOUR_PUBLIC_IP:4000`

### Option 3: Deploy to Cloud (Best for Production)

**Recommended platforms:**
- **Vercel** (free): https://vercel.com
- **Railway** (free tier): https://railway.app
- **DigitalOcean** ($5/mo): https://digitalocean.com
- **Heroku** (free tier): https://heroku.com

---

## Fixing Auth Admin Login

If you can't login to the auth admin panel:

### 1. Make sure auth server is running:
```powershell
cd C:\Users\silen\Downloads\zypher\auth-server
node server.js
```

You should see:
```
Zypher Auth Server running on port 3000
Admin credentials: admin / admin123
```

### 2. Clear browser cache:
- Press `Ctrl + Shift + Delete`
- Clear cookies and cache
- Try logging in again

### 3. Check browser console:
- Press `F12`
- Go to Console tab
- Look for errors

### 4. Reset admin password:
Delete the `zypher_db.json` file in the auth-server folder and restart the server. It will recreate with default credentials.

---

## Current Setup

**Local URLs:**
- Website: http://localhost:4000
- Auth Admin: http://localhost:3000
- Website Admin: http://localhost:4000/admin

**Default Credentials:**
- Auth Admin: `admin` / `admin123`
- Website Admin: No login required (public)

---

## Testing the Full Flow

1. **Start all services:**
   ```
   C:\Users\silen\Downloads\zypher\start-all.bat
   ```

2. **Generate a license key:**
   - Go to http://localhost:3000
   - Login with admin/admin123
   - Click "Generate Key"
   - Copy the key

3. **Test the loader:**
   - Run `zypher.exe`
   - Enter the license key
   - Should authenticate successfully

4. **Test purchase flow:**
   - Go to http://localhost:4000
   - Add a product via admin panel
   - Click "Buy Now"
   - Use Stripe test card: `4242 4242 4242 4242`
   - Check email for license key

---

## Making It Public with ngrok

### Step-by-step:

1. **Install ngrok:**
   ```powershell
   npm install -g ngrok
   ```

2. **Sign up at ngrok.com** and get your authtoken

3. **Add authtoken:**
   ```powershell
   ngrok config add-authtoken YOUR_TOKEN_HERE
   ```

4. **Start services:**
   ```powershell
   # Terminal 1
   cd C:\Users\silen\Downloads\zypher\auth-server
   node server.js
   
   # Terminal 2
   cd C:\Users\silen\Downloads\zypher\website
   node server.js
   
   # Terminal 3
   ngrok http 4000
   ```

5. **Copy the HTTPS URL** from ngrok output
   - Example: `https://abc123def.ngrok.io`
   - Share this with anyone!

6. **Update the loader** to use your ngrok URL:
   - Edit `self_auth_stub.h`
   - Change `localhost` to your ngrok URL (without https://)
   - Rebuild the loader

---

## Troubleshooting

### "Cannot connect to server"
- Make sure the service is running
- Check if port is blocked by firewall
- Try running as administrator

### "Invalid credentials"
- Default is `admin` / `admin123`
- Check caps lock
- Clear browser cache

### ngrok URL not working
- ngrok free tier URLs change each restart
- Upgrade to paid plan for custom domains
- Check ngrok dashboard for status

### Stripe webhook not working
- Use ngrok to expose webhook URL
- Update Stripe dashboard with new URL
- Test with Stripe CLI first

---

## Security Notes

**For production:**
1. Change default admin password immediately
2. Use HTTPS (ngrok provides this for free)
3. Keep your `.env` file secure
4. Don't commit database files to git
5. Regular backups of `db.json` files
6. Monitor auth logs for suspicious activity

---

## Support

If you're still having issues:
1. Check that Node.js is installed: `node --version`
2. Check that ports aren't in use: `netstat -ano | findstr :3000`
3. Restart all services
4. Check Windows Firewall settings
5. Try running as administrator
