const express = require('express');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const CryptoJS = require('crypto-js');
const { RateLimiterMemory } = require('rate-limiter-flexible');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');
const helmet = require('helmet');
const cors = require('cors');

const app = express();
const PORT = process.env.PORT || 3000;
const JWT_SECRET = process.env.JWT_SECRET || crypto.randomBytes(64).toString('hex');
const ENCRYPTION_KEY = process.env.ENCRYPTION_KEY || crypto.randomBytes(32).toString('hex');

app.use(helmet());
app.use(cors());
app.use(express.json());

const DB_PATH = path.join(__dirname, 'zypher_db.json');

let db = {
    licenses: [],
    admins: [],
    auth_logs: [],
    sessions: []
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

const rateLimiter = new RateLimiterMemory({
    points: 10,
    duration: 60,
});

function generateLicenseKey() {
    const segments = [];
    for (let i = 0; i < 4; i++) {
        segments.push(crypto.randomBytes(4).toString('hex').toUpperCase());
    }
    return segments.join('-');
}

function getHWID(hwid) {
    return crypto.createHash('sha256').update(hwid).digest('hex');
}

function encryptResponse(data) {
    const encrypted = CryptoJS.AES.encrypt(JSON.stringify(data), ENCRYPTION_KEY).toString();
    return { encrypted };
}

function logAuth(licenseKey, hwid, ip, success) {
    db.auth_logs.unshift({
        license_key: licenseKey,
        hwid: hwid,
        ip_address: ip,
        success: success,
        timestamp: new Date().toISOString()
    });

    if (db.auth_logs.length > 1000) {
        db.auth_logs = db.auth_logs.slice(0, 1000);
    }

    saveDB();
}

app.post('/api/auth/validate', async (req, res) => {
    try {
        await rateLimiter.consume(req.ip);
    } catch (e) {
        return res.status(429).json(encryptResponse({ error: 'Too many requests' }));
    }

    const { key, hwid } = req.body;

    if (!key || !hwid) {
        return res.status(400).json(encryptResponse({ error: 'Missing key or hwid' }));
    }

    const license = db.licenses.find(l => l.key === key && l.is_active === 1);

    if (!license) {
        logAuth(key, hwid, req.ip, false);
        return res.status(401).json(encryptResponse({ error: 'Invalid license key' }));
    }

    if (license.expires_at && new Date(license.expires_at) < new Date()) {
        logAuth(key, hwid, req.ip, false);
        return res.status(401).json(encryptResponse({ error: 'License expired' }));
    }

    const hashedHwid = getHWID(hwid);

    if (license.hwid && license.hwid !== hashedHwid) {
        logAuth(key, hwid, req.ip, false);
        return res.status(401).json(encryptResponse({
            error: 'HWID mismatch',
            hwid_locked: true
        }));
    }

    if (!license.hwid) {
        if (license.hwid_change_count >= license.max_hwid_changes) {
            logAuth(key, hwid, req.ip, false);
            return res.status(401).json(encryptResponse({
                error: 'Maximum HWID changes reached',
                hwid_locked: true
            }));
        }

        license.hwid = hashedHwid;
        license.hwid_change_count = (license.hwid_change_count || 0) + 1;
        saveDB();
    }

    const sessionToken = crypto.randomBytes(32).toString('hex');
    const sessionExpiry = new Date(Date.now() + 24 * 60 * 60 * 1000).toISOString();

    db.sessions = db.sessions.filter(s => s.license_key !== key);
    db.sessions.push({
        license_key: key,
        session_token: sessionToken,
        hwid: hashedHwid,
        created_at: new Date().toISOString(),
        expires_at: sessionExpiry
    });

    saveDB();
    logAuth(key, hwid, req.ip, true);

    res.json(encryptResponse({
        success: true,
        session_token: sessionToken,
        expires_at: license.expires_at || null,
        message: 'Authentication successful'
    }));
});

app.post('/api/auth/heartbeat', (req, res) => {
    const { session_token, hwid } = req.body;

    if (!session_token || !hwid) {
        return res.status(400).json(encryptResponse({ error: 'Missing data' }));
    }

    const session = db.sessions.find(s =>
        s.session_token === session_token && s.hwid === getHWID(hwid)
    );

    if (!session || new Date(session.expires_at) < new Date()) {
        return res.status(401).json(encryptResponse({ error: 'Invalid session' }));
    }

    session.expires_at = new Date(Date.now() + 24 * 60 * 60 * 1000).toISOString();
    saveDB();

    res.json(encryptResponse({ success: true }));
});

app.post('/api/admin/login', (req, res) => {
    const { username, password } = req.body;

    if (!username || !password) {
        return res.status(400).json({ error: 'Missing credentials' });
    }

    const admin = db.admins.find(a => a.username === username);

    if (!admin || !bcrypt.compareSync(password, admin.password_hash)) {
        return res.status(401).json({ error: 'Invalid credentials' });
    }

    const token = jwt.sign({ username: admin.username }, JWT_SECRET, { expiresIn: '24h' });

    res.json({ token });
});

function adminAuth(req, res, next) {
    const token = req.headers.authorization?.split(' ')[1];

    if (!token) {
        return res.status(401).json({ error: 'No token' });
    }

    try {
        const decoded = jwt.verify(token, JWT_SECRET);
        req.admin = decoded;
        next();
    } catch (e) {
        res.status(401).json({ error: 'Invalid token' });
    }
}

app.post('/api/admin/licenses', adminAuth, (req, res) => {
    const { expires_at, max_hwid_changes } = req.body;

    const key = generateLicenseKey();
    const expiresAt = expires_at ? new Date(expires_at).toISOString() : null;
    const maxChanges = max_hwid_changes || 3;

    db.licenses.push({
        key: key,
        hwid: null,
        created_at: new Date().toISOString(),
        expires_at: expiresAt,
        is_active: 1,
        max_hwid_changes: maxChanges,
        hwid_change_count: 0
    });

    saveDB();
    res.json({ key, expires_at: expiresAt });
});

app.get('/api/admin/licenses', adminAuth, (req, res) => {
    res.json(db.licenses.sort((a, b) => new Date(b.created_at) - new Date(a.created_at)));
});

app.delete('/api/admin/licenses/:key', adminAuth, (req, res) => {
    const license = db.licenses.find(l => l.key === req.params.key);
    if (license) {
        license.is_active = 0;
        saveDB();
    }
    res.json({ success: true });
});

app.get('/api/admin/logs', adminAuth, (req, res) => {
    res.json(db.auth_logs.slice(0, 100));
});

app.get('/api/admin/stats', adminAuth, (req, res) => {
    const totalLicenses = db.licenses.length;
    const activeLicenses = db.licenses.filter(l => l.is_active === 1).length;
    const totalAuthAttempts = db.auth_logs.length;
    const failedAuthAttempts = db.auth_logs.filter(l => l.success === false).length;

    res.json({
        totalLicenses,
        activeLicenses,
        totalAuthAttempts,
        failedAuthAttempts
    });
});

app.use(express.static(path.join(__dirname, 'public')));

const DEFAULT_ADMIN_PASSWORD = 'admin123';

if (db.admins.length === 0) {
    db.admins.push({
        username: 'admin',
        password_hash: bcrypt.hashSync(DEFAULT_ADMIN_PASSWORD, 10),
        created_at: new Date().toISOString()
    });
    saveDB();
} else {
    const admin = db.admins.find(a => a.username === 'admin');
    if (admin && !bcrypt.compareSync(DEFAULT_ADMIN_PASSWORD, admin.password_hash)) {
        admin.password_hash = bcrypt.hashSync(DEFAULT_ADMIN_PASSWORD, 10);
        saveDB();
        console.log('Admin password was incorrect - reset to default');
    }
}

app.listen(PORT, () => {
    console.log(`Zypher Auth Server running on port ${PORT}`);
    console.log(`Admin credentials: admin / ${DEFAULT_ADMIN_PASSWORD}`);
    console.log(`Change these immediately!`);
    console.log(`Dashboard: http://localhost:${PORT}`);
});
