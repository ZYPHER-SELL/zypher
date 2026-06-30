#  Deploy Zypher to Render (Simple)

## Why Render?
- ✅ Free tier available
- ✅ Easy setup
- ✅ Automatic HTTPS
- ✅ Built-in database
- ✅ Good for Node.js

---

## Step 1: Prepare Your Code

1. Create `render.yaml` in root directory:

```yaml
services:
  - type: web
    name: zypher-auth
    env: node
    buildCommand: cd auth-server && npm install
    startCommand: cd auth-server && node server.js
    envVars:
      - key: PORT
        value: 3000
      - key: JWT_SECRET
        generateValue: true
      - key: ENCRYPTION_KEY
        generateValue: true

  - type: web
    name: zypher-website
    env: node
    buildCommand: cd website && npm install
    startCommand: cd website && node server.js
    envVars:
      - key: PORT
        value: 4000
      - key: STRIPE_SECRET_KEY
        sync: false
      - key: STRIPE_WEBHOOK_SECRET
        sync: false
      - key: EMAIL_USER
        sync: false
      - key: EMAIL_PASS
        sync: false
      - key: WEBSITE_URL
        fromService:
          name: zypher-website
          type: web
          property: host
      - key: AUTH_SERVER_URL
        fromService:
          name: zypher-auth
          type: web
          property: host

  - type: pserv
    name: zypher-db
    env: node
    buildCommand: echo "No build needed"
    startCommand: echo "Database service"
```

2. Push to GitHub:
   ```powershell
   git add .
   git commit -m "Add Render config"
   git push
   ```

---

## Step 2: Setup Render

1. Go to https://render.com
2. Click "Get Started" → "Sign up with GitHub"
3. Click "New +" → "Blueprint"
4. Connect your GitHub repo
5. Render will auto-detect `render.yaml`
6. Click "Apply"

---

## Step 3: Configure Services

**For zypher-auth:**
1. Go to service settings
2. Add environment variables:
   ```
   PORT=3000
   JWT_SECRET=(auto-generated)
   ENCRYPTION_KEY=(auto-generated)
   ```

**For zypher-website:**
1. Add environment variables:
   ```
   PORT=4000
   STRIPE_SECRET_KEY=sk_test_your_key
   STRIPE_WEBHOOK_SECRET=whsec_your_secret
   EMAIL_USER=your@gmail.com
   EMAIL_PASS=your_app_password
   ```

---

## Step 4: Add Database

1. Click "New +" → "PostgreSQL"
2. Name: `zypher-database`
3. Choose free plan
4. Copy connection string
5. Add to both services as `DATABASE_URL`

---

## Step 5: Get Your URLs

After deployment, you'll get:
- Auth: `https://zypher-auth.onrender.com`
- Website: `https://zypher-website.onrender.com`

**Your website is live!**

---

## Step 6: Update Loader

Edit `self_auth_stub.h`:
```cpp
SelfAuth::api auth("zypher-auth.onrender.com", 443);
```

---

## Step 7: Setup Stripe Webhook

1. Stripe Dashboard → Webhooks
2. Add: `https://zypher-website.onrender.com/webhook`
3. Select: `checkout.session.completed`
4. Copy secret to Render env vars

---

## Free Tier Limits

- **750 hours/month** (enough for 2 services)
- **Sleeps after 15 min** of inactivity (free plan)
- **512 MB RAM** per service
- **1 GB disk** storage

**Note:** Free tier services sleep. First request after sleep takes ~30 seconds.

---

## Paid Plan ($7/month per service)

- No sleep
- More resources
- Better performance
- Priority support

---

## Custom Domain

1. Go to service settings
2. Click "Add Custom Domain"
3. Enter: `yourdomain.com`
4. Update DNS at Namecheap:
   - Type: CNAME
   - Name: @
   - Value: `zypher-website.onrender.com`

---

## Updating

```powershell
git push
```

Render auto-deploys!

---

## Troubleshooting

**Service sleeping?**
- Upgrade to paid plan
- Or use uptime monitor (UptimeRobot free)

**Build failed?**
- Check logs in Render dashboard
- Verify `render.yaml` syntax

**Database connection error?**
- Check DATABASE_URL is set
- Verify PostgreSQL service is running

---

## You're Live! 🚀

Website: `https://zypher-website.onrender.com`
Auth: `https://zypher-auth.onrender.com`
