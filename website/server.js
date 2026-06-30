require('dotenv').config();
const express = require('express');
const stripe = process.env.STRIPE_SECRET_KEY ? require('stripe')(process.env.STRIPE_SECRET_KEY) : null;
const nodemailer = process.env.EMAIL_USER ? require('nodemailer') : null;
const crypto = require('crypto');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');
const path = require('path');
const { MongoClient } = require('mongodb');

const app = express();
const PORT = process.env.PORT || 4000;
const JWT_SECRET = process.env.JWT_SECRET || 'zypher-secret-change-me-in-production';

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

const PUBLIC_DIR = path.join(__dirname, 'public');

app.get('/css/*', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, req.path));
});

app.get('/js/*', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, req.path));
});

// MongoDB connection
const MONGODB_URI = process.env.MONGODB_URI;
let db = null;
let productsCol = null;
let ordersCol = null;
let keysCol = null;
let usersCol = null;

async function connectDB() {
    if (!MONGODB_URI) {
        console.warn('No MONGODB_URI set - using in-memory storage (data will be lost on restart)');
        return;
    }
    try {
        const client = await MongoClient.connect(MONGODB_URI);
        db = client.db('zypher');
        productsCol = db.collection('products');
        ordersCol = db.collection('orders');
        keysCol = db.collection('license_keys');
        usersCol = db.collection('users');
        console.log('Connected to MongoDB');
        await ensureAdminUser();

        // Pre-populate if no products exist
        const count = await productsCol.countDocuments();
        if (count === 0) {
            await productsCol.insertOne({
                id: 'zypher-loader-001',
                name: 'Zypher Loader',
                description: 'Multi-game cheat loader with HWID locking, auto-updates, and premium support',
                tiers: [
                    { id: 'tier-3day', name: '3 Day Key', price: 15, duration_days: 3 },
                    { id: 'tier-1week', name: '1 Week Key', price: 25, duration_days: 7 },
                    { id: 'tier-1month', name: '1 Month Key', price: 45, duration_days: 30 },
                    { id: 'tier-lifetime', name: 'Lifetime Key', price: 100, duration_days: 0 }
                ],
                created_at: new Date().toISOString()
            });
            console.log('Pre-populated Zypher Loader product');
        }
    } catch (err) {
        console.error('MongoDB connection failed:', err.message);
    }
}

// In-memory fallback if no MongoDB
let memoryDB = {
    products: [{
        id: 'zypher-loader-001',
        name: 'Zypher Loader',
        description: 'Multi-game cheat loader with HWID locking, auto-updates, and premium support',
        tiers: [
            { id: 'tier-3day', name: '3 Day Key', price: 15, duration_days: 3 },
            { id: 'tier-1week', name: '1 Week Key', price: 25, duration_days: 7 },
            { id: 'tier-1month', name: '1 Month Key', price: 45, duration_days: 30 },
            { id: 'tier-lifetime', name: 'Lifetime Key', price: 100, duration_days: 0 }
        ],
        created_at: new Date().toISOString()
    }],
    orders: [],
    license_keys: [],
    users: []
};

function getProducts() { return productsCol ? productsCol.find({}).toArray() : Promise.resolve(memoryDB.products); }
function getOrders() { return ordersCol ? ordersCol.find({}).sort({ created_at: -1 }).toArray() : Promise.resolve(memoryDB.orders); }
function getKeys() { return keysCol ? keysCol.find({}).sort({ created_at: -1 }).toArray() : Promise.resolve(memoryDB.license_keys); }

// Admin auth
let adminsCol = null;
async function ensureAdminUser() {
    if (!db) return;
    adminsCol = db.collection('admins');
    const existing = await adminsCol.findOne({ username: 'admin' });
    if (!existing) {
        const hash = bcrypt.hashSync('admin123', 10);
        await adminsCol.insertOne({ username: 'admin', password_hash: hash, created_at: new Date().toISOString() });
        console.log('Default admin user created (admin / admin123)');
    }
}

function adminAuth(req, res, next) {
    const token = req.headers.authorization?.split(' ')[1] || req.cookies?.admin_token;
    if (!token) return res.status(401).json({ error: 'Not authenticated' });
    try {
        const decoded = jwt.verify(token, JWT_SECRET);
        req.admin = decoded;
        next();
    } catch (e) {
        res.status(401).json({ error: 'Invalid token' });
    }
}

function userAuth(req, res, next) {
    const token = req.headers.authorization?.split(' ')[1];
    if (!token) return res.status(401).json({ error: 'Not authenticated' });
    try {
        const decoded = jwt.verify(token, JWT_SECRET);
        if (decoded.type !== 'user') return res.status(403).json({ error: 'Forbidden' });
        req.user = decoded;
        next();
    } catch (e) {
        res.status(401).json({ error: 'Invalid token' });
    }
}

function getUsers() { return usersCol ? usersCol.find({}).toArray() : Promise.resolve(memoryDB.users); }

let transporter = null;
if (nodemailer && process.env.EMAIL_USER && process.env.EMAIL_PASS) {
    transporter = nodemailer.createTransport({
        service: 'gmail',
        auth: { user: process.env.EMAIL_USER, pass: process.env.EMAIL_PASS }
    });
}

function generateLicenseKey() {
    const segments = [];
    for (let i = 0; i < 4; i++) {
        segments.push(crypto.randomBytes(4).toString('hex').toUpperCase());
    }
    return segments.join('-');
}

async function sendReceiptEmail(email, orderId, productName, licenseKey, amount) {
    if (!transporter) return;
    try {
        await transporter.sendMail({
            from: process.env.EMAIL_USER,
            to: email,
            subject: `Zypher - Order Confirmation #${orderId}`,
            html: `<div style="font-family:Arial,sans-serif;max-width:600px;margin:0 auto;background:#1a1028;color:#e0d0f0;padding:40px;border-radius:12px;">
                <h1 style="color:#a78bfa;text-align:center;">Thank You!</h1>
                <p><strong>Order:</strong> ${orderId}</p>
                <p><strong>Product:</strong> ${productName}</p>
                <p><strong>Amount:</strong> $${(amount/100).toFixed(2)}</p>
                <div style="background:rgba(34,197,94,0.1);padding:20px;border-radius:8px;margin:20px 0;border:1px solid #22c55e;">
                    <h2 style="color:#22c55e;margin-top:0;">Your License Key</h2>
                    <p style="font-family:monospace;font-size:18px;background:#0f0a1a;padding:15px;border-radius:6px;text-align:center;">${licenseKey}</p>
                </div>
            </div>`
        });
    } catch (e) { console.error('Email failed:', e); }
}

app.get('/', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, 'index.html'));
});

app.get('/admin', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, 'admin.html'));
});

app.get('/account', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, 'account.html'));
});

app.get('/success', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, 'success.html'));
});

app.get('/cancel', (req, res) => {
    res.sendFile(path.join(PUBLIC_DIR, 'cancel.html'));
});

app.get('/api/products', async (req, res) => {
    try {
        const products = await getProducts();
        res.json(products);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/admin/products', adminAuth, async (req, res) => {
    try {
        const { name, description, tiers } = req.body;
        if (!name || !description || !tiers || !Array.isArray(tiers) || tiers.length === 0) {
            return res.status(400).json({ error: 'Name, description, and at least one tier required' });
        }
        const product = {
            id: crypto.randomBytes(8).toString('hex'),
            name,
            description,
            tiers: tiers.map(t => ({
                id: crypto.randomBytes(4).toString('hex'),
                name: t.name,
                price: parseFloat(t.price),
                duration_days: parseInt(t.duration_days) || null
            })),
            created_at: new Date().toISOString()
        };
        if (productsCol) {
            await productsCol.insertOne(product);
        } else {
            memoryDB.products.push(product);
        }
        res.json(product);
    } catch (error) {
        console.error('Error creating product:', error);
        res.status(500).json({ error: 'Internal server error: ' + error.message });
    }
});

app.post('/api/checkout', async (req, res) => {
    try {
        const { email, productId, tierId } = req.body;
        const products = await getProducts();
        const product = products.find(p => p.id === productId);
        if (!product) return res.status(404).json({ error: 'Product not found' });
        const tier = product.tiers.find(t => t.id === tierId);
        if (!tier) return res.status(404).json({ error: 'Tier not found' });

        if (!stripe) return res.status(500).json({ error: 'Stripe not configured' });

        const session = await stripe.checkout.sessions.create({
            payment_method_types: ['card'],
            line_items: [{
                price_data: {
                    currency: 'usd',
                    product_data: { name: `${product.name} - ${tier.name}`, description: product.description },
                    unit_amount: tier.price * 100,
                },
                quantity: 1,
            }],
            mode: 'payment',
            success_url: `${process.env.WEBSITE_URL || ''}/success?session_id={CHECKOUT_SESSION_ID}`,
            cancel_url: `${process.env.WEBSITE_URL || ''}/cancel`,
            customer_email: email,
            metadata: { productId, tierId, tierName: tier.name, durationDays: tier.duration_days || 0, email }
        });
        res.json({ url: session.url });
    } catch (error) {
        console.error('Checkout error:', error);
        res.status(500).json({ error: 'Checkout failed: ' + error.message });
    }
});

app.post('/webhook', express.raw({ type: 'application/json' }), async (req, res) => {
    if (!stripe) return res.status(500).send('Stripe not configured');
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
        const tierId = session.metadata.tierId;
        const tierName = session.metadata.tierName;
        const durationDays = parseInt(session.metadata.durationDays) || 0;
        const email = session.metadata.email;
        const products = await getProducts();
        const product = products.find(p => p.id === productId);
        if (product) {
            const licenseKey = generateLicenseKey();
            let expiresAt = null;
            if (durationDays > 0) {
                expiresAt = new Date(Date.now() + durationDays * 24 * 60 * 60 * 1000).toISOString();
            }
            const keyDoc = {
                key: licenseKey, product_id: productId, tier_id: tierId, tier_name: tierName,
                email, order_id: session.id, created_at: new Date().toISOString(),
                expires_at: expiresAt, used: false
            };
            const orderDoc = {
                id: session.id, email, product_id: productId, product_name: product.name,
                tier_name: tierName, amount: session.amount_total, license_key: licenseKey,
                created_at: new Date().toISOString()
            };
            if (keysCol) {
                await keysCol.insertOne(keyDoc);
                await ordersCol.insertOne(orderDoc);
            } else {
                memoryDB.license_keys.push(keyDoc);
                memoryDB.orders.push(orderDoc);
            }
            await sendReceiptEmail(email, session.id, product.name, licenseKey, session.amount_total);
        }
    }
    res.json({ received: true });
});

app.get('/api/admin/keys', adminAuth, async (req, res) => {
    try { res.json(await getKeys()); } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/admin/login', async (req, res) => {
    try {
        const { username, password } = req.body;
        if (!username || !password) return res.status(400).json({ error: 'Missing credentials' });

        if (adminsCol) {
            const admin = await adminsCol.findOne({ username });
            if (!admin || !bcrypt.compareSync(password, admin.password_hash)) {
                return res.status(401).json({ error: 'Invalid credentials' });
            }
        } else {
            if (username !== 'admin' || password !== 'admin123') {
                return res.status(401).json({ error: 'Invalid credentials' });
            }
        }

        const token = jwt.sign({ username }, JWT_SECRET, { expiresIn: '24h' });
        res.json({ token });
    } catch (e) {
        console.error('Login error:', e);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.post('/api/admin/change-password', adminAuth, async (req, res) => {
    try {
        const { current_password, new_password } = req.body;
        if (!current_password || !new_password) return res.status(400).json({ error: 'Missing fields' });
        if (new_password.length < 6) return res.status(400).json({ error: 'Password must be at least 6 characters' });

        if (adminsCol) {
            const admin = await adminsCol.findOne({ username: req.admin.username });
            if (!admin || !bcrypt.compareSync(current_password, admin.password_hash)) {
                return res.status(401).json({ error: 'Current password is incorrect' });
            }
            const hash = bcrypt.hashSync(new_password, 10);
            await adminsCol.updateOne({ username: req.admin.username }, { $set: { password_hash: hash } });
        } else {
            if (current_password !== 'admin123') return res.status(401).json({ error: 'Current password is incorrect' });
        }

        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/admin/orders', adminAuth, async (req, res) => {
    try {
        const orders = await getOrders();
        res.json(orders);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/auth/register', async (req, res) => {
    try {
        const { username, email, password } = req.body;
        if (!username || !email || !password) return res.status(400).json({ error: 'All fields are required' });
        if (password.length < 6) return res.status(400).json({ error: 'Password must be at least 6 characters' });

        const existingUser = usersCol
            ? await usersCol.findOne({ $or: [{ email }, { username }] })
            : memoryDB.users.find(u => u.email === email || u.username === username);

        if (existingUser) return res.status(409).json({ error: 'Username or email already exists' });

        const hash = bcrypt.hashSync(password, 10);
        const user = {
            id: crypto.randomBytes(8).toString('hex'),
            username,
            email,
            password_hash: hash,
            created_at: new Date().toISOString()
        };

        if (usersCol) {
            await usersCol.insertOne(user);
        } else {
            memoryDB.users.push(user);
        }

        const token = jwt.sign({ id: user.id, username: user.username, email: user.email, type: 'user' }, JWT_SECRET, { expiresIn: '7d' });
        res.json({ token, user: { id: user.id, username: user.username, email: user.email } });
    } catch (e) {
        console.error('Register error:', e);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.post('/api/auth/login', async (req, res) => {
    try {
        const { email, password } = req.body;
        if (!email || !password) return res.status(400).json({ error: 'Email and password are required' });

        const user = usersCol
            ? await usersCol.findOne({ email })
            : memoryDB.users.find(u => u.email === email);

        if (!user || !bcrypt.compareSync(password, user.password_hash)) {
            return res.status(401).json({ error: 'Invalid email or password' });
        }

        const token = jwt.sign({ id: user.id, username: user.username, email: user.email, type: 'user' }, JWT_SECRET, { expiresIn: '7d' });
        res.json({ token, user: { id: user.id, username: user.username, email: user.email } });
    } catch (e) {
        console.error('Login error:', e);
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/auth/me', userAuth, async (req, res) => {
    try {
        const user = usersCol
            ? await usersCol.findOne({ id: req.user.id })
            : memoryDB.users.find(u => u.id === req.user.id);

        if (!user) return res.status(404).json({ error: 'User not found' });
        res.json({ id: user.id, username: user.username, email: user.email, created_at: user.created_at });
    } catch (e) {
        res.status(500).json({ error: 'Internal server error' });
    }
});

app.get('/api/user/orders', userAuth, async (req, res) => {
    try {
        const allOrders = await getOrders();
        const userOrders = allOrders.filter(o => o.email === req.user.email);
        res.json(userOrders);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.get('/api/user/keys', userAuth, async (req, res) => {
    try {
        const allKeys = await getKeys();
        const userKeys = allKeys.filter(k => k.email === req.user.email);
        res.json(userKeys);
    } catch (e) { res.status(500).json({ error: e.message }); }
});

app.post('/api/auth/change-password', userAuth, async (req, res) => {
    try {
        const { current_password, new_password } = req.body;
        if (!current_password || !new_password) return res.status(400).json({ error: 'Missing fields' });
        if (new_password.length < 6) return res.status(400).json({ error: 'Password must be at least 6 characters' });

        if (usersCol) {
            const user = await usersCol.findOne({ id: req.user.id });
            if (!user || !bcrypt.compareSync(current_password, user.password_hash)) {
                return res.status(401).json({ error: 'Current password is incorrect' });
            }
            const hash = bcrypt.hashSync(new_password, 10);
            await usersCol.updateOne({ id: req.user.id }, { $set: { password_hash: hash } });
        } else {
            const user = memoryDB.users.find(u => u.id === req.user.id);
            if (!user || !bcrypt.compareSync(current_password, user.password_hash)) {
                return res.status(401).json({ error: 'Current password is incorrect' });
            }
            user.password_hash = bcrypt.hashSync(new_password, 10);
        }

        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ error: 'Internal server error' });
    }
});

connectDB().then(() => {
    app.listen(PORT, () => {
        console.log(`Zypher website running on port ${PORT}`);
    });
});

module.exports = app;
