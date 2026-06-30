let selectedProduct = null;
let selectedTier = null;

async function loadProducts() {
    try {
        const response = await fetch('/api/products');
        const products = await response.json();

        const grid = document.getElementById('productsGrid');

        if (products.length === 0) {
            grid.innerHTML = `
                <div style="grid-column: 1/-1; text-align: center; padding: 60px; color: var(--text-dim);">
                    <h3 style="color: var(--primary); margin-bottom: 20px;">Products Coming Soon</h3>
                    <p>We're working hard to bring you amazing products. Check back soon!</p>
                </div>
            `;
            return;
        }

        grid.innerHTML = products.map(product => `
            <div class="product-card">
                <div class="product-header">
                    <div class="product-name">${product.name}</div>
                </div>
                <div class="product-body">
                    <p style="color: var(--text-dim); margin-bottom: 20px;">${product.description}</p>
                    <div class="tier-options">
                        ${product.tiers.map(tier => `
                            <div class="tier-option" onclick="selectTier('${product.id}', '${tier.id}', '${tier.name}', ${tier.price})">
                                <div class="tier-name">${tier.name}</div>
                                <div class="tier-price">$${tier.price.toFixed(2)}</div>
                            </div>
                        `).join('')}
                    </div>
                    <button class="buy-btn" onclick="openPurchaseModal('${product.id}')">
                        Buy Now
                    </button>
                </div>
            </div>
        `).join('');
    } catch (error) {
        console.error('Failed to load products:', error);
    }
}

function selectTier(productId, tierId, tierName, price) {
    selectedTier = { id: tierId, name: tierName, price };
    document.querySelectorAll('.tier-option').forEach(el => el.classList.remove('selected'));
    event.currentTarget.classList.add('selected');
}

function openPurchaseModal(productId) {
    if (!selectedTier) {
        alert('Please select a pricing tier first');
        return;
    }

    selectedProduct = { id: productId };
    document.getElementById('purchaseModal').classList.add('active');
}

function openModal(type) {
    if (type === 'contact') {
        document.getElementById('contactModal').classList.add('active');
    }
}

function closeModal() {
    document.querySelectorAll('.modal').forEach(m => m.classList.remove('active'));
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
            headers: {
                'Content-Type': 'application/json'
            },
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

// Close modal when clicking outside
document.querySelectorAll('.modal').forEach(modal => {
    modal.addEventListener('click', (e) => {
        if (e.target === modal) {
            closeModal();
        }
    });
});

// Load products on page load
window.addEventListener('DOMContentLoaded', loadProducts);

// Smooth scroll for navigation links
document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
        e.preventDefault();
        const target = document.querySelector(this.getAttribute('href'));
        if (target) {
            target.scrollIntoView({
                behavior: 'smooth',
                block: 'start'
            });
        }
    });
});
