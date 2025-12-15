# Flujo MQTT entre el broker y Monitor01

Este documento resume el intercambio de mensajes MQTT entre el broker y un Monitor01 de tipo **001**, incorporando el nuevo comportamiento del campo `DEVICE_MODIFICACION` del broker.

## Telemetría enviada por el Monitor01

- **Tópico**: `NUUP/<MAC_MONITOR>/...` (la MAC del monitor se extrae del segundo segmento, por lo que se mantiene el formato actual).
- **Payload**: `001,<MAC_SENSOR>,<litros>,<voltaje>,<temperatura>,<altura_cm>,<capacidad_litros>,<alias>`.
  - Si el alias llega vacío, el servidor lo normaliza a `NUUP01 NIVEL`.

## Confirmación del servidor hacia el monitor

- **Tópico fijo de respuesta**: `NUUP/<MAC_MONITOR>/confirmacion/`.
- **Payload cuando existe una modificación pendiente o `DEVICE_MODIFICACION = 1`**:
  - `mac_sensor,modificar,<alias_objetivo>,<altura_objetivo>,<capacidad_objetivo>,<litros_reportados>`
  - El dispositivo queda marcado como pendiente y este payload se repetirá en **cada** telemetría mientras el flag siga activo.
- **Payload cuando no hay cambios pendientes**:
  - `mac_sensor,sin_cambios`

## Confirmación de modificación enviada por el monitor

El monitor debe publicar en el mismo tópico `NUUP/<MAC_MONITOR>/confirmacion/` una segunda confirmación que limpie `DEVICE_MODIFICACION` cuando haya recibido una solicitud de ajuste:

- `mac_sensor,modificacion_ok`
- `mac_sensor,modificacion_aplicada`

Si el monitor responde `mac_sensor,sin_cambios` mientras hay un ajuste pendiente, el flag **no** se limpia y el broker volverá a pedir la modificación en la siguiente telemetría.

## Secuencia completa (consecutivo)

### Ciclo MQTT entre broker y monitor

1. El Monitor01 envía telemetría `001,...` a `NUUP/<MAC_MONITOR>/...` con litros, voltaje, temperatura, altura, capacidad y alias actuales, y queda a la espera de la confirmación en `NUUP/<MAC_MONITOR>/confirmacion/`.
2. El broker valida la MAC del sensor en base de datos y revisa el campo `DEVICE_MODIFICACION`.
3. **Si `DEVICE_MODIFICACION = 1` o detecta diferencias**:
   - Devuelve `mac_sensor,modificar,<alias_objetivo>,<altura_objetivo>,<capacidad_objetivo>,<litros_reportados>` en `NUUP/<MAC_MONITOR>/confirmacion/` y deja el dispositivo como pendiente.
   - El monitor recibe el mensaje de modificación, actualiza su EEPROM con alias, altura y capacidad nuevos, y responde `mac_sensor,modificacion_ok` (o `mac_sensor,modificacion_aplicada`) en el mismo tópico.
   - Con la confirmación positiva, el broker limpia `DEVICE_MODIFICACION` y en la siguiente telemetría enviará `mac_sensor,sin_cambios`.
   - Después de confirmar, el monitor continúa su ciclo normal sin enviar confirmaciones adicionales en telemetrías subsecuentes.
4. **Si no hay cambios pendientes**:
   - El broker responde `mac_sensor,sin_cambios` en `NUUP/<MAC_MONITOR>/confirmacion/`.
   - El monitor no realiza acciones adicionales y continúa el ciclo de telemetría.

### Ciclo Monitor01 <-> Sensor01 (LoRa)

1. El Monitor01 recibe información del Sensor01 por LoRa usando los mismos mensajes que ya existen entre ambos.
2. Valida si la variable que indica `modificado_broker` está activa (derivada del flujo MQTT anterior):
   - **Si `modificado_broker = 1`**: el Monitor01 no sobreescribe su configuración con lo que envía el Sensor01; en su lugar, devuelve al sensor los valores de alias, altura y capacidad almacenados en la EEPROM (actualizados desde el broker), limpia la bandera y reinicia su ciclo normal.
   - **Si `modificado_broker = 0`**: el Monitor01 persiste normalmente la información recibida del Sensor01 y continúa sin cambios.
3. El intercambio Monitor01-Sensor01 mantiene la misma estructura de mensajes previa; solo se ajusta la lógica de persistencia cuando la bandera de modificación está activa.

## Resumen en español

- La telemetría del Monitor01 mantiene el formato `001,<MAC_SENSOR>,<litros>,<voltaje>,<temperatura>,<altura_cm>,<capacidad_litros>,<alias>` y usa el tópico `NUUP/<MAC_MONITOR>/...`.
- El broker siempre responde en `NUUP/<MAC_MONITOR>/confirmacion/`, enviando `modificar,...` cuando `DEVICE_MODIFICACION = 1` o hay diferencias, y `sin_cambios` cuando todo está alineado.
- Ante un `modificar,...`, el monitor ajusta alias, altura y capacidad en EEPROM, confirma con `modificacion_ok` o `modificacion_aplicada`, y solo necesita esa confirmación una vez para limpiar la bandera.
- Cuando el monitor recibe datos del Sensor01 por LoRa y existe `modificado_broker`, mantiene los valores que ya tiene en EEPROM (provenientes del broker), los reenvía al sensor como confirmación y elimina la bandera; de lo contrario, persiste la información del sensor normalmente.
