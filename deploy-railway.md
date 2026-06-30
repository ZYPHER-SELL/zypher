#  Deploy Zypher to Railway (Easiest)

## Why Railway?
- ✅ Free tier (500 hours/month)
- ✅ Database included
- ✅ Automatic HTTPS
- ✅ Easy deployment
- ✅ Custom domains
- ✅ 10-minute setup

---

## Step 1: Create GitHub Repository

1. Go to https://github.com
2. Click "New Repository"
3. Name it: `zypher`
4. Make it **Public**
5. Click "Create repository"

6. Upload your code:
   ```powershell
   cd C:\Users\silen\Downloads\zypher
   
   git init
   git add .
   git commit -m "Initial commit"
   git branch -M main
   git remote add origin https://github.com/YOUR_USERNAME/zypher.git
   git push -u origin main
   ```

---

## Step 2: Setup Railway

1. Go to https://railway.app
2. Click "Login" → "Login with GitHub"
3. Click "New Project"
4. Click "Deploy from GitHub repo"
5. Select your `zypher` repository

---

## Step 3: Deploy Auth Server

1. Click "+ New" → "Empty Service"
2. Name it: `zypher-auth`
3. Go to "Settings" tab
4. Set:
   - **Start Command:** `node auth-server/server.js`
   - **Root Directory:** (leave blank)

5. Go to "Variables" tab
6. Add these environment variables:
   ```
   PORT=3000
   JWT_SECRET=your-random-secret-key-here
   ENCRYPTION_KEY=your-random-encryption-key-here
   ```

7. Go to "Deploy" tab
8. Click "Deploy"

---

## Step 4: Deploy Website

1. Click "+ New" → "Empty Service"
2. Name it: `zypher-website`
3. Go to "Settings" tab
4. Set:
   - **Start Command:** `node website/server.js`

5. Go to "Variables" tab
6. Add:
   ```
   PORT=4000
   STRIPE_SECRET_KEY=sk_test_your_key
   STRIPE_WEBHOOK_SECRET=whsec_your_secret
   EMAIL_USER=your@gmail.com
   EMAIL_PASS=your_app_password
   WEBSITE_URL=https://zypher-website.up.railway.app
   AUTH_SERVER_URL=https://zypher-auth.up.railway.app
   ```

7. Click "Deploy"

---

## Step 5: Add Database

1. Click "+ New" → "Database" → "PostgreSQL"
2. Wait for it to provision
3. Copy the connection string
4. Add to both services as `DATABASE_URL`

---

## Step 6: Get Your Public URL

1. Go to `zypher-website` service
2. Click "Settings"
3. Find "Domains" section
4. Click "Generate Domain"
5. Your URL: `https://zypher-website.up.railway.app`

**That's your public website!**

---

## Step 7: Update Loader

Edit `zypher/zypher/src/gui/self_auth_stub.h`:

Change:
```cpp
SelfAuth::api auth("localhost", 3000);
```

To:
```cpp
SelfAuth::api auth("zypher-auth.up.railway.app", 443);
```

Rebuild the loader.

---

## Step 8: Setup Stripe Webhook

1. Go to Stripe Dashboard → Developers → Webhooks
2. Add endpoint: `https://zypher-website.up.railway.app/webhook`
3. Select event: `checkout.session.completed`
4. Copy webhook secret
5. Add to Railway variables as `STRIPE_WEBHOOK_SECRET`

---

## Step 9: Add Custom Domain (Optional)

1. Buy domain from Namecheap (~$10/year)
2. In Railway, go to "Settings" → "Domains"
3. Click "Add Custom Domain"
4. Enter: `yourdomain.com`
5. Update DNS records at Namecheap:
   - Type: CNAME
   - Name: @
   - Value: `zypher-website.up.railway.app`

---

## Costs

- **Free tier:** 500 hours/month (enough for 2 services)
- **After free tier:** ~$5/month
- **Domain:** ~$10/year (optional)

---

## Updating Your Site

```powershell
# Make changes to your code
git add .
git commit -m "Update"
git push
```

Railway automatically redeploys!

---

## Troubleshooting

**Site not loading?**
- Check Railway logs in dashboard
- Verify environment variables are set
- Check if services are running

**Database connection failed?**
- Make sure DATABASE_URL is set
- Check if PostgreSQL service is running

**Stripe webhook not working?**
- Verify webhook URL is correct
- Check Stripe dashboard for errors

---

## You're Live! 🚀

Your website is now:
- ✅ Publicly accessible
- ✅ HTTPS secured
- ✅ 24/7 uptime
- ✅ Auto-scaling
- ✅ Database included

Share your Railway URL and start selling!
