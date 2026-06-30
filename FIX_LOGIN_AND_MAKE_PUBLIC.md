# Zypher - Complete Setup & Public Access Guide

## 🚀 Quick Fix: Can't Login to Auth Admin

### Problem: You can't login to http://localhost:3000

### Solution:

1. **Make sure auth server is running:**
   ```powershell
   cd C:\Users\silen\Downloads\zypher\auth-server
   node server.js
   ```
   
   You should see:
   ```
   Zypher Auth Server running on port 3000
   Admin credentials: admin / admin123
   ```

2. **Clear your browser cache:**
   - Press `Ctrl + Shift + Delete`
   - Select "Cookies" and "Cached images"
   - Click "Clear data"
   - Refresh the page

3. **Try logging in again:**
   - Username: `admin`
   - Password: `admin123`

4. **Still not working? Reset the database:**
   - Delete file: `C:\Users\silen\Downloads\zypher\auth-server\zypher_db.json`
   - Restart the auth server
   - Try logging in again

---

## 🌍 Make Your Website Public (So Anyone Can Access)

### Method 1: ngrok (Easiest - 5 minutes)

**What is ngrok?** It creates a public URL that tunnels to your local computer.

**Steps:**

1. **Download ngrok:**
   - Go to: https://ngrok.com/download
   - Download for Windows
   - Extract the zip file

2. **Sign up for free account:**
   - Go to: https://ngrok.com/signup
   - Get your authtoken from the dashboard

3. **Add your authtoken:**
   ```powershell
   ngrok config add-authtoken YOUR_TOKEN_HERE
   ```

4. **Start your services:**
   ```powershell
   # Terminal 1 - Auth Server
   cd C:\Users\silen\Downloads\zypher\auth-server
   node server.js
   
   # Terminal 2 - Website
   cd C:\Users\silen\Downloads\zypher\website
   node server.js
   ```

5. **Create public tunnel:**
   ```powershell
   # Terminal 3
   ngrok http 4000
   ```

6. **Copy the public URL:**
   - ngrok will show you a URL like: `https://abc123def.ngrok.io`
   - **This is your public website URL!**
   - Share it with anyone

**Note:** Free ngrok URLs change each time you restart. Paid plans get custom domains.

---

### Method 2: Port Forwarding (Permanent)

**Steps:**

1. **Get your public IP:**
   - Go to: https://whatismyipaddress.com
   - Copy your IP address

2. **Find your local IP:**
   ```powershell
   ipconfig
   ```
   - Look for "IPv4 Address" (usually 192.168.x.x)

3. **Forward ports on your router:**
   - Login to your router (usually 192.168.1.1)
   - Find "Port Forwarding" section
   - Add rules:
     - Port 3000 → Your local IP (auth server)
     - Port 4000 → Your local IP (website)

4. **Access from anywhere:**
   - Website: `http://YOUR_PUBLIC_IP:4000`
   - Auth Admin: `http://YOUR_PUBLIC_IP:3000`

**Warning:** Exposing your IP can be a security risk. Use a firewall!

---

### Method 3: Deploy to Cloud (Best for Production)

**Recommended platforms:**

1. **Vercel** (Free, Easy)
   - https://vercel.com
   - Deploy website frontend
   - Use serverless functions for backend

2. **Railway** (Free tier)
   - https://railway.app
   - Deploy both servers
   - Automatic HTTPS

3. **DigitalOcean** ($5/month)
   - https://digitalocean.com
   - Full control
   - Get a domain name

4. **Heroku** (Free tier)
   - https://heroku.com
   - Easy deployment
   - Good for testing

---

## 📋 Complete Setup Checklist

### Phase 1: Local Testing
- [ ] Auth server running (port 3000)
- [ ] Website running (port 4000)
- [ ] Can login to auth admin (admin/admin123)
- [ ] Can generate license keys
- [ ] Can add products via website admin
- [ ] Test purchase with Stripe test card

### Phase 2: Email Setup
- [ ] Create Gmail account (or use existing)
- [ ] Enable 2-Factor Authentication
- [ ] Generate App Password
- [ ] Add to `website/.env`:
  ```
  EMAIL_USER=your_email@gmail.com
  EMAIL_PASS=your_app_password
  ```

### Phase 3: Stripe Setup
- [ ] Create Stripe account (https://stripe.com)
- [ ] Get API keys from dashboard
- [ ] Add to `website/.env`:
  ```
  STRIPE_SECRET_KEY=sk_test_...
  STRIPE_WEBHOOK_SECRET=whsec_...
  ```
- [ ] Test with card: `4242 4242 4242 4242`

### Phase 4: Make Public
- [ ] Choose method (ngrok/port forwarding/cloud)
- [ ] Setup public access
- [ ] Test from different device/network
- [ ] Update loader to use public URL

### Phase 5: Production
- [ ] Get domain name
- [ ] Setup SSL/HTTPS
- [ ] Switch to Stripe live keys
- [ ] Change admin passwords
- [ ] Setup monitoring
- [ ] Start marketing!

---

## 🔧 Troubleshooting Guide

### Auth Admin Login Issues

**Problem:** "Invalid credentials"
- **Fix:** Use `admin` / `admin123` (case-sensitive)
- **Fix:** Clear browser cache
- **Fix:** Delete `zypher_db.json` and restart

**Problem:** "Cannot connect to server"
- **Fix:** Make sure auth server is running
- **Fix:** Check port 3000 isn't blocked
- **Fix:** Try running as administrator

**Problem:** Page loads but login fails
- **Fix:** Open browser console (F12)
- **Fix:** Check for CORS errors
- **Fix:** Verify server is responding: http://localhost:3000/api/admin/stats

### Website Not Accessible

**Problem:** Can't access from another device
- **Fix:** Use ngrok to create public URL
- **Fix:** Or setup port forwarding
- **Fix:** Check Windows Firewall

**Problem:** ngrok URL not working
- **Fix:** Make sure website is running on port 4000
- **Fix:** Check ngrok is connected
- **Fix:** Try restarting ngrok

### Email Not Sending

**Problem:** Purchase succeeds but no email
- **Fix:** Check Gmail app password
- **Fix:** Enable 2FA on Gmail first
- **Fix:** Check spam folder
- **Fix:** Verify email in `.env` is correct

### Stripe Issues

**Problem:** Checkout fails
- **Fix:** Check Stripe API keys
- **Fix:** Use test mode first
- **Fix:** Verify webhook URL

**Problem:** Webhook not received
- **Fix:** Use ngrok for webhook URL
- **Fix:** Check Stripe dashboard for errors
- **Fix:** Test with Stripe CLI

---

## 📱 Testing From Another Device

### Using ngrok:
1. Start ngrok: `ngrok http 4000`
2. Copy the HTTPS URL
3. Open on phone/another computer
4. Should work!

### Using Port Forwarding:
1. Get public IP
2. Forward port 4000
3. Access: `http://PUBLIC_IP:4000`
4. May need to allow through Windows Firewall

---

## 🔐 Security Best Practices

1. **Change default passwords immediately**
2. **Use HTTPS** (ngrok provides this free)
3. **Keep `.env` files secure** (don't share)
4. **Regular backups** of database files
5. **Monitor auth logs** for suspicious activity
6. **Use strong passwords** for admin accounts
7. **Enable rate limiting** (already done)
8. **Keep Node.js updated**

---

##  Quick Reference

### Local URLs:
- Website: http://localhost:4000
- Auth Admin: http://localhost:3000
- Website Admin: http://localhost:4000/admin

### Default Credentials:
- Auth Admin: `admin` / `admin123`

### Test Card:
- Stripe: `4242 4242 4242 4242`

### Commands:
```powershell
# Start auth server
cd C:\Users\silen\Downloads\zypher\auth-server
node server.js

# Start website
cd C:\Users\silen\Downloads\zypher\website
node server.js

# Make public
ngrok http 4000
```

---

## 🎯 You're Ready!

Once you can:
1. ✅ Login to auth admin
2. ✅ Generate license keys
3. ✅ Make website public
4. ✅ Process test payment
5. ✅ Receive email with key

**You're ready to start selling!** 💰

Share your public URL, add products, and watch the sales come in!
