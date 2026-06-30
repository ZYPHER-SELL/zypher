#  Deploy Zypher to Vercel (Fast)

## Why Vercel?
- ✅ Generous free tier
- ✅ Lightning fast CDN
- ✅ Automatic HTTPS
- ✅ Great for frontend
- ✅ Serverless functions

---

## Step 1: Restructure for Vercel

Vercel works best with:
- Frontend in `/public`
- API routes in `/api`

Create `vercel.json`:

```json
{
  "version": 2,
  "builds": [
    {
      "src": "website/server.js",
      "use": "@vercel/node"
    },
    {
      "src": "auth-server/server.js",
      "use": "@vercel/node"
    }
  ],
  "routes": [
    {
      "src": "/api/(.*)",
      "dest": "auth-server/server.js"
    },
    {
      "src": "/(.*)",
      "dest": "website/server.js"
    }
  ]
}
```

---

## Step 2: Push to GitHub

```powershell
cd C:\Users\silen\Downloads\zypher
git add .
git commit -m "Add Vercel config"
git push
```

---

## Step 3: Deploy to Vercel

1. Go to https://vercel.com
2. Click "Sign Up" → "Continue with GitHub"
3. Click "Add New..." → "Project"
4. Import your `zypher` repository
5. Vercel auto-detects settings
6. Click "Deploy"

---

## Step 4: Add Environment Variables

In Vercel dashboard:
1. Go to your project → "Settings" → "Environment Variables"
2. Add:
   ```
   STRIPE_SECRET_KEY=sk_test_your_key
   STRIPE_WEBHOOK_SECRET=whsec_your_secret
   EMAIL_USER=your@gmail.com
   EMAIL_PASS=your_app_password
   JWT_SECRET=your-random-secret
   ENCRYPTION_KEY=your-random-key
   ```

---

## Step 5: Get Your URL

After deployment:
- Website: `https://zypher.vercel.app`
- API: `https://zypher.vercel.app/api`

---

## Step 6: Update Loader

```cpp
SelfAuth::api auth("zypher.vercel.app", 443);
```

---

## Step 7: Setup Webhook

Stripe Dashboard → Webhooks:
- URL: `https://zypher.vercel.app/api/webhook`
- Event: `checkout.session.completed`

---

## Free Tier Limits

- **100 GB bandwidth/month**
- **Unlimited deployments**
- **Serverless functions:** 100 GB-hours
- **Automatic HTTPS**

---

## Custom Domain

1. Vercel Dashboard → "Domains"
2. Add: `yourdomain.com`
3. Update DNS:
   - Type: A
   - Name: @
   - Value: `76.76.21.21`
   - OR CNAME to `cname.vercel-dns.com`

---

## Advantages

✅ **Fastest CDN** - Global edge network
✅ **Best free tier** - Very generous
✅ **Easy updates** - Just git push
✅ **Automatic SSL** - No config needed
✅ **Great analytics** - Built-in

---

## Disadvantages

❌ **Serverless only** - No long-running processes
❌ **Need to adapt code** - For serverless functions
 **Database not included** - Use external (Supabase, MongoDB Atlas)

---

## Database Options for Vercel

1. **Supabase** (Free): https://supabase.com
2. **MongoDB Atlas** (Free): https://mongodb.com/cloud/atlas
3. **PlanetScale** (Free): https://planetscale.com

---

## Updating

```powershell
git push
```

Vercel deploys automatically!

---

## You're Live! 🚀

Website: `https://zypher.vercel.app`

Fast, reliable, and free!
