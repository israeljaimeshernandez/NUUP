# Monitor01

Este firmware para ESP32 actúa como concentrador de monitorización LoRa y pasarela MQTT/WiFi. El flujo principal vive en `src/main.cpp` y combina cinco piezas clave:

- **Portal de configuración WiFi y ID:** En modo AP (`NUUP_2025`) expone un servidor HTTP para dar de alta redes, elegir la activa, borrar redes y capturar el `userID`. Si no hay `userID` guardado solo muestra el formulario de registro. Con `userID` presente también publica la lista de redes y los dispositivos LoRa registrados.
- **Gestión de dispositivos LoRa:** `procesarMensajeLoRa()` y `recepcion_lora()` procesan mensajes para altas (`REG...`) y bajas (`BAJA...`), almacenan MACs en EEPROM (`guardarDispositivos()/cargarDispositivos()`) y mantienen el arreglo `configDispositivos`.
- **MQTT de alta del monitor:** `MQTT_ALTA()` envía `alta/1/solicitud/` con la MAC del monitor y el `userID`; el callback MQTT valida la confirmación en `alta/1/confirmacion/` y persiste los datos de usuario.
- **Sincronización de bajas:** Las eliminaciones de dispositivos por LoRa o por el portal web usan `eliminarDispositivo()` para limpiar EEPROM. Cuando el monitor está dado de alta en MQTT, `notificarBajaMQTT()` publica `baja/1/solicitud/` con la MAC del monitor, la MAC del dispositivo y opcionalmente el `userID` para que el backend borre el registro remoto.
- **Interfaz TFT y botones físicos:** La pantalla OLED muestra estado WiFi, progreso de emparejamientos y datos básicos de los dispositivos locales. Los botones `BOTON_S` y `BOTON_W` disparan el emparejamiento LoRa y la activación del modo AP, respectivamente.

## Flujo del portal web

1. `startAPMode()` levanta el AP, inicializa el DNS de captura y registra rutas como `/` (configuración), `/save` (nueva red), `/delete` (eliminar red), `/select` (elegir red), `/setid` (guardar ID) y `/delete-device` (eliminar dispositivo).
2. `handleRoot()` arma la página HTML. Cuando existe un `userID`, añade una sección **Dispositivos registrados** con la lista de MACs guardadas y botones para borrarlas.
3. `handleDeleteDevice()` valida la MAC recibida, usa `eliminarDispositivo()` para depurar EEPROM y, si hay conexión MQTT confirmada, llama a `notificarBajaMQTT()` para replicar la baja en el backend.

## Persistencia en EEPROM

- Redes WiFi y su estado activo se guardan con `saveNetworksToEEPROM()`/`loadNetworksFromEEPROM()`.
- El ID de usuario se almacena en `saveUserIDToEEPROM()` y se recupera con `loadUserIDFromEEPROM()`.
- Las MAC de dispositivos se serializan desde `configDispositivos` usando `guardarDispositivos()` y se recargan con `cargarDispositivos()`.

Esta descripción resume el comportamiento actual y las rutas disponibles para operar el monitor sin necesidad de revisar todo el código fuente.
