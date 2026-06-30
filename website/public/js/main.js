let selectedProduct = null;
let selectedTier = null;

function checkUserAuth() {
    const token = localStorage.getItem('zypher_user_token');
    const user = JSON.parse(localStorage.getItem('zypher_user_data') || 'null');
    const navActions = document.querySelector('.nav-actions');
    if (token && user && navActions) {
        navActions.innerHTML = '<a href="/account" class="btn-ghost" style="text-decoration:none;color:var(--primary);">' + user.username + '</a>';
    }
}

function openModal(type) {
    if (type === 'contact') {
        document.getElementById('contactModal').classList.add('active');
    } else if (type === 'login') {
        document.getElementById('loginModal').classList.add('active');
    } else if (type === 'register') {
        document.getElementById('registerModal').classList.add('active');
    }
}

function switchToRegister() {
    document.getElementById('loginModal').classList.remove('active');
    document.getElementById('loginError').textContent = '';
    document.getElementById('registerModal').classList.add('active');
}

function switchToLogin() {
    document.getElementById('registerModal').classList.remove('active');
    document.getElementById('registerError').textContent = '';
    document.getElementById('loginModal').classList.add('active');
}

function doUserLogin() {
    var email = document.getElementById('loginEmail').value;
    var password = document.getElementById('loginPassword').value;
    var errEl = document.getElementById('loginError');
    errEl.textContent = '';

    if (!email || !password) { errEl.textContent = 'Please fill in all fields'; return; }

    fetch('/api/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ email: email, password: password })
    }).then(function(res) { return res.json(); })
    .then(function(data) {
        if (data.token) {
            localStorage.setItem('zypher_user_token', data.token);
            localStorage.setItem('zypher_user_data', JSON.stringify(data.user));
            window.location.href = '/account';
        } else {
            errEl.textContent = data.error || 'Login failed';
        }
    }).catch(function() {
        errEl.textContent = 'Connection error';
    });
}

function doUserRegister() {
    var username = document.getElementById('regUsername').value;
    var email = document.getElementById('regEmail').value;
    var password = document.getElementById('regPassword').value;
    var errEl = document.getElementById('registerError');
    errEl.textContent = '';

    if (!username || !email || !password) { errEl.textContent = 'Please fill in all fields'; return; }
    if (password.length < 6) { errEl.textContent = 'Password must be at least 6 characters'; return; }

    fetch('/api/auth/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, email: email, password: password })
    }).then(function(res) { return res.json(); })
    .then(function(data) {
        if (data.token) {
            localStorage.setItem('zypher_user_token', data.token);
            localStorage.setItem('zypher_user_data', JSON.stringify(data.user));
            window.location.href = '/account';
        } else {
            errEl.textContent = data.error || 'Registration failed';
        }
    }).catch(function() {
        errEl.textContent = 'Connection error';
    });
}

async function loadProducts() {
    try {
        const response = await fetch('/api/products');
        const products = await response.json();

        const grid = document.getElementById('productsGrid');

        if (products.length === 0) {
            grid.innerHTML = `
                <div style="grid-column: 1/-1; text-align: center; padding: 60px; color: var(--text-dim);">
                    <h3 style="color: var(--text); margin-bottom: 20px;">Products Coming Soon</h3>
                    <p>We're working hard to bring you amazing products. Check back soon!</p>
                </div>
            `;
            return;
        }

        grid.innerHTML = products.map(product => {
            const prices = product.tiers.map(t => t.price);
            const minPrice = Math.min(...prices);
            const maxPrice = Math.max(...prices);

            return `
            <div class="product-card" onclick="openProductDetail('${product.id}')">
                <div class="product-image">
                    <div class="product-box-art">
                        <div class="box-art-front">
                            <span class="product-icon">⚡</span>
                            <h3>${product.name}</h3>
                        </div>
                    </div>
                </div>
                <div class="product-info">
                    <div class="product-header">
                        <h3 class="product-title">${product.name}</h3>
                        <div class="product-stock">
                            <span class="stock-dot"></span>
                            In Stock
                        </div>
                    </div>
                    <p class="product-desc">${product.description}</p>
                    <div class="product-price-range">
                        <span class="price-from">$${minPrice.toFixed(2)}</span>
                        <span class="price-to">$${maxPrice.toFixed(2)}</span>
                    </div>
                </div>
            </div>
            `;
        }).join('');
    } catch (error) {
        console.error('Failed to load products:', error);
    }
}

function openProductDetail(productId) {
    fetch('/api/products').then(r => r.json()).then(products => {
        const product = products.find(p => p.id === productId);
        if (!product) return;

        selectedProduct = { id: productId };

        document.getElementById('detailProductName').textContent = product.name;
        document.getElementById('detailProductName2').textContent = product.name;
        document.getElementById('detailProductDesc').textContent = product.description;

        document.getElementById('detailSysReq').innerHTML = `
            <div class="sys-req-grid">
                <div class="sys-req-item">
                    <span class="sys-req-label">OS</span>
                    <span class="sys-req-value">Windows 10/11 (64-bit)</span>
                </div>
                <div class="sys-req-item">
                    <span class="sys-req-label">Processor</span>
                    <span class="sys-req-value">Intel i5 or AMD equivalent</span>
                </div>
                <div class="sys-req-item">
                    <span class="sys-req-label">Memory</span>
                    <span class="sys-req-value">8 GB RAM</span>
                </div>
                <div class="sys-req-item">
                    <span class="sys-req-label">Storage</span>
                    <span class="sys-req-value">500 MB available</span>
                </div>
            </div>
        `;

        const tiersContainer = document.getElementById('detailTiers');
        tiersContainer.innerHTML = product.tiers.map(tier => `
            <div class="tier-option" onclick="selectTier('${product.id}', '${tier.id}', '${tier.name}', ${tier.price})">
                <div class="tier-name">${tier.name}</div>
                <div class="tier-price">$${tier.price.toFixed(2)}</div>
            </div>
        `).join('');

        document.getElementById('productDetailModal').classList.add('active');
    });
}

function closeProductDetail() {
    document.getElementById('productDetailModal').classList.remove('active');
    selectedProduct = null;
    selectedTier = null;
}

function selectTier(productId, tierId, tierName, price) {
    selectedTier = { id: tierId, name: tierName, price };
    document.querySelectorAll('#detailTiers .tier-option').forEach(el => el.classList.remove('selected'));
    event.currentTarget.classList.add('selected');
}

function openPurchaseModal() {
    if (!selectedTier) {
        alert('Please select a pricing tier first');
        return;
    }

    closeProductDetail();
    document.getElementById('purchaseModal').classList.add('active');
}

function closeModal() {
    document.querySelectorAll('.modal').forEach(m => m.classList.remove('active'));
    document.getElementById('loginError').textContent = '';
    document.getElementById('registerError').textContent = '';
}

async function completePurchase() {
    const email = document.getElementById('buyerEmail').value;

    if (!email || !email.includes('@')) {
        alert('Please enter a valid email address');
        return;
    }

    if (!selectedProduct || !selectedTier) {
        alert('Please select a product and pricing tier');
        return;
    }

    try {
        const response = await fetch('/api/checkout', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                email: email,
                productId: selectedProduct.id,
                tierId: selectedTier.id
            })
        });

        const data = await response.json();

        if (data.url) {
            window.location.href = data.url;
        } else {
            alert('Checkout failed: ' + (data.error || 'Please try again.'));
        }
    } catch (error) {
        console.error('Purchase error:', error);
        alert('An error occurred. Please try again.');
    }
}

async function sendContact() {
    const email = document.getElementById('contactEmail').value;
    const message = document.getElementById('contactMessage').value;

    if (!email || !message) {
        alert('Please fill in all fields');
        return;
    }

    alert('Message sent! We\'ll get back to you soon.');
    closeModal();
}

document.querySelectorAll('.modal').forEach(modal => {
    modal.addEventListener('click', (e) => {
        if (e.target === modal) {
            closeModal();
        }
    });
});

window.addEventListener('DOMContentLoaded', function() {
    loadProducts();
    checkUserAuth();
});

document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
        e.preventDefault();
        const target = document.querySelector(this.getAttribute('href'));
        if (target) {
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    });
});

document.addEventListener('keydown', function(e) {
    if (e.key === 'Enter') {
        if (document.getElementById('loginModal').classList.contains('active')) {
            doUserLogin();
        } else if (document.getElementById('registerModal').classList.contains('active')) {
            doUserRegister();
        }
    }
});
