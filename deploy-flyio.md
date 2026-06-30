#  Deploy Zypher to Fly.io (Global)

## Why Fly.io?
- ✅ Free tier (3 VMs)
- ✅ Global deployment
- ✅ Automatic HTTPS
- ✅ Fast worldwide
- ✅ Easy CLI

---

## Step 1: Install Fly CLI

**Windows:**
```powershell
iwr https://fly.io/install.ps1 -useb | iex
```

**Or download from:** https://fly.io/docs/hands-on/install-flyctl/

---

## Step 2: Create Account

```bash
flyctl auth signup
```

Or go to: https://fly.io/app/sign-up

---

## Step 3: Login

```bash
flyctl auth login
```

---

## Step 4: Create Fly App

```bash
cd C:\Users\silen\Downloads\zypher

# Create auth server app
cd auth-server
flyctl launch --name zypher-auth --no-deploy
cd ..

# Create website app
cd website
flyctl launch --name zypher-website --no-deploy
cd ..
```

---

## Step 5: Configure Auth Server

Edit `auth-server/fly.toml`:

```toml
app = "zypher-auth"
primary_region = "iad"

[http_service]
  internal_port = 3000
  force_https = true
  auto_stop_machines = true
  auto_start_machines = true
  min_machines_running = 0

[env]
  PORT = "3000"
  JWT_SECRET = "your-random-secret"
  ENCRYPTION_KEY = "your-random-key"
```

---

## Step 6: Configure Website

Edit `website/fly.toml`:

```toml
app = "zypher-website"
primary_region = "iad"

[http_service]
  internal_port = 4000
  force_https = true
  auto_stop_machines = true
  auto_start_machines = true
  min_machines_running = 0

[env]
  PORT = "4000"
  STRIPE_SECRET_KEY = "sk_test_your_key"
  STRIPE_WEBHOOK_SECRET = "whsec_your_secret"
  EMAIL_USER = "your@gmail.com"
  EMAIL_PASS = "your_app_password"
  WEBSITE_URL = "https://zypher-website.fly.dev"
  AUTH_SERVER_URL = "https://zypher-auth.fly.dev"
```

---

## Step 7: Deploy

```bash
# Deploy auth server
cd auth-server
flyctl deploy
cd ..

# Deploy website
cd website
flyctl deploy
cd ..
```

---

## Step 8: Get Your URLs

After deployment:
- Auth: `https://zypher-auth.fly.dev`
- Website: `https://zypher-website.fly.dev`

---

## Step 9: Update Loader

```cpp
SelfAuth::api auth("zypher-auth.fly.dev", 443);
```

---

## Step 10: Setup Stripe Webhook

Stripe Dashboard → Webhooks:
- URL: `https://zypher-website.fly.dev/webhook`
- Event: `checkout.session.completed`

---

## Free Tier Limits

- **3 shared-cpu VMs**
- **256 MB RAM** each
- **3 GB persistent storage**
- **160 GB outbound transfer**
- **Auto-sleep** when not in use

---

## Custom Domain

```bash
# Add domain
flyctl certs create yourdomain.com -a zypher-website

# Update DNS at Namecheap:
# Type: A
# Name: @
# Value: (fly gives you IP)
```

---

## Database Options

Fly.io doesn't include database. Use:

1. **Supabase** (Free): https://supabase.com
2. **MongoDB Atlas** (Free): https://mongodb.com/cloud/atlas
3. **Fly Volumes** (Paid): Persistent storage

---

## Managing Your App

```bash
# View logs
flyctl logs -a zypher-auth
flyctl logs -a zypher-website

# Restart
flyctl restart -a zypher-auth
flyctl restart -a zypher-website

# Scale
flyctl scale count 2 -a zypher-website

# Update
cd website
flyctl deploy
```

---

## Advantages

✅ **Free tier** - 3 VMs free
✅ **Global** - Deploy to multiple regions
✅ **Fast** - Edge network
✅ **Easy CLI** - Simple commands
✅ **Auto-scaling** - Handles traffic

---

## Disadvantages

❌ **No database included** - Need external
❌ **Small free VMs** - 256 MB RAM
❌ **Auto-sleep** - Cold starts
 **Less popular** - Smaller community

---

## Global Deployment

Deploy to multiple regions:

```bash
flyctl regions add lhr -a zypher-website  # London
flyctl regions add nrt -a zypher-website  # Tokyo
flyctl regions add syd -a zypher-website  # Sydney
```

Your site will be fast worldwide!

---

## You're Live! 

Website: `https://zypher-website.fly.dev`
Auth: `https://zypher-auth.fly.dev`

Fast, global, and free!
