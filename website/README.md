# Zypher Commercial Website - Setup Guide

## Quick Start

### 1. Install Dependencies

```bash
cd C:\Users\silen\Downloads\zypher\website
npm install
```

### 2. Setup Stripe

1. Go to https://stripe.com and create an account
2. Get your API keys from the Stripe Dashboard
3. Copy `.env.example` to `.env`:
   ```bash
   copy .env.example .env
   ```
4. Edit `.env` and add your Stripe keys:
   ```
   STRIPE_SECRET_KEY=sk_test_your_actual_key
   STRIPE_WEBHOOK_SECRET=whsec_your_actual_secret
   ```

### 3. Setup Email (Gmail)

1. Go to your Google Account settings
2. Enable 2-Factor Authentication
3. Generate an App Password:
   - Go to https://myaccount.google.com/apppasswords
   - Create a new app password for "Mail"
4. Add to `.env`:
   ```
   EMAIL_USER=your_email@gmail.com
   EMAIL_PASS=your_app_password
   ```

### 4. Start the Website

```bash
npm start
```

Website will be available at: http://localhost:4000
Admin panel: http://localhost:4000/admin

### 5. Setup Stripe Webhook

For auto-email delivery, you need to setup a webhook:

1. Go to Stripe Dashboard → Developers → Webhooks
2. Add endpoint: `https://your-domain.com/webhook`
3. Select event: `checkout.session.completed`
4. Copy the webhook secret and add to `.env`:
   ```
   STRIPE_WEBHOOK_SECRET=whsec_...
   ```

**For local testing**, use Stripe CLI:
```bash
stripe listen --forward-to localhost:4000/webhook
```

## Features

### Customer Flow
1. Customer visits website
2. Browses products
3. Clicks "Buy Now"
4. Enters email address
5. Redirected to Stripe checkout
6. Completes payment
7. **Automatically receives email with:**
   - Order receipt
   - License key
   - Download instructions

### Admin Panel
- Add/remove products
- View all orders
- View all license keys
- Track sales

### Security
- Server-side license key generation
- Encrypted API responses
- HWID locking (via auth server)
- Rate limiting
- Stripe secure checkout

## File Structure

```
website/
├── server.js           # Main server file
├── package.json        # Dependencies
├── .env.example        # Environment variables template
├── db.json            # Database (auto-created)
└── public/
    ├── index.html      # Main website
    ├── admin.html      # Admin panel
    ├── success.html    # Payment success page
    ├── cancel.html     # Payment cancelled page
    ├── css/
    │   └── style.css   # Styles
    └── js/
        └── main.js     # Frontend logic
```

## Production Deployment

### 1. Get a Domain
- Buy from Namecheap, GoDaddy, etc.
- Point to your server IP

### 2. Setup VPS
- DigitalOcean, AWS, Linode, etc.
- Install Node.js
- Clone your project

### 3. SSL Certificate
```bash
npm install -g certbot
certbot --nginx -d your-domain.com
```

### 4. Environment Variables
Set these in production:
```
STRIPE_SECRET_KEY=sk_live_...
STRIPE_WEBHOOK_SECRET=whsec_...
EMAIL_USER=your@email.com
EMAIL_PASS=your_password
WEBSITE_URL=https://your-domain.com
PORT=443
```

### 5. Start with PM2
```bash
npm install -g pm2
pm2 start server.js --name zypher-website
pm2 save
pm2 startup
```

## Adding Products

### Via Admin Panel
1. Go to http://localhost:4000/admin
2. Fill in product details
3. Click "Add Product"

### Via API
```bash
curl -X POST http://localhost:4000/api/admin/products \
  -H "Content-Type: application/json" \
  -d '{"name":"Zypher Pro","description":"Premium version","price":49.99}'
```

## Email Customization

Edit the `sendReceiptEmail` function in `server.js` to customize:
- Email template
- Branding
- Additional information
- Download links

## Troubleshooting

### Email not sending?
- Check Gmail app password is correct
- Enable "Less secure apps" if needed
- Check spam folder

### Stripe webhook not working?
- Verify webhook URL is correct
- Check webhook secret matches
- Use Stripe CLI for local testing

### Products not showing?
- Check if products exist in `db.json`
- Verify API endpoint is working
- Check browser console for errors

## Support

For issues or questions:
- Email: support@zypher.com
- Website: https://zypher.com

---

**Note:** This is a complete e-commerce solution. Make sure to:
1. Change all default credentials
2. Use HTTPS in production
3. Keep your `.env` file secure
4. Regularly backup `db.json`
5. Monitor Stripe dashboard for transactions
