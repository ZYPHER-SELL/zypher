require('dotenv').config();
const express = require('express');
const stripe = require('stripe')(process.env.STRIPE_SECRET_KEY);
const nodemailer = require('nodemailer');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 4000;

app.use(express.static('public'));
app.use(express.json());

// Database
const DB_PATH = path.join(__dirname, 'db.json');
let db = {
    products: [],
    orders: [],
    license_keys: []
};

function loadDB() {
    if (fs.existsSync(DB_PATH)) {
        db = JSON.parse(fs.readFileSync(DB_PATH, 'utf8'));
    } else {
        saveDB();
    }
}

function saveDB() {
    fs.writeFileSync(DB_PATH, JSON.stringify(db, null, 2));
}

loadDB();

// Email transporter
const transporter = nodemailer.createTransport({
    service: 'gmail',
    auth: {
        user: process.env.EMAIL_USER,
        pass: process.env.EMAIL_PASS
    }
});

function generateLicenseKey() {
    const segments = [];
    for (let i = 0; i < 4; i++) {
        segments.push(crypto.randomBytes(4).toString('hex').toUpperCase());
    }
    return segments.join('-');
}

async function sendReceiptEmail(email, orderId, productName, licenseKey, amount) {
    const mailOptions = {
        from: process.env.EMAIL_USER,
        to: email,
        subject: `Zypher - Order Confirmation #${orderId}`,
        html: `
            <div style="font-family: Arial, sans-serif; max-width: 600px; margin: 0 auto; background: #1a1028; color: #e0d0f0; padding: 40px; border-radius: 12px;">
                <h1 style="color: #a78bfa; text-align: center; margin-bottom: 30px;">Thank You for Your Purchase!</h1>
                
                <div style="background: rgba(124, 58, 237, 0.1); padding: 20px; border-radius: 8px; margin-bottom: 20px;">
                    <h2 style="color: #a78bfa; margin-top: 0;">Order Details</h2>
                    <p><strong>Order ID:</strong> ${orderId}</p>
                    <p><strong>Product:</strong> ${productName}</p>
                    <p><strong>Amount Paid:</strong> $${(amount / 100).toFixed(2)}</p>
                </div>

                <div style="background: rgba(34, 197, 94, 0.1); padding: 20px; border-radius: 8px; margin-bottom: 20px; border: 1px solid #22c55e;">
                    <h2 style="color: #22c55e; margin-top: 0;">Your License Key</h2>
                    <p style="font-family: monospace; font-size: 18px; background: #0f0a1a; padding: 15px; border-radius: 6px; text-align: center; letter-spacing: 2px;">${licenseKey}</p>
                    <p style="font-size: 14px; color: #c0b0e0;">Save this key! You'll need it to activate your product.</p>
                </div>

                <div style="background: rgba(124, 58, 237, 0.1); padding: 20px; border-radius: 8px; margin-bottom: 20px;">
                    <h3 style="color: #a78bfa;">How to Use:</h3>
                    <ol style="color: #e0d0f0; padding-left: 20px;">
                        <li>Download the Zypher loader from your dashboard</li>
                        <li>Run the application</li>
                        <li>Enter your license key when prompted</li>
                        <li>Enjoy your product!</li>
                    </ol>
                </div>

                <div style="text-align: center; margin-top: 30px; padding-top: 20px; border-top: 1px solid #3a2a5a;">
                    <p style="color: #c0b0e0; font-size: 14px;">Need help? Contact us at support@zypher.com</p>
                    <p style="color: #a78bfa; font-weight: bold;">Thank you for choosing Zypher!</p>
                </div>
            </div>
        `
    };

    await transporter.sendMail(mailOptions);
}

// Checkout endpoint
app.post('/api/checkout', async (req, res) => {
    try {
        const { email, productId } = req.body;

        const product = db.products.find(p => p.id === productId);
        if (!product) {
            return res.status(404).json({ error: 'Product not found' });
        }

        const session = await stripe.checkout.sessions.create({
            payment_method_types: ['card'],
            line_items: [{
                price_data: {
                    currency: 'usd',
                    product_data: {
                        name: product.name,
                        description: product.description
                    },
                    unit_amount: product.price * 100,
                },
                quantity: 1,
            }],
            mode: 'payment',
            success_url: `${process.env.WEBSITE_URL}/success?session_id={CHECKOUT_SESSION_ID}`,
            cancel_url: `${process.env.WEBSITE_URL}/cancel`,
            customer_email: email,
            metadata: {
                productId: productId,
                email: email
            }
        });

        res.json({ url: session.url });
    } catch (error) {
        console.error(error);
        res.status(500).json({ error: 'Checkout failed' });
    }
});

// Webhook endpoint
app.post('/webhook', express.raw({ type: 'application/json' }), async (req, res) => {
    const sig = req.headers['stripe-signature'];

    let event;

    try {
        event = stripe.webhooks.constructEvent(req.body, sig, process.env.STRIPE_WEBHOOK_SECRET);
    } catch (err) {
        return res.status(400).send(`Webhook Error: ${err.message}`);
    }

    if (event.type === 'checkout.session.completed') {
        const session = event.data.object;

        const productId = session.metadata.productId;
        const email = session.metadata.email;
        const product = db.products.find(p => p.id === productId);

        if (product) {
            const licenseKey = generateLicenseKey();

            db.license_keys.push({
                key: licenseKey,
                product_id: productId,
                email: email,
                order_id: session.id,
                created_at: new Date().toISOString(),
                used: false
            });

            db.orders.push({
                id: session.id,
                email: email,
                product_id: productId,
                product_name: product.name,
                amount: session.amount_total,
                license_key: licenseKey,
                created_at: new Date().toISOString()
            });

            saveDB();

            try {
                await sendReceiptEmail(email, session.id, product.name, licenseKey, session.amount_total);
            } catch (emailError) {
                console.error('Failed to send email:', emailError);
            }
        }
    }

    res.json({ received: true });
});

// Get products
app.get('/api/products', (req, res) => {
    res.json(db.products);
});

// Admin: Add product
app.post('/api/admin/products', (req, res) => {
    const { name, description, price } = req.body;

    const product = {
        id: crypto.randomBytes(8).toString('hex'),
        name,
        description,
        price: parseFloat(price),
        created_at: new Date().toISOString()
    };

    db.products.push(product);
    saveDB();

    res.json(product);
});

// Admin: Get orders
app.get('/api/admin/orders', (req, res) => {
    res.json(db.orders);
});

// Admin: Get license keys
app.get('/api/admin/keys', (req, res) => {
    res.json(db.license_keys);
});

app.get('/admin', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'admin.html'));
});

app.listen(PORT, () => {
    console.log(`Zypher website running on port ${PORT}`);
});
