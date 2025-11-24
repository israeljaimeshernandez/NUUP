const defaultCatalog = [
  {
    id: 'nuup01',
    type: 'Monitor',
    name: 'NUUP 01 Monitor',
    description: 'Monitor LoRa con WiFi/BLE para telemetría y visualización en sitio.',
    price: 2499,
    stock: 12,
    image: 'https://images.unsplash.com/photo-1582719478248-44e9ba89a653?auto=format&fit=crop&w=900&q=80',
    mercadoLibre: 'https://articulo.mercadolibre.com.mx',
    amazon: 'https://www.amazon.com',
  },
  {
    id: 'nuup02',
    type: 'Sensor',
    name: 'NUUP 02 Sensor de Nivel de Agua',
    description: 'Sensor sumergible para nivel de agua con comunicación LoRa y alertas.',
    price: 1899,
    stock: 20,
    image: 'https://images.unsplash.com/photo-1469474968028-56623f02e42e?auto=format&fit=crop&w=900&q=80',
    mercadoLibre: 'https://articulo.mercadolibre.com.mx',
    amazon: 'https://www.amazon.com',
  },
  {
    id: 'nuup03',
    type: 'Sensor',
    name: 'NUUP 03 Sensor de Nivel de Gas',
    description: 'Sensor para tanques de gas con conectividad LoRa y avisos por MQTT.',
    price: 2099,
    stock: 18,
    image: 'https://images.unsplash.com/photo-1516116216624-53e697fedbea?auto=format&fit=crop&w=900&q=80',
    mercadoLibre: 'https://articulo.mercadolibre.com.mx',
    amazon: 'https://www.amazon.com',
  },
];

const defaultChannels = ['Stripe', 'PayPal', 'Mercado Libre', 'Amazon'];

const catalog = JSON.parse(localStorage.getItem('nuup-catalog') || 'null') || defaultCatalog;
let channels = JSON.parse(localStorage.getItem('nuup-channels') || 'null') || defaultChannels;
const cart = [];
let adminEnabled = false;

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

function formatCurrency(value) {
  return new Intl.NumberFormat('es-MX', { style: 'currency', currency: 'MXN' }).format(value);
}

function renderChannels() {
  const container = $('#sales-channels');
  container.innerHTML = '';
  channels.forEach((channel) => {
    const pill = document.createElement('span');
    pill.className = 'pill';
    pill.textContent = channel.trim();
    container.appendChild(pill);
  });
}

function renderProducts() {
  const grid = $('#product-grid');
  const template = $('#product-card-template');
  grid.innerHTML = '';
  catalog.forEach((product) => {
    const node = template.content.cloneNode(true);
    node.querySelector('.product-image').src = product.image;
    node.querySelector('.product-image').alt = product.name;
    node.querySelector('.product-type').textContent = product.type;
    node.querySelector('.product-name').textContent = product.name;
    node.querySelector('.product-description').textContent = product.description;
    node.querySelector('.price').textContent = formatCurrency(product.price);
    node.querySelector('.stock').textContent = `${product.stock} en almacén`;

    const marketplaceTags = node.querySelector('.marketplace-tags');
    marketplaceTags.innerHTML = '';
    ['mercadoLibre', 'amazon'].forEach((key) => {
      if (product[key]) {
        const tag = document.createElement('span');
        tag.className = 'pill';
        tag.textContent = key === 'mercadoLibre' ? 'Mercado Libre' : 'Amazon';
        marketplaceTags.appendChild(tag);
      }
    });

    node.querySelector('.add-to-cart').addEventListener('click', () => addToCart(product.id));
    grid.appendChild(node);
  });
}

function renderCart() {
  const container = $('#cart-items');
  const template = $('#cart-item-template');
  container.innerHTML = '';

  if (cart.length === 0) {
    container.innerHTML = '<p class="muted">El carrito está vacío.</p>';
    $('#cart-total').textContent = 'Total: $0.00';
    return;
  }

  let total = 0;
  cart.forEach((item) => {
    const product = catalog.find((p) => p.id === item.id);
    if (!product) return;
    const node = template.content.cloneNode(true);
    node.querySelector('.cart-name').textContent = product.name;
    node.querySelector('.cart-marketplace').textContent = getMarketplaceLabel(product);
    const qtyInput = node.querySelector('.qty');
    qtyInput.value = item.qty;
    qtyInput.addEventListener('change', (event) => updateQuantity(item.id, Number(event.target.value)));
    node.querySelector('.price').textContent = formatCurrency(product.price * item.qty);
    node.querySelector('.remove').addEventListener('click', () => removeFromCart(item.id));
    container.appendChild(node);
    total += product.price * item.qty;
  });

  $('#cart-total').textContent = `Total: ${formatCurrency(total)}`;
}

function getMarketplaceLabel(product) {
  if (product.mercadoLibre) return 'Vende en Mercado Libre';
  if (product.amazon) return 'Vende en Amazon';
  return 'Pago directo en sitio';
}

function addToCart(productId) {
  const existing = cart.find((item) => item.id === productId);
  if (existing) {
    existing.qty += 1;
  } else {
    cart.push({ id: productId, qty: 1 });
  }
  renderCart();
}

function updateQuantity(productId, qty) {
  const item = cart.find((entry) => entry.id === productId);
  if (item) {
    item.qty = Math.max(1, qty);
    renderCart();
  }
}

function removeFromCart(productId) {
  const index = cart.findIndex((entry) => entry.id === productId);
  if (index !== -1) {
    cart.splice(index, 1);
    renderCart();
  }
}

function resetCart() {
  cart.splice(0, cart.length);
  renderCart();
}

function renderAdmin() {
  const container = $('#admin-products');
  const template = $('#admin-product-template');
  container.innerHTML = '';
  catalog.forEach((product) => {
    const node = template.content.cloneNode(true);
    node.querySelectorAll('[data-field]').forEach((input) => {
      const field = input.dataset.field;
      input.value = product[field] || '';
      input.addEventListener('input', (event) => updateProduct(product.id, field, event.target.value));
    });
    container.appendChild(node);
  });
  $('#channels-input').value = channels.join(', ');
}

function updateProduct(id, field, value) {
  const product = catalog.find((item) => item.id === id);
  if (!product) return;
  if (field === 'price' || field === 'stock') {
    product[field] = Number(value) || 0;
  } else {
    product[field] = value;
  }
  persist();
  renderProducts();
  renderCart();
  renderMarketplaceLinks();
}

function persist() {
  localStorage.setItem('nuup-catalog', JSON.stringify(catalog));
  localStorage.setItem('nuup-channels', JSON.stringify(channels));
}

function toggleAdmin() {
  adminEnabled = !adminEnabled;
  $('#admin').classList.toggle('hidden', !adminEnabled);
}

function setupAdminActions() {
  $('#admin-toggle').addEventListener('click', toggleAdmin);
  $('#reset-admin').addEventListener('click', () => {
    localStorage.removeItem('nuup-catalog');
    localStorage.removeItem('nuup-channels');
    window.location.reload();
  });
  $('#channels-input').addEventListener('change', (event) => {
    channels = event.target.value.split(',').map((ch) => ch.trim()).filter(Boolean);
    if (channels.length === 0) channels = defaultChannels;
    persist();
    renderChannels();
  });
}

function renderMarketplaceLinks() {
  const container = $('#marketplace-links');
  container.innerHTML = '';
  catalog.forEach((product) => {
    const wrapper = document.createElement('div');
    wrapper.className = 'pill-group';
    const title = document.createElement('strong');
    title.textContent = product.name;
    wrapper.appendChild(title);

    if (product.mercadoLibre) {
      const link = document.createElement('a');
      link.href = product.mercadoLibre;
      link.target = '_blank';
      link.rel = 'noreferrer';
      link.className = 'pill';
      link.textContent = 'Mercado Libre';
      wrapper.appendChild(link);
    }

    if (product.amazon) {
      const link = document.createElement('a');
      link.href = product.amazon;
      link.target = '_blank';
      link.rel = 'noreferrer';
      link.className = 'pill';
      link.textContent = 'Amazon';
      wrapper.appendChild(link);
    }

    if (!product.mercadoLibre && !product.amazon) {
      const empty = document.createElement('span');
      empty.className = 'pill';
      empty.textContent = 'Pago directo en sitio';
      wrapper.appendChild(empty);
    }

    container.appendChild(wrapper);
  });
}

function setupCheckout() {
  const emailInput = $('#buyer-email');
  const customerReference = $('#customer-reference');

  const updateReference = () => {
    const email = emailInput.value.trim();
    customerReference.textContent = `Referencia de cliente: ${email || 'sin correo'}`;
  };

  $('#pay-now').addEventListener('click', () => {
    if (cart.length === 0) {
      $('#payment-link').textContent = 'Agrega productos al carrito antes de pagar.';
      return;
    }
    const provider = $('#payment-provider').value;
    const email = emailInput.value.trim();
    const notes = $('#billing-notes').value;
    const reference = `NUUP-${Date.now()}`;
    const total = cart.reduce((sum, item) => {
      const product = catalog.find((p) => p.id === item.id);
      return sum + (product ? product.price * item.qty : 0);
    }, 0);

    const summary = `Genera un checkout en ${provider} por ${formatCurrency(total)}. Referencia ${reference}. Cliente: ${email || 'sin correo'}. Notas: ${notes || 'N/A'}`;
    $('#payment-link').textContent = summary;
  });

  $('#shipping-quote').addEventListener('click', () => {
    const provider = $('#shipping-provider').value;
    const address = $('#shipping-address').value.trim();
    if (!address) {
      $('#shipping-summary').textContent = 'Captura una dirección para cotizar.';
      return;
    }
    const base = 150;
    const extra = Math.max(0, cart.length - 1) * 40;
    const emailNote = emailInput.value.trim() ? ` | Contacto: ${emailInput.value.trim()}` : '';
    $('#shipping-summary').textContent = `Cotiza en ${provider.toUpperCase()} aprox. ${formatCurrency(base + extra)} para ${cart.length} productos${emailNote}.`;
  });

  emailInput.addEventListener('input', updateReference);
  updateReference();
}

function bootstrap() {
  renderChannels();
  renderProducts();
  renderCart();
  renderAdmin();
  renderMarketplaceLinks();
  setupAdminActions();
  setupCheckout();
  $('#clear-cart').addEventListener('click', resetCart);
}

document.addEventListener('DOMContentLoaded', bootstrap);
