# 🚀 Zypher - Complete Commercial Solution

## What You Got

### ✅ Auth Server (Port 3000)
- License key validation
- HWID locking (anti-sharing)
- Admin dashboard
- Rate limiting
- Encrypted API responses
- **Admin Login:** http://localhost:3000
- **Credentials:** `admin` / `admin123`

### ✅ Commercial Website (Port 4000)
- Beautiful landing page
- Stripe payment integration
- Auto-email with license key
- Admin panel at `/admin`
- **Website:** http://localhost:4000
- **Admin:** http://localhost:4000/admin

### ✅ Client Loader
- Dark purple theme
- Real auth integration
- HWID detection
- **Build:** Open `zypher.sln` in Visual Studio
- **Run:** `zypher\x64\Release\zypher.exe`

---

##  Make It Public (So Anyone Can Access)

### Option 1: ngrok (Easiest - 5 minutes)

1. **Download ngrok**: https://ngrok.com/download
2. **Sign up** (free) and get your authtoken
3. **Run this command**:
   ```powershell
   ngrok http 4000
   ```
4. **Copy the HTTPS URL** it gives you (example: `https://abc123.ngrok.io`)
5. **Share that URL** - anyone can access your website!

### Option 2: Port Forwarding (Permanent)

1. Get your public IP: https://whatismyipaddress.com
2. Forward port 4000 on your router to your PC
3. Access via: `http://YOUR_PUBLIC_IP:4000`

### Option 3: Deploy to Cloud (Best for Production)

Use Vercel, Railway, or DigitalOcean for a permanent domain.

---

## 🔧 Quick Start

### 1. Start All Services
```powershell
cd C:\Users\silen\Downloads\zypher
.\start-all.bat
```

### 2. Test Locally
- Website: http://localhost:4000
- Auth Admin: http://localhost:3000 (admin/admin123)

### 3. Make Public
```powershell
ngrok http 4000
```

---

##  How Purchases Work

1. Customer visits your website
2. Clicks "Buy Now" on a product
3. Enters email address
4. Redirected to Stripe checkout (secure)
5. Pays with card
6. **Automatically receives email with:**
   - Order receipt
   - License key (XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX)
   - Download instructions
7. Customer downloads loader
8. Enters license key
9. Auth server validates + locks to their HWID
10. Customer gets access!

---

## 🔐 Admin Panels

### Auth Admin (http://localhost:3000)
- Generate license keys
- View auth logs
- Revoke licenses
- See failed attempts
- **Login:** admin / admin123

### Website Admin (http://localhost:4000/admin)
- Add products
- View orders
- View license keys
- Track sales

---

## 📦 File Structure

```
C:\Users\silen\Downloads\zypher\
│
├── auth-server/              # Authentication server
│   ├── server.js
│   ├── zypher_db.json        # Database
│   └── public/
│       └── index.html        # Admin dashboard
│
├── website/                  # Commercial website
│   ├── server.js
│   ├── .env.example
│   └── public/
│       ├── index.html        # Landing page
│       ├── admin.html        # Admin panel
│       ├── success.html      # Payment success
│       ├── cancel.html       # Payment cancelled
│       ├── css/
│       └── js/
│
── zypher/                   # Client loader
│   ├── zypher.sln
│   └── src/
│
├── start-all.bat             # Start all services
├── make-public.bat           # Setup ngrok
── setup-public.ps1          # PowerShell setup
├── SETUP.md                  # Full setup guide
└── PUBLIC_ACCESS.md          # Public access guide
```

---

##  Setup Checklist

### Immediate (Testing):
- [x] Auth server running
- [x] Website running
- [ ] Add Stripe test keys to `website/.env`
- [ ] Add Gmail credentials to `website/.env`
- [ ] Test with Stripe card: `4242 4242 4242 4242`

### Production:
- [ ] Get domain name
- [ ] Setup VPS or use ngrok
- [ ] Get Stripe live keys
- [ ] Setup SSL (HTTPS)
- [ ] Change admin passwords
- [ ] Configure webhook
- [ ] Test full purchase flow
- [ ] Start marketing!

---

## 🔑 Default Credentials

**Auth Admin:**
- Username: `admin`
- Password: `admin123`

**Website:**
- No login required (public)
- Admin panel at `/admin` (no auth yet - add your own)

---

## 🛠️ Troubleshooting

### Can't login to auth admin?
1. Make sure auth server is running on port 3000
2. Clear browser cache (Ctrl+Shift+Delete)
3. Try: `admin` / `admin123`
4. Check browser console (F12) for errors
5. Delete `zypher_db.json` and restart server

### Website not accessible publicly?
1. Use ngrok: `ngrok http 4000`
2. Share the HTTPS URL it gives you
3. Or setup port forwarding on your router

### Email not sending?
1. Check Gmail app password is correct
2. Enable 2FA on Gmail first
3. Generate app password at: https://myaccount.google.com/apppasswords
4. Check spam folder

### Stripe not working?
1. Use test mode first
2. Test card: `4242 4242 4242 4242`
3. Check Stripe dashboard for errors
4. Verify webhook URL is correct

---

## 📞 Support

**Documentation:**
- `SETUP.md` - Complete setup guide
- `PUBLIC_ACCESS.md` - Making it public
- `README.md` in each folder

**Services to keep running:**
- Auth server (port 3000)
- Website (port 4000)

Both can run on the same machine simultaneously.

---

## 🚀 Ready to Sell?

1. **Add products** via website admin panel
2. **Setup Stripe** with real keys
3. **Make public** with ngrok or deploy to cloud
4. **Share your URL** with customers
5. **Monitor sales** in admin panel
6. **Generate license keys** manually or auto via Stripe

**You're ready to make money!** 💰
