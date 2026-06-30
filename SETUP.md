# Zypher - Complete Commercial Setup

## What You Got

### 1. Auth Server (`auth-server/`)
- License key validation
- HWID locking
- Admin dashboard
- Rate limiting
- Encrypted responses

**Run:** `cd auth-server && npm start`
**Dashboard:** http://localhost:3000
**Login:** admin / admin123

---

### 2. Commercial Website (`website/`)
- Beautiful landing page
- Stripe payment integration
- Auto-email with receipt + license key
- Admin panel for managing products
- Order tracking

**Run:** `cd website && npm start`
**Website:** http://localhost:4000
**Admin:** http://localhost:4000/admin

---

### 3. Client Loader (`zypher/`)
- Dark purple theme
- Real auth server communication
- HWID detection
- Session management

**Build:** Open `zypher.sln` in Visual Studio
**Run:** `zypher\x64\Release\zypher.exe`

---

## How It Works

### Customer Purchase Flow:
1. Customer visits your website
2. Browses products
3. Clicks "Buy Now"
4. Enters email
5. Redirected to Stripe checkout
6. Pays securely
7. **Automatically receives email with:**
   - Order receipt
   - License key (format: XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX)
   - Download instructions

### After Purchase:
1. Customer downloads loader
2. Runs it
3. Enters license key
4. Auth server validates key + HWID
5. Customer gets access

---

## Setup Checklist

### Immediate (Local Testing):
- [x] Auth server running on port 3000
- [x] Website running on port 4000
- [ ] Add Stripe test keys to `website/.env`
- [ ] Add Gmail credentials to `website/.env`
- [ ] Test purchase flow with Stripe test card: `4242 4242 4242 4242`

### Production:
- [ ] Get domain name
- [ ] Setup VPS (DigitalOcean/AWS)
- [ ] Install Node.js on server
- [ ] Upload all files
- [ ] Get Stripe live keys
- [ ] Setup SSL certificate
- [ ] Configure webhook
- [ ] Update URLs in code
- [ ] Change admin passwords
- [ ] Setup email service

---

## File Locations

```
C:\Users\silen\Downloads\zypher\
├── auth-server/          # Authentication server
│   ├── server.js
│   ├── package.json
│   └── public/           # Admin dashboard
│
├── website/              # Commercial website
│   ├── server.js
│   ├── package.json
│   ├── .env.example
│   └── public/           # Website files
│       ├── index.html
│       ├── admin.html
│       ├── css/
│       └── js/
│
└── zypher/               # Client loader
    ├── zypher.sln
    ── src/
```

---

## Environment Variables

### Auth Server (auth-server/.env):
```
PORT=3000
JWT_SECRET=your-secret-key
ENCRYPTION_KEY=your-encryption-key
```

### Website (website/.env):
```
STRIPE_SECRET_KEY=sk_test_...
STRIPE_WEBHOOK_SECRET=whsec_...
EMAIL_USER=your@gmail.com
EMAIL_PASS=your-app-password
WEBSITE_URL=http://localhost:4000
PORT=4000
```

---

## Testing

### Test Purchase:
1. Go to http://localhost:4000
2. Add a product via admin panel
3. Click "Buy Now"
4. Use Stripe test card: `4242 4242 4242 4242`
5. Check email for license key

### Test Auth:
1. Go to http://localhost:3000
2. Login with admin/admin123
3. Generate a license key
4. Run zypher.exe
5. Enter the license key

---

## Security Features

- **Server-side validation** - All auth checks on server
- **HWID locking** - License binds to machine (3 changes max)
- **Rate limiting** - 10 requests per minute
- **Encrypted responses** - AES encryption on API
- **Session tokens** - 24-hour sessions
- **Stripe secure checkout** - PCI compliant payments
- **Auto license generation** - Unique keys per purchase
- **Email verification** - Receipt sent automatically

---

## Next Steps

1. **Get Stripe Account:** https://stripe.com
2. **Get Domain:** Namecheap, GoDaddy, etc.
3. **Get VPS:** DigitalOcean ($5/mo), AWS, Linode
4. **Setup Email:** Gmail with app password or SendGrid
5. **Customize:** Change colors, add your logo, modify text
6. **Add Products:** Use admin panel to add your products
7. **Test Everything:** Make sure purchase flow works
8. **Deploy:** Move to production server
9. **Market:** Start selling!

---

## Support

If you need help:
- Check the README files in each folder
- Review the code comments
- Test with Stripe test mode first
- Make sure all services are running

**Services to keep running:**
- Auth server (port 3000)
- Website (port 4000)

Both can run simultaneously on the same machine.
