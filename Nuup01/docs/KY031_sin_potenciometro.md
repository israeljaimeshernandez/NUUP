# Sensor de vibración KY-031 (versión sin potenciómetro)

Esta versión lleva únicamente un muelle con un contacto, sin comparador ni potenciómetro. Funciona como un interruptor que se cierra cuando el resorte vibra con fuerza.

## Cableado sugerido con GPIO33 (ESP32)
- **VCC** → 3.3 V del ESP32.
- **GND** → GND del ESP32.
- **S (salida)** → GPIO33.
- En el firmware del proyecto, GPIO33 puede usarse como fuente de *wake-up* con `ext0` y para leer el evento en el `loop()`.

### Polaridad y resistencia de entrada
- Configura el pin como `INPUT_PULLUP` para que, en reposo, GPIO33 esté en **HIGH** y el sensor cierre a **GND** cuando detecta un golpe.
- Si prefieres la lógica inversa, usa `INPUT_PULLDOWN` (reposo en **LOW**, pulso en **HIGH**), pero la mayoría de módulos KY-031 están pensados para cerrar a GND.

## Cómo filtrar vibraciones leves
Al no tener potenciómetro, la sensibilidad solo puede bajarse con medidas mecánicas y de filtrado:

1. **Orientación y montaje:** coloca el módulo de modo que el muelle quede paralelo al eje del golpe que quieres detectar. Si se activa con vibraciones leves, monta el módulo sobre espuma o cinta de doble cara para amortiguar.
2. **Peso/limitación:** añade un poco de termorretráctil o cinta al muelle para que necesite más desplazamiento antes de cerrar el contacto.
3. **RC + software:** puedes añadir un condensador pequeño (por ejemplo 100 nF entre S y GND) y un `debounce` en código (ej. esperar que el pin permanezca activo >20–50 ms) para ignorar contactos muy breves.
4. **Umbral por conteo:** sólo considera detección si hay varios cierres rápidos dentro de una ventana corta (p. ej., 2–3 eventos en 200 ms), típico de una sacudida fuerte.

## Comportamiento esperado
- **Reposo:** contacto abierto, el pin queda en el nivel marcado por la resistencia interna.
- **Sacudida fuerte:** el muelle golpea el contacto y el pin cambia de nivel durante unos milisegundos. Cada golpe produce un pulso; con el filtro RC + `debounce` puedes suavizarlo.

## Ejemplo de uso (solo lectura/impresión)
Pseudocódigo para imprimir cuando se detecte sacudida con `INPUT_PULLUP`:

```cpp
pinMode(33, INPUT_PULLUP);
...
if (digitalRead(33) == LOW) {
  if (millis() - ultimaDeteccion > 50) {
    Serial.println("Sensor detectado");
  }
  ultimaDeteccion = millis();
}
```

Para que despierte de *deep sleep*, habilita `esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0)` (disparo en LOW) antes de `esp_deep_sleep_start();`.
