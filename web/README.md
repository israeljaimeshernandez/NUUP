# Catálogo web NUUP

Interfaz estática para listar dispositivos (NUUP 01 Monitor, NUUP 02 Sensor de Nivel de Agua y NUUP 03 Sensor de Nivel de Gas), permitir agregar al carrito y preparar un checkout configurable con proveedores de pago/envío o redirección a marketplaces.

## Cómo usar
1. Servir la carpeta `web/` con cualquier servidor estático (`python -m http.server 8080` o subirla a un hosting estático).
2. Abrir `http://localhost:8080` en el navegador.
3. El botón **Modo admin** activa el panel para cambiar nombre, precio, inventario, descripción, imagen y enlaces de Mercado Libre/Amazon. Los cambios se guardan en `localStorage` del navegador.
4. Desde **Checkout**:
   - **Pago directo** genera un resumen listo para crear un checkout en Stripe o PayPal (puedes reemplazarlo por tu llamada a la API real).
   - **Envío** calcula una guía aproximada según cantidad de ítems; adapta el cálculo con tu proveedor (DHL, FedEx, Estafeta).
   - **Marketplaces** muestra los enlaces configurados para redirigir al cliente a Mercado Libre/Amazon cuando prefieras vender ahí.

## Personalización visual
- Los colores y botones siguen la paleta de NUUP.SPACE (fondos azul marino, acentos verde neón y cyan). Ajusta cualquier variable en `styles.css` (sección `:root`) para cambiar tono o radios sin modificar el resto del layout.
- El encabezado incluye un emblema y la leyenda `NUUP.SPACE`; si tienes el logo oficial, reemplaza el bloque `.brand` en `index.html` con tu SVG o imagen manteniendo las clases para conservar el espaciado.

## Integración recomendada
- **Stripe/PayPal**: reemplaza la generación de texto en `app.js` por tu llamada a la API de checkout, usando `total`, `reference` y la lista `cart`.
- **Envío**: sustituye la cotización mock en `#shipping-quote` por tu API de paquetería y muestra la guía o costo devuelto.
- **Marketplaces**: en el panel admin pega las URLs de tus publicaciones en Mercado Libre/Amazon para que los botones se actualicen de inmediato en el catálogo.

## Archivos
- `index.html`: estructura del catálogo, carrito, checkout, marketplaces y panel admin.
- `styles.css`: estilos oscuros inspirados en NUUP.SPACE con tarjetas, chips y layout responsivo.
- `app.js`: lógica de catálogo, carrito, persistencia en `localStorage`, administración básica y placeholders de pago/envío.
