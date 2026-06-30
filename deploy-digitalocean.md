#  Deploy Zypher to DigitalOcean (Full Control)

## Why DigitalOcean?
- ✅ Full control
- ✅ Reliable ($5/month)
- ✅ Custom domains
- ✅ Scalable
- ✅ Professional

---

## Step 1: Create Account

1. Go to https://digitalocean.com
2. Sign up (get $200 free credit for 60 days)
3. Verify email

---

## Step 2: Create Droplet (Server)

1. Click "Create" → "Droplets"
2. Choose:
   - **Image:** Ubuntu 22.04 LTS
   - **Plan:** Basic → $5/month (1 GB RAM)
   - **Region:** Closest to your customers
   - **Authentication:** Password (or SSH key)
3. Click "Create Droplet"
4. Wait 1-2 minutes

---

## Step 3: Connect to Server

**Windows:**
1. Download PuTTY: https://putty.org
2. Enter your droplet IP
3. Login as `root`

**Or use PowerShell:**
```powershell
ssh root@YOUR_DROPLET_IP
```

---

## Step 4: Setup Server

```bash
# Update system
apt update && apt upgrade -y

# Install Node.js
curl -fsSL https://deb.nodesource.com/setup_18.x | bash -
apt install -y nodejs

# Install PM2 (process manager)
npm install -g pm2

# Install Nginx
apt install -y nginx

# Install Git
apt install -y git
```

---

## Step 5: Clone Your Code

```bash
cd /var/www
git clone https://github.com/YOUR_USERNAME/zypher.git
cd zypher

# Install dependencies
cd auth-server && npm install && cd ..
cd website && npm install && cd ..
```

---

## Step 6: Setup Environment Variables

```bash
# Create .env files
cd /var/www/zypher/website
nano .env
```

Add:
```
PORT=4000
STRIPE_SECRET_KEY=sk_test_your_key
STRIPE_WEBHOOK_SECRET=whsec_your_secret
EMAIL_USER=your@gmail.com
EMAIL_PASS=your_app_password
WEBSITE_URL=https://yourdomain.com
AUTH_SERVER_URL=https://api.yourdomain.com
```

Save (Ctrl+X, Y, Enter)

---

## Step 7: Start Services with PM2

```bash
cd /var/www/zypher

# Start auth server
pm2 start auth-server/server.js --name zypher-auth

# Start website
pm2 start website/server.js --name zypher-website

# Save and startup
pm2 save
pm2 startup
```

---

## Step 8: Setup Nginx (Reverse Proxy)

```bash
nano /etc/nginx/sites-available/zypher
```

Add:
```nginx
server {
    listen 80;
    server_name yourdomain.com www.yourdomain.com;

    location / {
        proxy_pass http://localhost:4000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}

server {
    listen 80;
    server_name api.yourdomain.com;

    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}
```

Enable:
```bash
ln -s /etc/nginx/sites-available/zypher /etc/nginx/sites-enabled/
nginx -t
systemctl restart nginx
```

---

## Step 9: Setup SSL (HTTPS)

```bash
# Install Certbot
apt install -y certbot python3-certbot-nginx

# Get SSL certificate
certbot --nginx -d yourdomain.com -d www.yourdomain.com -d api.yourdomain.com
```

Follow prompts. Done!

---

## Step 10: Setup Firewall

```bash
ufw allow 22    # SSH
ufw allow 80    # HTTP
ufw allow 443   # HTTPS
ufw enable
```

---

## Step 11: Buy Domain

1. Go to Namecheap: https://namecheap.com
2. Search for your domain (~$10/year)
3. Buy it
4. Update DNS:
   - Type: A
   - Name: @
   - Value: YOUR_DROPLET_IP
   - Type: A
   - Name: api
   - Value: YOUR_DROPLET_IP

---

## Step 12: Update Loader

```cpp
SelfAuth::api auth("api.yourdomain.com", 443);
```

---

## Step 13: Setup Stripe Webhook

Stripe Dashboard → Webhooks:
- URL: `https://api.yourdomain.com/webhook`
- Event: `checkout.session.completed`

---

## Managing Your Server

```bash
# View logs
pm2 logs zypher-auth
pm2 logs zypher-website

# Restart services
pm2 restart all

# Stop services
pm2 stop all

# Monitor
pm2 monit

# Update code
cd /var/www/zypher
git pull
pm2 restart all
```

---

## Costs

- **Droplet:** $5/month
- **Domain:** $10/year
- **Total:** ~$6/month

---

## Advantages

✅ **Full control** - Root access
✅ **Reliable** - 99.99% uptime
✅ **Scalable** - Upgrade anytime
✅ **Professional** - Custom domain
✅ **Fast** - Choose region

---

## Disadvantages

❌ **Costs money** - $5/month minimum
❌ **More setup** - Technical knowledge needed
❌ **Manual updates** - SSH in to update
 **You manage everything** - Security, backups, etc.

---

## Backups

```bash
# Enable automatic backups in DigitalOcean dashboard
# Costs extra $1/month but worth it!

# Or manual backup
tar -czf backup-$(date +%Y%m%d).tar.gz /var/www/zypher
```

---

## You're Live! 

Website: `https://yourdomain.com`
API: `https://api.yourdomain.com`

Professional, reliable, and scalable!
