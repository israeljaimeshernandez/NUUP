/******************************************************************************
 * ╔═══════════════════════════════════════════════════════════════════════╗
 * ║                          NUUP AI                                      ║
 * ║                   Advanced Agentic Coding System                      ║
 * ╚═══════════════════════════════════════════════════════════════════════╝
 * 
 * Proyecto:    NUUP - Sistema de Monitoreo de Tanques de Agua
 * Componente:  Monitor01 - Estación Central de Monitoreo
 * Hardware:    ESP32 NodeMCU-32S (Espressif ESP32)
 * Plataforma:  PlatformIO + Arduino Framework
 *
 * CONSECUTIVO ACTUAL:
 * 134 - 2025-07-08 OLED vertical: márgenes ajustables, cabecera detallada y estatus MQTT=2 desde NUUP/<MAC>/estatus.
 * 133 - 2025-07-08 OLED vertical: márgenes configurables, manguera detallada y llenado por estatus MQTT=2.
 * 132 - 2025-07-08 OLED vertical: animación de llenado desde abajo al nivel actual cuando se está llenando.
 * 131 - 2025-07-08 OLED vertical: centrar tanque, manguera con animación de llenado y parpadeo <10% sin afectar operación.
 * 130 - 2025-07-08 Botón WiFi a GPIO33 + OLED I2C vertical (SDA15/SCL4) para nivel de agua con parpadeo <10% y operación aislada.
 * 129 - LoRa: corrección de modificación pendiente; el monitor ya no se queda fijo en litros antiguos y valida alias/altura/capacidad sin bloquear por litros actuales.
 * 128 - LoRa: al recibir datos mientras hay modificación pendiente se actualiza el timestamp para evitar "SIN DATOS" en OLED.
 * 127 - MQTT: el monitor atiende solicitudes de modificación iniciadas por el servidor (device_modificacion=1) aun sin
 *       mensaje LoRa previo, aplica EEPROM y mantiene la espera de confirmación hasta que NUUP01 reporte los valores nuevos.
 * 126 - LoRa: las confirmaciones por modificación pendiente envían altura/capacidad desde EEPROM y el parseo de payload usa
 *       la capacidad real en lugar de los litros actuales.
 *
 * DESCRIPCIÓN:
 * Dispositivo central que recibe datos de múltiples sensores NUUP01 vía LoRa,
 * gestiona hasta 50 dispositivos, conecta a WiFi/MQTT, y muestra información
 * en pantalla OLED. Incluye servidor BLE para emparejamiento de sensores.
 * 
 * CAPACIDADES:
 * - Recepción LoRa de sensores remotos
 * - Conectividad WiFi multi-red (hasta 3 redes guardadas)
 * - Integración MQTT con servidor remoto
 * - Portal cautivo para configuración web
 * - Servidor BLE para alta/baja de dispositivos
 * - Pantalla OLED 128x64 con interfaz visual
 * - Almacenamiento persistente en EEPROM (4KB)
 * - Gestión de hasta 50 dispositivos NUUP01
 * 
 * GESTIÓN DE ALTAS Y BAJAS MQTT:
 * 
 * Sistema de encolamiento persistente para solicitudes de alta/baja de dispositivos
 * que sobrevive a reinicios del ESP32, garantizando que las operaciones pendientes
 * se completen incluso sin conectividad WiFi/MQTT temporal.
 * 
 * TIPOS DE DISPOSITIVOS:
 * - Tipo 0: Monitor (este dispositivo) - Alta: alta/0/solicitud
 * - Tipo 1: Sensor NUUP01 Tanque - Alta: alta/1/solicitud | Baja: baja/1/solicitud
 * - Tipo 2: [Reservado futuro] - Alta: alta/2/solicitud | Baja: baja/2/solicitud
 * - Tipo 3: [Reservado futuro] - Alta: alta/3/solicitud | Baja: baja/3/solicitud
 * 
 * BAJAS ENCOLADAS (Persistencia EEPROM):
 * ┌─ Solicitud de baja ────────────────────────────────────────────────────┐
 * │ 1. Usuario solicita baja (Portal AP / BLE)                             │
 * │ 2. registrarBajaPendientePersistente() → Encola en EEPROM @ 3800       │
 * │ 3. ESP32 reinicia (opcional)                                           │
 * │ 4. cargarBajasPendientesEEPROM() → Restaura cola al arranque           │
 * │ 5. WiFi/MQTT conectan → procesarBajasPendientes()                      │
 * │ 6. publicarSolicitudBajaPersistente() → baja/[TIPO]/solicitud/         │
 * │ 7. Backend responde → baja/[TIPO]/confirmacion/ con "MAC,eliminado"    │
 * │ 8. limpiarBajaPendientePorMac() → Limpia EEPROM y RAM                  │
 * └────────────────────────────────────────────────────────────────────────┘
 * 
 * ⚠️  CANCELACIÓN AUTOMÁTICA DE BAJA:
 * - Si dispositivo se RE-REGISTRA → limpiarBajaPendientePorMac()
 * - Escenario: Usuario solicita baja pero luego da de alta mismo sensor
 * - Flujos: registrarDispositivo(), procesarRegistroBLE()
 * 
 * VALIDACIONES DE SEGURIDAD:
 * ✓ No se permiten bajas de tipo 0 (monitor) por flujo de sensores
 * ✓ Topics MQTT construidos dinámicamente según tipo de dispositivo
 * ✓ Validación en publicarSolicitudBaja() y publicarSolicitudBajaPersistente()
 * 
 * ALTAS ENCOLADAS (Persistencia EEPROM - Planificado):
 * ┌─ Solicitud de alta ────────────────────────────────────────────────────┐
 * │ 1. Usuario registra dispositivo (Portal AP / BLE / LoRa)               │
 * │ 2. registrarAltaPendientePersistente() → Encola en EEPROM @ 3900       │
 * │ 3. ESP32 reinicia (opcional)                                           │
 * │ 4. cargarAltasPendientesEEPROM() → Restaura cola al arranque           │
 * │ 5. WiFi/MQTT conectan → procesarAltasPendientes()                      │
 * │ 6. solicitarAltaNuupMQTT() → alta/[TIPO]/solicitud/                    │
 * │ 7. Backend responde → alta/[TIPO]/confirmacion/ con "MAC,registrado"   │
 * │ 8. limpiarAltaPendientePorMac() → Limpia EEPROM y RAM                  │
 * └────────────────────────────────────────────────────────────────────────┘
 * 
 * ⚠️  CANCELACIÓN AUTOMÁTICA DE ALTA:
 * - Si dispositivo se BORRA → limpiarAltaPendientePorMac()
 * - Escenario: Usuario registra sensor pero luego decide eliminarlo
 * - Flujos: eliminarDispositivo(), iniciarBajaDispositivo()
 * 
 * ESTRUCTURA EEPROM (4096 bytes):
 * 0-350:   Configuración Monitor (UserID, MQTT flags, etc.)
 * 400-699: User ID y perfil
 * 700-2999: ConfigDispositivos (50 dispositivos × ~46 bytes)
 * 3000-3700: Datos de usuario (nombre, email, teléfono, password)
 * 3800-3899: Bajas pendientes (10 slots × ~18 bytes) ✅ Implementado
 * 3900-3999: Altas pendientes (10 slots × ~18 bytes) 📋 Planificado
 * 

 * COMPILADO CON: Antigravity AI - Google DeepMind
 * FECHA: 2025-12-03
 * 
 ******************************************************************************/

// ============================================================================
// HISTORIAL DE VERSIONES Y CORRECCIONES
// ============================================================================
// 134 - 2025-07-08 OLED vertical: márgenes ajustables, cabecera detallada y estatus MQTT=2 desde NUUP/<MAC>/estatus.
// 133 - 2025-07-08 OLED vertical: márgenes configurables, manguera detallada y llenado por estatus MQTT=2.
// 132 - 2025-07-08 OLED vertical: animación de llenado desde abajo al nivel actual cuando se está llenando.
// 131 - 2025-07-08 OLED vertical: centrar tanque, manguera con animación de llenado y parpadeo <10%.
// 130 - 2025-07-08 Botón WiFi en GPIO33 y OLED vertical I2C (SDA15/SCL4) con nivel de agua y parpadeo <10%.
// 129 - 2025-07-07 LoRa: se actualizan lecturas mientras hay modificación pendiente y la validación no bloquea por litros actuales.
// 128 - 2025-07-07 LoRa: al recibir datos con modificación pendiente se actualiza el timestamp para evitar "SIN DATOS" en OLED.
// 127 - 2025-07-06 MQTT: solicitudes de modificación iniciadas por servidor se aplican en EEPROM aunque no exista LoRa previo,
//       manteniendo DEVICE_MODIFICACION activa hasta confirmar desde NUUP01.
// 126 - 2025-07-06 LoRa: confirmación en modificación pendiente incluye altura/capacidad de EEPROM y el parser usa capacidad real.
// 125 - 2025-07-06 LoRa/MQTT: sin_cambios no desmonta DEVICE_MODIFICACION mientras falta validar por LoRa; se conserva EEPROM.
// 124 - 2025-07-06 LoRa/MQTT: DEVICE_MODIFICACION permanece activo tras "modificacion_ok" del broker hasta validar datos por LoRa; la confirmación sigue usando EEPROM.
// 123 - 2025-07-05 LoRa: tras una modificación del broker se descarta la telemetría antigua y se reenvía confirmación con EEPROM hasta que NUUP01 reporte alias/altura/capacidad actualizados.
// 122 - 2025-07-05 MQTT: telemetría se publica en NUUP/<MAC_MONITOR> en lugar de la MAC del sensor, manteniendo confirmación en NUUP/<MAC_MONITOR>/confirmacion/ y bitácora clara.
// 121 - 2025-07-04 MQTT: trazas explican qué respuesta espera el monitor (modificar/sin_cambios/modificacion_ok) y en qué tópico debe llegar cuando DEVICE_MODIFICACION está activo; la espera se libera tras timeout sin bloquear la siguiente telemetría.
// 120 - 2025-07-03 MQTT: la espera de confirmación del broker no bloquea telemetría; tras timeout se libera y se reintentará en la siguiente lectura, dejando bitácora clara en serial.
// 119 - 2025-07-02 MQTT: bitácora clara de solicitud/espera/recepción de confirmación del broker por DEVICE_MODIFICACION; timeout visible y cierre del ciclo al aplicar cambios en EEPROM.
// 118 - 2025-07-01 MQTT/LoRa: broker con DEVICE_MODIFICACION pide alias/altura/capacidad/litros, se aplican en EEPROM, se confirma con modificacion_ok y se usan esos datos en LoRa hasta limpiar la bandera.
// 117 - 2025-06-27 LoRa: se confirma ajuste de potencia aun si la MAC no está activa, priorizando la compatibilidad con Nuup01.
// 116 - 2025-06-26 LoRa: confirmación de potencia alinea formato con Nuup01 y corrige el parseo de solicitudes sin tercera barra.
// 115 - 2025-06-22 LoRa: integrar consecutivo y detallar modificación activa en bitácora de arranque
// 114 - 2025-06-22 LoRa: reanudar escucha tras el test periódico para no dejar al monitor sordo frente a confirmaciones
// 113 - 2025-06-21 LoRa: alineación y bitácora cruzada con Nuup01 (SyncWord/Preámbulo/BW/SF/CR) dejando apagado el eco bidireccional por defecto
// 112 - 2025-06-19 LoRa: trazas dev explican cada parámetro (TX/RSSI/SNR/tiempos) para diagnosticar envío y escucha
// 113 - 2025-06-20 Baja portal: al solicitar baja desde AP se fuerza solicitud MQTT con consecutivo en consola.
// 112 - 2025-06-19 Baja fábrica NUUP01: se documenta flujo con reintentos y confirmación antes de reiniciar.
// 111 - 2025-06-18 LoRa: eco bidireccional dev con trazas de espera/quality en respuestas monitor_*
// 110 - 2025-06-18 LoRa: modo LORA_bidireccional_borrar solo para desarrollo, activable por bandera y sin impacto en producción.
// 109 - 2025-06-18 LoRa: envío asíncrono con espera y watchdog protegido; trazas claras de modo escucha tras cada confirmación.
// 108 - 2025-06-18 LoRa: consecutivo en español documentado en cabecera; triple confirmación inmediata con separación configurable y bitácora al cambiar estado.
// 107 - 2025-06-18 LoRa: triple confirmación inmediata con separación configurable y estados serial solo al cambiar.
// 106 - 2025-06-17 LoRa: respuesta inmediata con potencia/RSSI visibles y estado en consola solo al iniciar o cambiar.
// 105 - 2025-06-16 LoRa: corrección de compilación tras refactor al núcleo 0 (llave extra removida).
// 104 - 2025-06-16 LoRa: tarea dedicada en el segundo núcleo, prioridad máxima y pausa temporal de BLE/WiFi/OLED mientras se confirma.
// 103 - 2025-06-15 LoRa: confirmación inmediata al recibir datos de NUUP01 y trazas descriptivas.
// 102 - 2025-06-14 LoRa: compatibilidad de confirmación reforzada, envío verificado y trazas claras aun con monitor inactivo/MQTT.
// 101 - 2025-06-14 LoRa: se confirma siempre por LoRa con los datos recibidos (nombre/altura/litros) y se persisten cambios en EEPROM.
// 100 - 2025-06-13 LoRa: se imprime en consola la recepción y la confirmación enviada, manteniendo trazabilidad inmediata.
// 98 - 2025-06-13 LoRa: NUUP01 recibe confirmación clara al enviar desde la ruta nuup/MAC, normalizando el payload y deteniendo reintentos.
// 97 - 2025-06-12 LoRa: NUUP01 recibe confirmación o error claro al responder; se documenta el ajuste en español.
// 96 - 2025-06-11 Potencia LoRa: monitor responde configuracion/MAC/solicitud confirmando nivel solicitado y mantiene TX al máximo.
// 95 - 2025-06-11 LoRa: el monitor confirma solo por LoRa; la publicación MQTT sigue igual, sin ACK extra al broker.
// 94 - 2025-06-10 UI: sin dispositivos muestra solo "SIN Dispositivos" y "NUUP" en pantalla, sin mensajes adicionales.
// 93 - 2025-06-10 Ajuste: mensajes LoRa con MAC no registrada se descartan sin auto-alta ni envío MQTT, solo se avisa en consola.
// 92 - 2025-06-09 Corrección: trazas detalladas de bajas pendientes (BLE/AP), limpieza de banderas al re-alta y reenvío inmediato tras reconexión MQTT para que la baja se complete aun después de reinicios.
// 91 - 2025-06-08 Corrección: las bajas solicitadas sin WiFi (portal o BLE) se encolan en EEPROM y se reintentan al reconectar MQTT hasta confirmarlas o re-registrar el dispositivo.
// 90 - 2025-06-07 Corrección: tras bajas solicitadas desde el portal AP se reconecta a WiFi/MQTT y se envía la solicitud de baja antes del reinicio, manteniendo la animación como en BLE.
// 89 - 2025-06-06 Corrección: bajas marcadas cierran el portal sin reactivar AP, textos simplificados (usuario, bajas y reseteo) y guía de alcance movida a configuración inicial.
// 88 - 2025-06-05 Corrección: la página AP permite marcar dispositivos para baja masiva al guardar, dispara el mismo flujo de baja que BLE antes del reinicio y documenta cómo ajustar el alcance BLE/WiFi.
// 87 - 2025-06-04 Corrección: la baja solicitada desde el portal AP cierra el modo AP, reanuda WiFi y ejecuta el mismo ciclo que BLE (animación, solicitud MQTT y reinicio), sumando bitácora en español.
// 86 - 2025-06-03 Corrección: el portal AP solo se abre con el botón WiFi; la pantalla inicial resume red guardada, registro MQTT y sensores.
// 85 - 2025-06-02 Ajuste portal y bajas: usuario fijo por correo, reseteo de fábrica al final y bajas vía botón/BLE con animación y solicitud MQTT.
// 84 - 2025-06-01 Corrección: el modo AP automático se bloquea tras reinicio si no hay redes guardadas; solo se activa con el botón o al fallar redes existentes.
// 83 - 2025-05-31 Consecutivo en español: se anuncia en consola la versión activa y su resumen breve.
// 82 - 2025-05-30 Flujo guiado de reseteo de fábrica: intenta reconectar WiFi, pide baja MQTT por 1 minuto,
//      muestra resultado en español con hora y luego borra EEPROM antes de reiniciar.
// 81 - 2025-05-29 Corrección: al fallar 3 reintentos de WiFi se activa el portal AP automáticamente
//      mostrando aviso en pantalla en español para permitir reconfiguración inmediata.
// 80 - 2025-05-28 Ajuste: el check "ya estoy registrado" oculta y bloquea los datos de usuario
//      desde que carga la página y en cada cambio, evitando cualquier captura mientras esté activo.
// 79 - 2025-05-28 Ajuste: si el usuario marca "ya estoy registrado" los campos de nombre/teléfono/
//      password desaparecen por completo y no se pueden capturar mientras el check esté activo.
// 78 - 2025-05-28 Corrección: el reinicio de fábrica también envía la baja MQTT con advertencia
//      visual, manteniendo el estilo de botones NUUP01, y el portal ya no borra los campos de
//      nombre/teléfono/password al alternar "ya estoy registrado".
// 77 - 2025-05-27 Consecutivo: se documenta la integración de la baja del monitor con su
//      confirmación MQTT, reteniendo la MAC enviada, limpiando EEPROM y reiniciando tras un
//      estado válido, alineado con la bitácora de cambios.
// 76 - 2025-05-27 Confirmación: se escucha baja/0/confirmacion/ del monitor, validando MAC y
//      estado para limpiar EEPROM/reiniciar tras la baja y manteniendo la MAC enviada aun si
//      ya fue borrada.
// 75 - 2025-05-26 Mejora: el portal AP oculta/bloquea campos al marcar "ya estoy registrado",
//      valida capturas completas en altas nuevas y agrega controles visuales para baja MQTT
//      del monitor y reinicio de fábrica, enviando baja/0/solicitud y borrando EEPROM.
// 74 - 2025-05-25 Ajuste: el identificador de usuario sigue siendo userID; se usa para
//      almacenar/leer el users_registro_id confirmado sin variables duplicadas.
// 73 - 2025-05-25 Integración: el portal AP guía el alta por correo/teléfono/nombre/password
//      con el combo "ya estoy registrado", oculta formularios tras guardar users_registro_id
//      y añade el botón de reinicio de fábrica. Las solicitudes MQTT de alta usan correo en
//      lugar de user_id y procesan confirmaciones devolviendo users_registro_id.
// 72 - 2025-05-24 Ajuste baja: mensaje de reinicio prolongado (20s) asegurando
//      borrado total en RAM/EEPROM antes de reiniciar.
// 71 - 2025-05-23 Corrección: baja inmediata y visual reforzada; al solicitar baja se elimina de EEPROM/arreglo sin esperar
//      confirmación, se muestra el nombre en pantalla 5s y luego "Reiniciando dispositivo" antes de reiniciar.
// 70 - 2025-05-23 Corrección: deduplicar confirmaciones MQTT (alta/1/confirmacion) para no reactivar ni imprimir varias veces
//      el mismo sensor/monitor tras reconexiones; se valida si ya estaba activo/confirmado y si el payload es igual, se ignora.
// 69 - 2025-05-22 Corrección: no se fuerza el tipo=1 en altas de Nuup01; se respeta el tipo registrado, se aborta si está en
//      cero para evitar registros corruptos y se sigue enviando el tipo real en el payload para que el backend diferencie
//      sensores sin duplicarlos como MONITOR NUUP. Además, se mantiene la MAC fija del monitor sin spam en consola.
// 68 - 2025-05-22 Corrección: se fija y se imprime una sola vez la MAC del monitor, se reutiliza en todas las rutas MQTT y se
//      documenta para detener ciclos en consola; además se refuerza que la MAC del monitor nunca se use en altas de sensores
//      evitando registros MONITOR NUUP extra con MAC de Nuup01.
// 67 - 2025-05-21 Corrección: se blinda la ruta de alta del monitor fijando su MAC al inicio y anulando cualquier intento de
//      usar la MAC de un sensor; trazas claras indican si se bloquea el envío y se documenta el motivo para evitar duplicados
//      MONITOR NUUP con MAC de sensor en la base de datos.
// 66 - 2025-05-21 Corrección: separar rutas de alta de monitor y Nuup01, forzando tipo=1 en sensores y bloqueando que su MAC
//      se publique como monitor; se normaliza y persiste el tipo antes de enviar MQTT para que el backend no cree MONITOR NUUP
// 65 - 2025-05-21 Corrección: eliminar altas duplicadas de Nuup01 corrigiendo payload MQTT (MAC,UserID,Nombre,Altura,Litros)
//      y bloqueando reenvíos idénticos; la MAC del sensor ya no se acepta en alta/0/solicitud para evitar registros tipo 0
// 64 - 2025-05-20 Corrección: documentar flujo MQTT de altas/bajas (monitor y Nuup01), trazas extendidas de tópicos/payloads
//      y resúmenes periódicos separados para Monitor01 y cada Nuup01 con todas sus banderas
//      Flujo monitor01: TX alta/0/solicitud -> "MAC_MONITOR,UserID" | RX alta/0/confirmacion -> "MAC_MONITOR,registrado,usuario,email"
//      Flujo alta nuup01: TX alta/1/solicitud -> "MAC_NUUP,UserID,Nombre,Altura,Litros,Tipo(1)" | RX alta/1/confirmacion -> "MAC_NUUP,registrado" activa MQTT
//      Flujo baja nuup01: TX baja/1/solicitud -> "MAC_NUUP,UserID" | RX baja/1/confirmacion -> "MAC_NUUP,eliminado" limpia flags/EEPROM
// 63 - 2025-05-19 Corrección: disparar alta MQTT inmediata tras BLE/LoRa cuando el monitor esté confirmado y detallar estados
// 62 - 2025-05-18 Corrección: alta BLE inactiva y sin duplicados en MQTT; normalizar MAC y bloquear publicación hasta confirmación
// 61 - 2025-05-16 Corrección: limpiar banderas de alta MQTT al borrar o dar de baja por BLE para evitar falsos registros
// 60 - 2025-05-15 Mejora: baja MQTT con espera configurable, reintentos y cancelación al re-registrar
// 59 - 2025-05-13 Corrección: normalizar y validar MAC LoRa en mayúsculas para evitar duplicados vacíos
// 58 - 2025-05-13 Corrección: reiniciar bandera de confirmación si EEPROM fue limpiada para evitar falsos positivos
// 57 - 2025-05-12 Corrección: procesar confirmación MQTT del monitor (alta/0/confirmacion) y frenar reintentos
// 56 - 2025-05-12 Corrección: asumir confirmación MQTT cuando EEPROM indica registro previo y evitar reintentos
// 55 - 2025-05-11 Corrección: marcar monitor confirmado cuando el registro previo está presente y evitar reenviar alta
// 54 - 2025-05-10 Corrección: no reenviar alta del monitor confirmado y altas pendientes inmediato + cada 5 minutos
// 53 - 2025-05-09 Corrección: reintento de alta MQTT cada 5 minutos para dispositivos pendientes
// 52 - 2025-05-07 Corrección: alta MQTT con nombre del dispositivo, tipo forzado y sin duplicados
// 51 - 2025-05-05 Corrección: solicitar alta por MQTT y publicar solo tras confirmación, texto en español
// 50 - 2025-05-04 Actualizar siempre datos LoRa y publicar la última versión en MQTT
// 49 - 2025-05-03 Detalle serial completo, limpieza total en baja y mensaje LoRa guardado por dispositivo
// 48 - 2025-05-03 Actualización: registro inactivo hasta confirmación MQTT y persistencia del mensaje LoRa completo
// 47 - Corrección: mostrar en pantalla SSID/contraseña/ID final por 5s antes de reiniciar
// 46 - Corrección: limpiar valor de contraseña mostrado, apilar botones Modificar/Borrar y resaltar contraseñas en amarillo
// 45 - Corrección: permitir finalizar sin SSID al borrar redes, limpiar valores vacíos y soportar ñ en contraseñas
// 44 - Corrección: portal vacío al abrir, renombrar/editar redes sin duplicar y borrar redes persiste en EEPROM
// 43 - Corrección: reparar el HTML del portal para que no muestre artefactos de las cadenas crudas
// 39 - Corrección: mostrar datos 5s antes de reiniciar, quitar leyenda extra y asegurar persistencia de redes
// 40 - Corrección: mantener SSID editable al abrir, bloquearlo al elegir "Usar" y guardar SSID+pass en EEPROM
// 41 - Corrección: resaltar la red elegida, mover la selección a un indicador compacto y garantizar que "Usar" llene el SSID
// 42 - Corrección: simplificar la selección quitando el chip, eliminar diagonales invertidas y mostrar/editar SSID y contraseña
// 38 - Corrección: validar guardado de red en EEPROM al finalizar configuración y abortar si falla
// 37 - Corrección: fijar mensaje final 3s, reinicio automático y SSID seleccionado visible/guardado en portal
// 36 - Corrección: mantener reinicio automático tras guardar y limpiar el portal/selección visual de redes
// 35 - Corrección: evitar bloqueos al guardar, resaltar red elegida y asegurar persistencia de red en EEPROM
// 34 - Corrección: fijar mensaje en display antes de reiniciar, persistir credenciales en EEPROM y reflejar red seleccionada al usarla
// 33 - Corrección: portal muestra UserID y red guardados, formulario centrado en contraseña y mensaje fijo antes de reiniciar
// 32 - Corrección: mantener portal estático, limpiar leyendas y mostrar credenciales en pantalla antes de reiniciar
// 31 - Corrección: desactivar reescaneos manuales para mantener la página fija mientras se configura
// 30 - Corrección: cancelar reinicios mientras el portal está activo para evitar cierres inesperados
// 29 - Corrección: mantener el portal abierto tras refrescar redes evitando cierres por recarga temprana
// 28 - Corrección: evitar reinicios inmediatos al seleccionar una red guardada para que el portal no se cierre
// 27 - Corrección: mantener el portal abierto al finalizar sin cerrar la pestaña, evitando errores al usuario
// 26 - Corrección: retirar leyendas de relleno en listas de redes y títulos para simplificar la interfaz
// 25 - Corrección: mantener el portal abierto cancelando reinicios mientras el usuario configura
// 24 - Corrección: reescribir botones HTML con escape doble para compilar sin errores
// 23 - Corrección: consolidar el escape de comillas en botones HTML para compilar sin errores
// 22 - Corrección: escapar atributos onclick con comillas y evitar errores de compilación en botones del portal
// 21 - Corrección: escapar las comillas de botones HTML para compilar sin errores y mantener las acciones del portal
// 20 - Corrección: procesar los escaneos WiFi incluso con el portal abierto para que el refresco muestre redes reales
// 19 - Corrección: permitir que el reescaneo WiFi concluya y refresque la lista en el portal
// 18 - Actualización: se agrega la leyenda inicial de actualizaciones con su consecutivo
// 17 - Corrección: simplificar el portal mostrando el User ID guardado y permitir usar/editar redes sin botones extra
// 16 - Corrección: permitir refrescar/editar redes, persistir UserID visible y reiniciar solo cuando cambian redes
// 15 - Corrección: mantener portal abierto hasta que el usuario termine; mostrar ID guardado y reiniciar al finalizar
// 14 - Corrección: mantener pantalla fija y dedicar todo el ciclo al portal mientras el usuario navega hasta cerrar/manual
// 13 - Corrección: congelar animación WiFi y dedicar el ciclo solo al portal cuando el usuario ya abrió la página
// 12 - Corrección: dedicar ciclo a portal WiFi cuando está activo y permitir scroll completo en la página
// 11 - Corrección: registrar rutas de portal cautivo para todos los probes y atenderlas de inmediato al activar AP
// 10 - Corrección: cachear el escaneo de redes y mantener el portal accesible mientras se completa para evitar cortes al abrir la página
// 09 - Corrección: el portal se atiende antes de cualquier animación (BLE/WiFi) para evitar cuelgues y "request handler not found"
// 08 - Corrección: el portal HTTP se sigue atendiendo mientras el AP esté activo para evitar bloqueos al navegar
// 07 - Corrección: portal unificado en una sola sesión con UserID, redes y dispositivos, bloqueando guardados si falta el ID
// 06 - Corrección: ajustar reinicio de portal WiFi quitando bandera inexistente para compilar correctamente
// 05 - Corrección: esconder WiFi al inicio (solo aviso breve) y mantener el portal activo tras guardar ID sin cerrarlo
// 04 - Corrección: iniciar portal WiFi si falta ID o conexión, proteger OLED antes de inicializarla y mantener reinicios seguros del AP
// 03 - Corrección: reiniciar el portal WiFi con cada pulsación larga, listar/borrar dispositivos y mostrar redes cercanas en el portal
// 02 - Corrección: validar y limpiar UserID corrupto en EEPROM y activar modo AP con botón WiFi de 1s mostrando icono y reinicio tras conexión
//Bersion BLE

#include <SPI.h>

//AP
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <time.h>
#include <math.h>

#include <LoRa.h>
#include <EEPROM.h>

//Pantalla TFT
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Wiffi
#include <WiFi.h>
#include "esp_wifi.h"  // Necesario para usar esp_wifi_set_mac()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//MQTT
#include <PubSubClient.h>

//BLE 
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

//EEPROM  Tamaño EEPROM (ESP32 tiene 4KB)
// Definir direcciones para nombre y email (después de tus otras configuraciones) 
#define USER_NAME_ADDR 3000    //
#define USER_PHONE_ADDR 3200   // Teléfono del usuario
#define USER_EMAIL_ADDR 3400   // Correo electrónico del usuario
#define USER_PASS_ADDR 3600    // Password del usuario
#define USER_REGISTERED_FLAG_ADDR 3700 // 1 = usuario ya existe en backend
#define BAJAS_PENDIENTES_ADDR 3800     // Lista persistente de MACs en baja pendiente
#define EEPROM_SIZE 4096              //
#define ALIAS_DISPOSITIVOS 2000       //

#define MQTT_CONFIRMED_FLAG_ADDR 350  //
#define MQTT_INITIAL_REQUEST_FLAG_ADDR 351
#define USER_ID_ADDR 400              //
#define CONFIG_DISPOSITIVOS_ADDR 700  //

#define USER_NAME_MAX_LEN 64
#define USER_PHONE_MAX_LEN 24
#define USER_EMAIL_MAX_LEN 64
#define USER_PASS_MAX_LEN 32

// Tiempos y tópicos principales (ajustes rápidos)
const unsigned long TIEMPO_SIN_DATOS = 120000;              // 2 minutos sin recibir LoRa → mostrar "SIN DATOS"
const char *TOPICO_LORA_BASE = "NUUP/";                    // Prefijo MQTT para datos LoRa

// Configuración WiFi
#define AP_SSID "NUUP_monitor01"// que permita el acceso directo finalmente no puede hacer nada hasta no ingresar un ID de usuario correcto "nuup"
#define AP_PASS ""
#define WIFI_TIMEOUT 5000 // 30 segundos
#define USER_ID_MAX_LEN 32    // Máximo 32 caracteres para el ID lo puedo cambiar si solo necesito el users.users_id concatenado a la clave NUUP2025

// --- Nueva Configuración para Dispositivos LoRa ---
#define MAX_DISPOSITIVOS 50         // Máximo de dispositivos registrables
#define MAC_LEN 17                  // Longitud de MAC (ej: "A0:B1:C2:D3:E4:F5")
#define VALORES_POR_DISPOSITIVO 5    // Máximo de valores por dispositivo
#define CONFIG_VERSION 0xA5         // Versión del bloque de dispositivos en EEPROM

uint8_t potenciaLoRaMonitorDbm = 20;  // Potencia actual usada para TX LoRa
volatile int ultimoRssiLoRaRx = 0;    // Calidad del último paquete recibido
volatile float ultimoSnrLoRaRx = 0.0; // Relación señal/ruido del último paquete recibido
const uint8_t REPETICIONES_CONFIRMACION_LORA = 3;          // Confirmaciones inmediatas repetidas
const unsigned long INTERVALO_CONFIRMACION_LORA_MS = 100;  // Pausa entre confirmaciones LoRa
volatile bool loraEnEscucha = false;                       // Estado de recepción activa para trazas
bool LORA_BIDIRECCIONAL_BORRAR = false;                    // Solo para desarrollo: debe permanecer en false en producción
unsigned long INTERVALO_BIDIRECCIONAL_LORA_MS = 100;       // Intervalo entre ciclos dev (ajustable)
uint32_t consecutivoMonitorBidireccional = 0;              // Contador de respuestas dev
uint32_t consecutivoConfirmacionesLoRa = 0;                // Consecutivo global de confirmaciones TX
const uint16_t CONSECUTIVO_CAMBIO_ACTUAL = 134;            // Última modificación documentada


//Redes guardadas
#define MAX_NETWORKS 3
#define SSID_LEN 32
#define PASS_LEN 64
#define NETWORK_SIZE (SSID_LEN + PASS_LEN + 2) // +2 para los bytes de longitud

// Pines LoRa
#define LORA_SS   5
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23


//*****************************
//***   LORA ***
//*****************************
// Variable global para almacenar el último mensaje LoRa recibido
SPIClass loraSPI(HSPI);  // Segundo SPI para LoRa

volatile bool nuevoMensajeLoRa = false;
String mensajeLoRa = "";
volatile bool loraProcesando = false;

struct LoRaProcessingGuard {
    volatile bool &flag;
    explicit LoRaProcessingGuard(volatile bool &flagRef) : flag(flagRef) { flag = true; }
    ~LoRaProcessingGuard() { flag = false; }
};


typedef struct {
  char mac[MAC_LEN + 1];  
  char nombre[20];        // Nombre del dispositivo
  float litrosActuales;   // Litros actuales
  float voltaje;          // Voltaje de batería
  float temperatura;      // Temperatura
  float alturaConfig;     // Altura configurada
  float litrosConfig;     // Litros configurados
  int porcentaje;         // ⭐ NUEVO: Porcentaje calculado
  byte tipoDispositivo;   // Tipo de dispositivo  
  bool activo;           // Para MQTT
} ConfigDispositivo;

ConfigDispositivo configDispositivos[MAX_DISPOSITIVOS];
//para asignar un numero de serial al dispositivo lector este viene del LORA pero ahorita lo fijmos aqui
const String serial_number = "TOPICMYSQL";  //aqui voy a maper la MAC

// Almacenar el último mensaje LoRa completo por dispositivo
String ultimoMensajeLoRaDispositivo[MAX_DISPOSITIVOS];
bool modificacionBrokerActiva[MAX_DISPOSITIVOS] = {false};
String aliasObjetivoBroker[MAX_DISPOSITIVOS];
float alturaObjetivoBroker[MAX_DISPOSITIVOS] = {0};
float capacidadObjetivoBroker[MAX_DISPOSITIVOS] = {0};
float litrosReportadosBroker[MAX_DISPOSITIVOS] = {0};
bool esperandoConfirmacionBroker = false;
String macEsperandoConfirmacion = "";
unsigned long inicioEsperaConfirmacion = 0;
const unsigned long timeoutConfirmacionBroker = 15000; // 15s para informar falta de respuesta

// Variables para control de tiempo sin datos
unsigned long ultimaActualizacionLoRa[MAX_DISPOSITIVOS] = {0};
bool mostrarSinDatos[MAX_DISPOSITIVOS] = {false};

// Control de solicitudes de alta por dispositivo
unsigned long ultimaSolicitudAlta[MAX_DISPOSITIVOS] = {0};
bool solicitudAltaEnviada[MAX_DISPOSITIVOS] = {false};
bool bajaPendienteMQTT[MAX_DISPOSITIVOS] = {false};
unsigned long ultimaSolicitudBaja[MAX_DISPOSITIVOS] = {0};
unsigned long inicioEsperaBaja[MAX_DISPOSITIVOS] = {0};
String ultimoPayloadAltaMQTT[MAX_DISPOSITIVOS];
String ultimoPayloadConfirmAlta[MAX_DISPOSITIVOS];
String ultimoPayloadConfirmMonitor = "";

// Lista persistente de bajas pendientes (portal AP/BLE sin WiFi)
const int MAX_BAJAS_PERSISTENTES = 10; // Espacio reservado en EEPROM (10 MACs)
char bajasPendientesMac[MAX_BAJAS_PERSISTENTES][MAC_LEN + 1] = {{0}};
bool bajasPendientesActivas[MAX_BAJAS_PERSISTENTES] = {false};
unsigned long bajasPendientesUltimoIntento[MAX_BAJAS_PERSISTENTES] = {0};
unsigned long bajasPendientesInicioEspera[MAX_BAJAS_PERSISTENTES] = {0};


// O si quieres hacerlo configurable via BLE/serial:
unsigned long tiempoSinDatosConfig = 60000; // Puedes cambiar este valor

//*****************************
//***   CONFIGURACION MQTT  ***
//*****************************
const char *mqtt_server ="168.231.66.42";//
const int  mqtt_port =2783; //Es docker
const char *mqtt_user="nuup_web";
const char *mqtt_pass ="Kfl-0878";
WiFiClient espClient;
PubSubClient client(espClient);
String macMonitorFija = "";  // MAC del monitor fijada al inicio para evitar usar MAC de sensores
bool macMonitorFijaAnunciada = false; // evita spam en consola cuando se dibuja la animación WiFi

long lastMsg = 0;

//*****************************
//***   ALTA MQTT DE MONITOR ***
//*****************************
bool mqttConfirmed = false;          // Bandera de confirmación MQTT
bool solicitudAltaInicialEnviada = false; // Controla que el alta tipo 0 solo se envíe una vez
unsigned long lastConfirmationAttempt = 0;
const unsigned long confirmationTimeout = 30000; // 30 segundos para esperar confirmación
const unsigned long confirmationRetryInterval = 10000; // segundos entre reintentos de conexion MQTT para el monitor
const unsigned long altaPendienteInterval = 5UL * 60UL * 1000UL; // 5 minutos entre solicitudes de alta pendientes
const unsigned long bajaConfirmTimeout = 30000; // Tiempo de espera de confirmación de baja MQTT
const unsigned long bajaPendienteInterval = 5UL * 60UL * 1000UL; // Reenvío de bajas pendientes
unsigned long lastAltaPendienteCheck = 0;
String userID = "";                // También se usa para almacenar users_registro_id confirmado
String userEmail = "";
String userNombre = "";
String userTelefono = "";
String userPassword = "";
bool userFlagRegistrado = false;  // true si el usuario ya existe y solo se busca por correo
bool bajaMonitorEsperandoConfirmacion = false; // true cuando se envió baja/0/solicitud
unsigned long inicioEsperaBajaMonitor = 0;     // inicio de ventana de confirmación de baja del monitor
String ultimaMacMonitorBaja = "";             // MAC usada en la última solicitud de baja para validar confirmación
bool registroMonitorEEPROM = false;  // Bandera de registro general (no se confunde con activo de dispositivos)
// === Alcances ajustables ===
// BLE monitor: POTENCIA_BLE_MONITOR controla la cercanía (N12 ≈ ~5 cm). Sube el nivel para más distancia.
// Proximidad BLE: RSSI_MIN_APAREAMIENTO_MONITOR limita el emparejamiento a RSSI igual o mayor.
// AP monitor: alcanceWiFiAPMetrosMonitor define la cobertura objetivo en metros y ajusta potenciaTxWiFiAPMonitor automáticamente.
esp_power_level_t POTENCIA_BLE_MONITOR = ESP_PWR_LVL_N12; // Ajusta alcance BLE (eleva para más distancia)
int RSSI_MIN_APAREAMIENTO_MONITOR = -45; // dBm objetivo (~5 cm) si se habilita proximidad por RSSI
int alcanceWiFiAPMetrosMonitor = 5;   // Alcance estimado del AP en metros (ajustable)
int potenciaTxWiFiAPMonitor = 11;     // dBm aplicados al AP según el alcance deseado
wifi_power_t potenciaWiFiAPMonitor = WIFI_POWER_11dBm; // Potencia WiFi usada al iniciar el AP


// Estructura para almacenar credenciales WIFFI
typedef struct {
  String ssid;
  String password;
  bool active;
} WiFiCredential;
// Objetos globales
WebServer server(80);
DNSServer dnsServer;
bool apMode = false;
bool forceAPMode = false;
bool portalEnUso = false;           // Se activa al servir la página para congelar la animación
bool portalPantallaFija = false;    // Evita reescribir la pantalla en cada loop
uint32_t consecutivoBajaPortal = 0; // Consecutivo de solicitudes de baja desde portal AP
bool reinicioSolicitado = false;    // Permite reiniciar tras finalizar configuración
unsigned long reinicioProgramado = 0;
//intento de reconectar
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 2 * 60 * 1000; ; // 5 minutos
WiFiCredential savedNetworks[MAX_NETWORKS];
int currentNetwork = -1;

// Cacheo de redes cercanas para no bloquear el portal en cada petición
String scannedNetworksCache = "";
unsigned long lastNetworkScan = 0;
bool scanInProgress = false;
const unsigned long SCAN_INTERVAL_MS = 15000;  // re-scan cada 15s en modo AP

// Declaración de funciones
void inicializa_eeprom();
void iniciarLoRaConReintentos();
ConfigDispositivo obtenerConfigPorMac(const String &macBuscada);
String extraerMacDeMensajeLoRa(const String &mensaje);
void clearEEPROM();
void startAPMode();
void registrarRutasPortal();
void handleRoot();
void handleSaveCredentials();
void handleFinalizeConfig();
void handleDeleteNetwork();
void handleSelectNetwork();
void handleDeleteDevice();
void handleBajaMonitor();
bool iniciarBajaPortal(const String &mac);
void reiniciarConfiguracionWiFi();
void detenerConfiguracionWiFi();
bool saveNetworksToEEPROM();
bool loadNetworksFromEEPROM();
bool attemptReconnectToAllNetworks();
void handleSetID();
void saveUserIDToEEPROM(const String& id);
bool loadUserIDFromEEPROM();
void saveUserProfileToEEPROM();
void loadUserProfileFromEEPROM();
void handleFactoryReset();
void callback(char* topic, byte* playload, unsigned int lengt);
void reconnect();
void  checkMemory();
void cargarDispositivos();
bool eliminarDispositivo(const String &mac);
ConfigDispositivo* getConfigDispositivo(const String &mac);
int obtenerIndiceDispositivo(const String &mac);


bool registrarDispositivo(const String &mac, byte tipo = 1);
bool esMacValida(const String &mac);
bool mensajeLoRaTieneDatos(const String &mensaje);
void solicitarAltaMonitorMQTT();
bool loadMQTTConfirmationState();
void guardarMQTTConfirmationState(bool estado);
bool loadSolicitudAltaInicialState();
void guardarSolicitudAltaInicialState(bool estado);
String obtenerTopicoConfirmacionMonitor();
void suscribirTopicoConfirmacionMonitor();
void publicarConfirmacionModificacionMQTT(const String &macSensor, const String &estado);
bool procesarConfirmacionBroker(const String &topic, const String &mensaje);
void asegurarMacMonitorFija(const char* motivo);
void imprimirConfigDispositivo(const String &mac);
void imprimirDispositivosRegistrados();
void Reintentar_Wiffi();
String normalizarPayloadParaMQTT(const String &payload);
void solicitarAltaNuupMQTT(int indice, const String &mac);
void intentarAltaTrasRegistro(int indice, const String &mac, const char* origen);
void procesarAltasPendientes();
String normalizarMac(const String &macRaw);
bool iniciarBajaDispositivo(const String &mac, const char* origen = "desconocido", bool forzarMqtt = false);
bool publicarSolicitudBaja(int indice, const String &mac);
bool solicitarBajaMonitorMQTT();
void procesarBajasPendientes();
void limpiarEstadoBaja(int indice);
int buscarSlotBajaPendiente(const String &mac);
int buscarSlotLibreBajaPendiente();
void guardarBajasPendientesEEPROM();
void cargarBajasPendientesEEPROM();
void registrarBajaPendientePersistente(const String &mac);
void limpiarBajaPendientePorMac(const String &mac);
int contarBajasPendientesRAM();
int contarBajasPendientesPersistentes();
void imprimirEstadoBajasPendientes(const char* origen);

void debugNetworks();
void checkWiFiStatus();

void iniciarEscaneoRedes();
void procesarEscaneoRedes();

void reanudarRecepcionLoRa(const char *motivo);
void LORA_bidireccional_borrar();
void recepcion_lora();
void procesarPaqueteLoRaRecibido(int packetSize);
void imprimirResumenLoRa(const char *motivo);
void imprimirDetalleParametrosLoRa();
String construirPayloadEEPROMParaMQTT(const ConfigDispositivo &config);


struct DatosConfirmacionLoRa;
DatosConfirmacionLoRa construirConfirmacionLoRa(const String &mensaje, const ConfigDispositivo &config, bool preferirEEPROM = false);


void actualizarDatosDesdeLoRa(const String &mac, const String &mensaje, const String &nombre);
bool actualizarLecturasParcialesDesdeLoRa(int indice, const String &mensaje);

// AGREGAR estas declaraciones:
int contarDispositivosRegistrados();
void dibujarHeader();
void dibujarTituloDispositivo();
void dibujarContenidoPrincipal();
bool guardarDispositivos();

void debugNombresDispositivos();

void verificarTiemposSinDatos();

void debugMensajeLoRa(const String &mensaje);
void testDispositivosRapido() ;
void activarDispositivosTrasConfirmacion();
void iniciarWaterDisplay();
void actualizarWaterDisplay(int porcentaje);
void dibujarNivelAguaVertical(int porcentaje, bool mostrarAgua);
int extraerEstatusLlenado(const String &payload);


//Definiciones pantalla TFT
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool displayReady = false;

// OLED secundario vertical (nivel de agua) - bus I2C independiente
static const uint8_t WATER_OLED_SDA = 15;
static const uint8_t WATER_OLED_SCL = 4;
static const uint8_t WATER_OLED_ADDR = 0x3C;
// Márgenes del tanque en el OLED vertical (ajustables para centrar el recuadro)
static const uint8_t WATER_MARGIN_LEFT = 18;
static const uint8_t WATER_MARGIN_RIGHT = 6;
static const uint8_t WATER_MARGIN_TOP = 8;
static const uint8_t WATER_MARGIN_BOTTOM = 6;
TwoWire waterWire = TwoWire(1);
Adafruit_SSD1306 waterDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &waterWire, OLED_RESET);
bool waterDisplayOk = false;
int waterDisplayUltimoPorcentaje = -1;
unsigned long waterDisplayUltimoBlink = 0;
bool waterDisplayBlinkOn = true;
bool waterDisplayLlenando = false;
int waterDisplayPorcentajeAnimado = 0;
unsigned long waterDisplayUltimaAnimacion = 0;

// Estructura para los dispositivos
struct Dispositivo {
  String nombre;
  int porcentaje;
  float bateria; // en volts
  int litros;
  bool sinDatos; // ⭐ NUEVO: indica si no hay datos recientes
};

// Variables de estado
bool wifiConectado = false;
int conteoReintentosWiFi = 0;

// Datos de ejemplo
Dispositivo dispositivos[] = {
  {"Solares 1", 80, 3.3, 1500},
  {"Villas 1", 60, 3.1, 6500},
  {"Principal", 45, 2.9, 3200}
};

int dispositivoActual = 0;
unsigned long ultimoCambio = 0;
unsigned long INTERVALO_CAMBIO = 3000;




// Variables para animación de emparejamiento
bool emparejando = false;
int frameAnimacion = 0;
unsigned long ultimoCambioAnimacion = 0;
const unsigned long INTERVALO_ANIMACION = 200; // ms entre frames

// Variables para controlar el estado de alta/baja
bool operacionCompletada = false;
String tipoOperacion = ""; // "ALTA" o "BAJA"
unsigned long tiempoFinOperacion = 0;
const unsigned long TIEMPO_MOSTRAR_OPERACION = 3000; // 3 segundos

// Bitmaps para iconos (16x16 pixels)
const unsigned char PROGMEM wifiIcon[] = {
  0b00000011, 0b11000000,
  0b00001100, 0b00110000,
  0b00110000, 0b00001100,
  0b01000000, 0b00000010,
  0b00000000, 0b00000000,
  0b00000011, 0b11000000,
  0b00001100, 0b00110000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b11000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000
};

const unsigned char PROGMEM wifiIconOff[] = {
  0b10000011, 0b11000001,  // Tache + WiFi
  0b11001100, 0b00110011,  // Tache + WiFi
  0b01110000, 0b00001110,  // Tache + WiFi
  0b00110000, 0b00001100,  // Tache + WiFi
  0b00011000, 0b00011000,  // Tache + WiFi
  0b00001111, 0b11110000,  // Tache + WiFi
  0b00000111, 0b11100000,  // Tache + WiFi
  0b00000011, 0b11000000,  // Tache + WiFi
  0b00000011, 0b11000000,  // Tache + WiFi
  0b00000111, 0b11100000,  // Tache + WiFi
  0b00001111, 0b11110000,  // Tache + WiFi
  0b00011000, 0b00011000,  // Tache + WiFi
  0b00110000, 0b00001100,  // Tache + WiFi
  0b01110000, 0b00001110,  // Tache + WiFi
  0b11001100, 0b00110011,  // Tache + WiFi
  0b10000011, 0b11000001   // Tache + WiFi
};

const unsigned char PROGMEM batteryFull[] = {
  0b00111111, 0b11111100,
  0b01100000, 0b00000110,
  0b11000000, 0b00000011,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b11000000, 0b00000011,
  0b01100000, 0b00000110,
  0b00111111, 0b11111100
};

const unsigned char PROGMEM batteryEmpty[] = {
  0b00111111, 0b11111100,
  0b01100000, 0b00000110,
  0b11000000, 0b00000011,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b11000000, 0b00000011,
  0b01100000, 0b00000110,
  0b00111111, 0b11111100
};


// Variables para animación de WiFi en TFT
bool animandoWifi = false;
int frameWifi = 0;
unsigned long ultimoCambioWifi = 0;
const unsigned long INTERVALO_WIFI = 300; // ms entre frames

// DECLARACIONES DE FUNCIONES TFT
void dibujarCirculoGiratorio(int centroX, int centroY, int radio, int angulo);
void mostrarEmparejamiento();
void iniciarEmparejamiento();
void detenerEmparejamiento();
void emparejarNuevoDispositivo();
void actualizarDatos(int index, int porcentaje, float bateria, int litros);
void setWifiStatus(bool conectado);

// También agregar estas declaraciones al principio TFT
void dibujarWifiAnimado(int centroX, int centroY, int frame);
void mostrarConexionWifi();
void mostrarWifiInicioTemporal();
void mostrarAvisoPortalAutomatico();
void iniciarAnimacionWifi();
void detenerAnimacionWifi();
void conectarWifi();
void testWiFiConnection();
void debugEEPROMReal();
void iniciarFlujoFactoryReset();
void manejarFlujoFactoryReset();
String horaLegibleCorta();
void mostrarMensajeFactory(const String &l1, const String &l2 = "", const String &l3 = "");

void verificarEstadoConfigDispositivos();

void debugEstadoDispositivos();

void debugNombreProblema();
void manejarBotonWifi();
void mostrarMensajeRedConectada(const String &ssid, bool conectado, const String &password = "", unsigned long retrasoMs = 0, unsigned long duracionMs = 5000);
void dibujarMensajeConexion();
String escapeForJS(const String &input);

// --- Pines para los botones---
#define BOTON_S 32
#define BOTON_W 33
#define TIEMPO_BOTON 1000

bool boton_s=false;
unsigned long tiempoInicioPresion = 0;
bool wifiButtonPressed = false;
unsigned long wifiButtonPressStart = 0;
bool wifiConfigInProgress = false;
bool mostrarMensajeConexion = false;
unsigned long inicioMensajeConexion = 0;
String ultimaRedConfigurada = "";
String ultimaContrasenaConfigurada = "";
bool conexionExitosa = false;
unsigned long retrasoMensajeConexion = 0;
unsigned long duracionMensajeConexion = 5000;

// Flujo guiado de reseteo de fábrica con baja MQTT
enum FactoryResetStage {
  FACTORY_IDLE = 0,
  FACTORY_BUSCANDO_WIFI,
  FACTORY_SOLICITANDO_BAJA,
  FACTORY_ESPERANDO_CONFIRMACION,
  FACTORY_FALLO_WIFI,
  FACTORY_CONFIRMADA
};

FactoryResetStage factoryResetStage = FACTORY_IDLE;
bool factoryResetEnProceso = false;
unsigned long factoryWifiDeadline = 0;
unsigned long factoryConfirmDeadline = 0;
unsigned long ultimoIntentoWifiFactory = 0;
String horaConfirmacionBaja = "";
const unsigned long FACTORY_WIFI_TIMEOUT = 120000;   // 2 minutos para reconectar
const unsigned long FACTORY_WIFI_RETRY = 5000;        // cada 5 segundos intentar
const unsigned long FACTORY_BAJA_TIMEOUT = 60000;     // 1 minuto esperando confirmación

//void manejarBoton_S();


// Variables BLE
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;


bool solicitudAltaBLE = false;
bool solicitudBajaBLE = false;

// Variables para mostrar detalles de la operación del ALTA o BAJA de un dispositivo  
String ultimoNombreDispositivo = "";
int ultimosLitros = 0;
int ultimaAltura = 0;
bool mostrarResultado = false;
unsigned long inicioResultado = 0;
const unsigned long TIEMPO_RESULTADO = 5000; // 5 segundos

// Basado en "NUUP" - más fácil de recordar
#define SERVICE_UUID        "4e555550-2024-1337-8001-123456789abc"
#define CHARACTERISTIC_UUID "4e555550-2024-1337-8002-123456789abc"

// Agrega esto con las otras variables globales BLE
String ultimaMacCliente = "";  // Para mantener la MAC entre mensajes
String macBajaEnCurso = "";    // Verificación extra de borrado

bool procesarBajaBLE(const String &macCliente) {
    Serial.println("\n🔄 ===== PROCESANDO BAJA BLE =====");
    Serial.println("📱 MAC recibida: '" + macCliente + "'");
    
    // ⭐⭐ SIEMPRE ACTIVAR LA SOLICITUD DE BAJA PARA LA ANIMACIÓN
    solicitudBajaBLE = true;
    
    // Resto del código igual...
    String macBuscada = macCliente;
    macBuscada.toUpperCase();
    macBuscada.trim();
    
    Serial.println("🔍 Buscando dispositivo...");
    
    bool encontrado = false;
    int indiceEncontrado = -1;
    
    // DEBUG: Mostrar todos los dispositivos registrados
    Serial.println("📋 DISPOSITIVOS REGISTRADOS:");
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            String macGuardada = String(configDispositivos[i].mac);
            macGuardada.trim();
            
            Serial.printf("   [%d] MAC: '%s' (long: %d)\n", 
                         i, macGuardada.c_str(), macGuardada.length());
            
            // Comparación case-insensitive
            if (macGuardada.equalsIgnoreCase(macBuscada)) {
                Serial.println("   ✅ COINCIDENCIA ENCONTRADA!");
                encontrado = true;
                indiceEncontrado = i;
                
                // Guardar datos para mostrar en pantalla
                ultimoNombreDispositivo = String(configDispositivos[i].nombre);
                if (ultimoNombreDispositivo.isEmpty()) {
                    ultimoNombreDispositivo = "Dispositivo"; // Nombre genérico para baja
                }
                ultimosLitros = configDispositivos[i].litrosActuales;
                ultimaAltura = configDispositivos[i].alturaConfig;
                break;
            }
        }
    }
    
    if (!encontrado) {
        Serial.println("❌ BAJA FALLIDA - Dispositivo no encontrado");
        Serial.println("💡 El sensor no está dado de alta en el servidor");

        // ⭐⭐ CONFIGURAR DATOS PARA MOSTRAR EN PANTALLA
        ultimoNombreDispositivo = "No Registrado";
        ultimosLitros = 0;
        ultimaAltura = 0;

        macBajaEnCurso = "";

        Serial.println("===== FIN BAJA BLE =====\n");
        return false;
    }
    
    macBajaEnCurso = macBuscada;

    // Proceder con la eliminación protegida por MQTT
    if (iniciarBajaDispositivo(configDispositivos[indiceEncontrado].mac, "BLE")) {
        Serial.println("✅ Baja solicitada para la MAC: " + macCliente);
        imprimirDispositivosRegistrados();

        Serial.println("✅ BAJA en curso (esperando confirmación MQTT si aplica)");
        Serial.println("===== FIN BAJA BLE =====\n");
        return true;
    }

    Serial.println("❌ ERROR en la solicitud de baja");
    Serial.println("===== FIN BAJA BLE =====\n");
    return false;
}


void intentarAltaTrasRegistro(int indice, const String &mac, const char* origen) {
    if (indice < 0 || indice >= MAX_DISPOSITIVOS) return;

    bool mqttConectado = WiFi.status() == WL_CONNECTED && client.connected();

    if (mqttConfirmed && mqttConectado) {
        Serial.printf("🚀 Alta MQTT solicitada tras %s para %s\n", origen, mac.c_str());
        solicitarAltaNuupMQTT(indice, mac);
        return;
    }

    Serial.printf("⏳ Alta pendiente (%s): monitor %s, MQTT %s\n",
                  origen,
                  mqttConfirmed ? "confirmado" : "sin confirmar",
                  mqttConectado ? "conectado" : "desconectado");

    solicitudAltaEnviada[indice] = false;
    ultimaSolicitudAlta[indice] = 0;
}


bool procesarRegistroBLE(const String &macCliente, const String &nombre = "", int altura = 0, int litros = 0) {

    String macNormalizada = normalizarMac(macCliente);
    if (!esMacValida(macNormalizada)) {
        Serial.println("❌ MAC inválida en registro BLE, se descarta");
        return false;
    }

    Serial.println("🔄 Procesando registro BLE COMPLETO:");
    Serial.println("   MAC: " + macNormalizada);
    Serial.println("   Nombre: " + nombre);
    Serial.println("   Altura: " + String(altura));
    Serial.println("   Litros: " + String(litros));
    
    // Guardar datos para mostrar en pantalla
    ultimoNombreDispositivo = nombre;
    ultimaAltura = altura;
    ultimosLitros = litros;

    // Buscar si ya existe el dispositivo
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (normalizarMac(String(configDispositivos[i].mac)) == macNormalizada) {
            Serial.println("⚠️  Dispositivo ya registrado - Actualizando datos");

            // ⭐⭐ ACTUALIZAR CORRECTAMENTE los campos
            configDispositivos[i].alturaConfig = altura;
            configDispositivos[i].litrosConfig = litros;
          configDispositivos[i].litrosActuales = 0; // Inicialmente vacío, se actualizará con LoRa
            nombre.toCharArray(configDispositivos[i].nombre, 20);
            configDispositivos[i].activo = false; // Se activará tras confirmación MQTT
            configDispositivos[i].porcentaje = 100; // Inicialmente al 100%
            solicitudAltaEnviada[i] = false;
            ultimaSolicitudAlta[i] = 0;
            limpiarEstadoBaja(i);
            limpiarBajaPendientePorMac(macNormalizada);  // Cancelar cualquier baja pendiente

            Serial.println("✅ Datos actualizados para dispositivo existente");
            
            if (guardarDispositivos()) {
                int indice = obtenerIndiceDispositivo(macNormalizada);
                intentarAltaTrasRegistro(indice, macNormalizada, "BLE (actualización)");
                solicitudAltaBLE = true;
                iniciarEmparejamiento();
                return true;
            } else {
                Serial.println("❌ Error al guardar en EEPROM");
                return false;
            }
        }
    }

    // Buscar espacio libre para nuevo registro
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == "") {
            // ⭐⭐ GUARDAR CORRECTAMENTE todos los campos
            macNormalizada.toCharArray(configDispositivos[i].mac, MAC_LEN + 1);
            nombre.toCharArray(configDispositivos[i].nombre, 20);
            Serial.println("📝 Nombre guardado: " + String(configDispositivos[i].nombre));
            Serial.println("📏 Altura guardada: " + String(configDispositivos[i].alturaConfig));
            Serial.println("💧 Litros guardados: " + String(configDispositivos[i].litrosConfig));
            Serial.println("📊 Porcentaje inicial: " + String(configDispositivos[i].porcentaje) + "%");
            
            if (guardarDispositivos()) {
                imprimirDispositivosRegistrados();
                int indice = obtenerIndiceDispositivo(macNormalizada);
                intentarAltaTrasRegistro(indice, macNormalizada, "BLE (nuevo)");
                solicitudAltaBLE = true;
                iniciarEmparejamiento();
                return true;
            } else {
                Serial.println("❌ Error al guardar en EEPROM");
                return false;
            }
        }
    }
    
    Serial.println("❌ No hay espacio para más dispositivos");
    return false;
}


// Función para verificar proximidad por RSSI (5cm o menos)
// Función estaCerca() SIMPLIFICADA - sin RSSI por ahora
bool estaCerca() {
    if (!deviceConnected) {
        return false;
    }
    
    // ⚠️ SOLUCIÓN TEMPORAL: Usar potencia BLE reducida en lugar de RSSI
    // La potencia ya está configurada en ESP_PWR_LVL_N12 (-12dBm)
    // Esto limita físicamente el alcance a ~5cm

    Serial.printf("✅ Verificación de proximidad por potencia BLE reducida (objetivo >= %d dBm si se activa RSSI)\n", RSSI_MIN_APAREAMIENTO_MONITOR);
    return true;
}

// ⭐ NUEVA FUNCIÓN: Procesar registro con datos completos

// Callbacks BLE
// Callbacks BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("📱 Dispositivo BLE conectado");
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("📱 Dispositivo BLE desconectado");
    }
};




class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {
        // Verificación simplificada de proximidad
        if (!estaCerca()) {
            Serial.println("🚫 Comando rechazado - Fuera de rango");
            pCharacteristic->setValue("ERROR:PROXIMIDAD");
            pCharacteristic->notify();
            return;
        }
        
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            String command = String(value.c_str());
            Serial.print("📨 Comando BLE recibido: ");
            Serial.println(command);
            
            if (command.startsWith("REG:")) {
                handleRegistration(pCharacteristic, command);
            }
            else if (command.startsWith("CONFIG,")) {
                // ⭐ CORREGIDO: El cliente envía CONFIG,nombre,altura,litros
                handleConfigWithData(pCharacteristic, command);
            }
            else if (command.startsWith("BAJA:")) {
                handleUnregistration(pCharacteristic, command);
            }
            else if (command == "conf") {
                // Manejo por si acaso todavía envía 'conf' solo
                Serial.println("✅ Confirmación 'conf' recibida (sin datos)");
                pCharacteristic->setValue("listo");
                pCharacteristic->notify();
                Serial.println("📤 Enviado: listo - Esperando CONFIG...");
            }
            else {
                String respuesta = "ERROR:COMANDO_DESCONOCIDO";
                pCharacteristic->setValue(respuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Comando no reconocido: " + command);
            }
          
     }
  }
      private:
    String ultimaMacCliente = "";
  
    void handleRegistration(BLECharacteristic *pCharacteristic, const String &command) {
        // ⭐ EXTRAER MAC DEL CLIENTE del comando REG:MAC_CLIENTE
        String macCliente = command.substring(4);
        macCliente.trim();
        
        Serial.println("🔵 Solicitud de REGISTRO recibida via BLE");
        Serial.println("📱 MAC del Cliente: " + macCliente);
        
        // Validar formato de MAC
        if (macCliente.length() != 17) {
            String errorRespuesta = "ERROR:MAC_INVALIDA";
            pCharacteristic->setValue(errorRespuesta.c_str());
            pCharacteristic->notify();
            Serial.println("❌ MAC del cliente inválida: " + macCliente);
            return;
        }
        
        // Guardar la MAC para el siguiente mensaje
        ultimaMacCliente = macCliente;
        
        // Obtener MAC del servidor
        String macServidor = WiFi.macAddress();
        macServidor.replace("-", ":");
        Serial.println("🖥️  MAC del Servidor: " + macServidor);
        
        // ⭐ PRIMERA PARTE: Enviar OK_REG con MAC del servidor
        String respuesta1 = "OK_REG," + macServidor;
        pCharacteristic->setValue(respuesta1.c_str());
        pCharacteristic->notify();
        Serial.print("📤 Parte 1 enviada: ");
        Serial.println(respuesta1);
        Serial.println("⏳ Esperando datos de configuración (CONFIG,nombre,altura,litros)...");
    }

    void handleConfigWithData(BLECharacteristic *pCharacteristic, const String &command) {
        // ⭐ CORREGIDO: Procesar CONFIG,nombre,altura,litros
        Serial.println("📥 Datos de configuración CONFIG recibidos");
        
        if (ultimaMacCliente.length() == 0) {
            String errorRespuesta = "ERROR:NO_HAY_REGISTRO_PREVIO";
            pCharacteristic->setValue(errorRespuesta.c_str());
            pCharacteristic->notify();
            Serial.println("❌ No hay registro previo (REG) para asociar los datos");
            return;
        }
        
        // Procesar: CONFIG,nombre,altura,litros
        String configData = command.substring(7); // Quitar "CONFIG,"
        configData.trim();
        
        Serial.println("📊 Datos crudos después de CONFIG,: " + configData);
        
        // Parsear: nombre,altura,litros
        int pos1 = configData.indexOf(',');
        int pos2 = configData.indexOf(',', pos1 + 1);
        
        if (pos1 != -1 && pos2 != -1) {
            String nombre = configData.substring(0, pos1);
            String alturaStr = configData.substring(pos1 + 1, pos2);
            String litrosStr = configData.substring(pos2 + 1);
            
            nombre.trim();
            alturaStr.trim();
            litrosStr.trim();
            
            Serial.printf("🎯 Datos parseados - Nombre: '%s', Altura: '%s', Litros: '%s'\n", 
                         nombre.c_str(), alturaStr.c_str(), litrosStr.c_str());
            
            // Validar datos
            if (nombre.length() == 0 || alturaStr.length() == 0 || litrosStr.length() == 0) {
                String errorRespuesta = "ERROR:DATOS_INCOMPLETOS";
                pCharacteristic->setValue(errorRespuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Datos de configuración incompletos");
                return;
            }
            
            // Convertir a números
            int altura = alturaStr.toInt();
            int litros = litrosStr.toInt();
            
            if (altura <= 0 || litros <= 0) {
                String errorRespuesta = "ERROR:DATOS_NUMERICOS_INVALIDOS";
                pCharacteristic->setValue(errorRespuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Datos numéricos inválidos (deben ser > 0)");
                return;
            }
            
            // ⭐ REGISTRAR CON LA MAC DEL CLIENTE
            Serial.println("🔄 Registrando dispositivo con MAC: " + ultimaMacCliente);
            bool registroExitoso = procesarRegistroBLE(ultimaMacCliente, nombre, altura, litros);
            
            if (registroExitoso) {
                // ⭐ ENVIAR READY de confirmación
                String respuestaFinal = "READY";
                pCharacteristic->setValue(respuestaFinal.c_str());
                pCharacteristic->notify();
                Serial.print("📤 Confirmación enviada: ");
                Serial.println(respuestaFinal);
                
                Serial.println("✅ REGISTRO completado exitosamente");
                Serial.println("💾 MAC Cliente: " + ultimaMacCliente);
                Serial.println("📝 Nombre: " + nombre);
                Serial.println("📏 Altura: " + String(altura) + " cm");
                Serial.println("💧 Litros: " + String(litros) + " L");
                
                solicitudAltaBLE = true;
                iniciarEmparejamiento();
                
                // Limpiar para el próximo registro
                ultimaMacCliente = "";
            } else {
                String errorRespuesta = "ERROR:REGISTRO_FALLIDO";
                pCharacteristic->setValue(errorRespuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Falló el registro en el servidor");
            }
        } else {
            String errorRespuesta = "ERROR:FORMATO_CONFIG_INVALIDO";
            pCharacteristic->setValue(errorRespuesta.c_str());
            pCharacteristic->notify();
            Serial.println("❌ Formato de CONFIG inválido");
            Serial.println("   Se esperaba: CONFIG,nombre,altura,litros");
            Serial.println("   Se recibió: " + command);
            Serial.printf("   pos1: %d, pos2: %d\n", pos1, pos2);
        }
    }
  

void handleUnregistration(BLECharacteristic *pCharacteristic, const String &command) {
    // ⭐ EXTRAER MAC DEL CLIENTE del comando BAJA:MAC_CLIENTE
    String macCliente = command.substring(5);
    macCliente.trim();
    
    Serial.println("🔴 Solicitud de BAJA recibida via BLE");
    Serial.println("📱 MAC del Cliente: " + macCliente);
    
    // Procesar baja
    bool bajaExitosa = procesarBajaBLE(macCliente);
    
    if (bajaExitosa) {
        String respuesta = "OK_BAJA";
        pCharacteristic->setValue(respuesta.c_str());
        pCharacteristic->notify();
        Serial.println("✅ BAJA exitosa via BLE");
        Serial.println("🗑️  MAC eliminada de EEPROM: " + macCliente);
        
        solicitudBajaBLE = true;
        iniciarEmparejamiento();
    } else {
        // ⭐⭐ CORREGIDO: Enviar "ERROR:NO_EXISTE_MAC" PERO ACTIVAR ANIMACIÓN
        String respuesta = "ERROR:NO_EXISTE_MAC";
        pCharacteristic->setValue(respuesta.c_str());
        pCharacteristic->notify();
        Serial.println("📤 Enviado al cliente: ERROR:NO_EXISTE_MAC");
        Serial.println("❌ BAJA fallida - MAC no existe: " + macCliente);
        
        // ⭐⭐ NUEVO: ACTIVAR ANIMACIÓN DE BAJA A PESAR DEL ERROR
        Serial.println("🎭 ACTIVANDO ANIMACIÓN DE PANTALLA PARA 'NO_EXISTE_MAC'");
        solicitudBajaBLE = true;  // ← ESTA LÍNEA ES CLAVE
        iniciarEmparejamiento();  // ← ESTA LÍNEA ACTIVA LA ANIMACIÓN
        
        // ⭐⭐ OPCIONAL: Configurar datos para mostrar en pantalla
        ultimoNombreDispositivo = "Dispositivo No Registrado";
        ultimosLitros = 0;
        ultimaAltura = 0;
    }
}

};

void iniciarBLE() {
    Serial.println("🔵 Iniciando BLE para 5cm de distancia...");

    BLEDevice::init("NUUP_Monitor");

    // ⚡ CONFIGURACIÓN CLAVE: Potencia de transmisión ajustable
    BLEDevice::setPower(POTENCIA_BLE_MONITOR); // Ajusta POTENCIA_BLE_MONITOR para acercar/alejar
    Serial.printf("📡 Potencia BLE del monitor: nivel %d (incrementa para más alcance, reduce para más cercanía)\n",
                  POTENCIA_BLE_MONITOR);

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pCharacteristic->setValue("NUUP Ready");
    
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    
    BLEDevice::startAdvertising();
    Serial.println("🔵 BLE iniciado - Alcance limitado a ~5cm");
}

void manejarBLE() {
    // Notificar cambio de estado de conexión
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("🔵 BLE publicidad reiniciada");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
}

void mostrarResultadoOperacion() {
    display.clearDisplay();
    
    // Texto grande ALTA/BAJA
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    
    if (solicitudAltaBLE) {
        display.setCursor(SCREEN_WIDTH/2 - 36, 5);
        display.print("ALTA");
    } else if (solicitudBajaBLE) {
        display.setCursor(SCREEN_WIDTH/2 - 36, 5);
        display.print("BAJA");
    }
    
    // Detalles más pequeños
    display.setTextSize(1);
    
    // Mostrar nombre si está disponible
    if (ultimoNombreDispositivo.length() > 0) {
        display.setCursor(0, 30);
        display.print("Nombre: ");
        display.print(ultimoNombreDispositivo);
    }
    
    // Mostrar litros si está disponible
    if (ultimosLitros > 0) {
        display.setCursor(0, 40);
        display.print("Litros: ");
        display.print(ultimosLitros);
        display.print(" L");
    }
    
    // Mostrar altura si está disponible
    if (ultimaAltura > 0) {
        display.setCursor(0, 50);
        display.print("Altura: ");
        display.print(ultimaAltura);
        display.print(" cm");
    }
    
    display.display();
}

void mostrarMensajeBajaFinal() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // Etiqueta de baja
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("BAJA");

    // Nombre del dispositivo en grande (recortado para caber)
    String nombre = ultimoNombreDispositivo;
    if (nombre.length() > 10) {
        nombre = nombre.substring(0, 10);
    }
    display.setTextSize(2);
    display.setCursor(0, 24);
    display.println("DISPOSITIVO");
    display.setCursor(0, 44);
    display.println(nombre);

    display.display();
}

void mostrarMensajeReinicioBaja() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.println("Reiniciando");
    display.setCursor(0, 40);
    display.println("dispositivo");
    display.display();
}

void testLoRaPeriodico() {
    static unsigned long lastTest = 0;
    if (millis() - lastTest > 30000) { // Cada 30 segundos
        lastTest = millis();

        Serial.println("\n🔧 TEST PERIÓDICO LoRa:");
        Serial.printf("   - Free Heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("   - Paquetes recibidos: %s\n", nuevoMensajeLoRa ? "SI" : "NO");

        // Test de envío
        LoRa.beginPacket();
        LoRa.print("SERVER_ALIVE_" + String(millis()));
        LoRa.endPacket();
        Serial.println("   - Mensaje test enviado");

        // Reanudar escucha inmediata para no bloquear confirmaciones reales
        reanudarRecepcionLoRa("testLoRaPeriodico");
    }
}



void verificarEstadoConfigDispositivos() {
    Serial.println("\n=== VERIFICACIÓN configDispositivos[] ===");
    Serial.printf("Tamaño del arreglo: %d\n", MAX_DISPOSITIVOS);
    Serial.printf("Dirección del arreglo: %p\n", (void*)configDispositivos);
    
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("✅ [%d] MAC: '%s' (longitud: %d)\n", 
                         i, configDispositivos[i].mac, strlen(configDispositivos[i].mac));
        } else {
            Serial.printf("❌ [%d] VACÍO\n", i);
        }
    }
    Serial.println("=========================================\n");
}


void testWiFiConnection() {
  Serial.println("\n=== PRUEBA MANUAL DE WIFI ===");
  
  // Lista de contraseñas comunes para probar
  String testPasswords[] = {
    "JNWWM7KHzE",  // Reemplaza con la real
    "INFINITUM0EBB",
    "admin",
    "password",
    "12345678",
    ""
  };
  
  String ssid = "INFINITUM0EBB_2.4";
  
  for (int i = 0; i < 6; i++) {
    if (testPasswords[i].length() > 0) {
      Serial.print("Probando contraseña: '");
      Serial.print(testPasswords[i]);
      Serial.println("'");
      
      WiFi.begin(ssid.c_str(), testPasswords[i].c_str());
      
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ CONTRASEÑA CORRECTA: " + testPasswords[i]);
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        return;
      } else {
        Serial.println("\n✗ Falló con esta contraseña");
        checkWiFiStatus();
      }
    }
  }
  Serial.println("=== FIN PRUEBA MANUAL ===");
}

void manejarBotonWifi() {
  bool presionado = digitalRead(BOTON_W) == LOW;

  if (presionado && !wifiButtonPressed) {
    wifiButtonPressed = true;
    wifiButtonPressStart = millis();
  }

  if (!presionado && wifiButtonPressed) {
    wifiButtonPressed = false;
    wifiButtonPressStart = 0;
  }

  if (wifiButtonPressed && (millis() - wifiButtonPressStart >= TIEMPO_BOTON)) {
    wifiButtonPressed = false;
    wifiButtonPressStart = 0;

    if (forceAPMode) {
      Serial.println("Presionando boton WiFi - cancelando portal y regresando a modo normal...");
      detenerConfiguracionWiFi();
    } else {
      Serial.println("Presionando boton WiFi - reiniciando modo AP...");
      reiniciarConfiguracionWiFi();
    }
  }
}



// Implementación de funciones

void reiniciarConfiguracionWiFi() {
  mostrarMensajeConexion = false;
  inicioMensajeConexion = 0;
  conexionExitosa = false;
  wifiConfigInProgress = true;
  portalEnUso = false;
  portalPantallaFija = false;
  reinicioSolicitado = false;
  reinicioProgramado = 0;
  forceAPMode = true;
  apMode = true;
  iniciarAnimacionWifi();
  mostrarConexionWifi();
  startAPMode();
}

void detenerConfiguracionWiFi() {
  mostrarMensajeConexion = false;
  inicioMensajeConexion = 0;
  conexionExitosa = false;
  wifiConfigInProgress = false;
  portalEnUso = false;
  portalPantallaFija = false;
  reinicioSolicitado = false;
  forceAPMode = false;
  apMode = false;
  detenerAnimacionWifi();
  scanInProgress = false;
  scannedNetworksCache = "";
  WiFi.scanDelete();

  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void configurarAlcanceWiFiAPMonitor(int metros) {
  alcanceWiFiAPMetrosMonitor = metros;

  switch (metros) {
    case 1:
      potenciaWiFiAPMonitor = WIFI_POWER_2dBm;
      potenciaTxWiFiAPMonitor = 2;
      break;
    case 2:
      potenciaWiFiAPMonitor = WIFI_POWER_5dBm;
      potenciaTxWiFiAPMonitor = 5;
      break;
    case 10:
      potenciaWiFiAPMonitor = WIFI_POWER_17dBm;
      potenciaTxWiFiAPMonitor = 17;
      break;
    case 5:
    default:
      potenciaWiFiAPMonitor = WIFI_POWER_11dBm;
      potenciaTxWiFiAPMonitor = 11;
      break;
  }

  WiFi.setTxPower(potenciaWiFiAPMonitor);
  Serial.printf("📶 AP Monitor01 - Alcance estimado: %d m | Potencia TX: %d dBm. Ajusta alcanceWiFiAPMetrosMonitor para cambiarlo.\n",
                alcanceWiFiAPMetrosMonitor,
                potenciaTxWiFiAPMonitor);
}

void registrarRutasPortal() {
  server.on("/", HTTP_ANY, handleRoot);
  server.on("/save", HTTP_POST, handleSaveCredentials);
  server.on("/finalizar", HTTP_POST, handleFinalizeConfig);
  server.on("/delete", HTTP_POST, handleDeleteNetwork);
  server.on("/select", HTTP_POST, handleSelectNetwork);
  server.on("/delete_device", HTTP_POST, handleDeleteDevice);
  server.on("/setid", HTTP_POST, handleSetID);
  server.on("/factory_reset", HTTP_POST, handleFactoryReset);

  // Captura peticiones típicas de detección de portal cautivo
  server.on("/generate_204", HTTP_ANY, handleRoot);
  server.on("/hotspot-detect.html", HTTP_ANY, handleRoot);
  server.on("/ncsi.txt", HTTP_ANY, handleRoot);
  server.on("/connecttest.txt", HTTP_ANY, handleRoot);
  server.on("/fwlink", HTTP_ANY, handleRoot);
  server.on("/favicon.ico", HTTP_ANY, handleRoot);

  server.onNotFound(handleRoot);
}

void startAPMode() {
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  configurarAlcanceWiFiAPMonitor(alcanceWiFiAPMetrosMonitor);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  apMode = true;
  wifiConfigInProgress = true;
  forceAPMode = true;
  portalEnUso = false;
  portalPantallaFija = false;
  reinicioSolicitado = false;
  reinicioProgramado = 0;
  mostrarMensajeConexion = false;

  registrarRutasPortal();
  server.begin();

  scannedNetworksCache = "";
  lastNetworkScan = 0;
  scanInProgress = false;
  iniciarEscaneoRedes();

  // Atender inmediatamente las primeras peticiones del portal (probes) para evitar timeouts
  unsigned long inicioAtencion = millis();
  while (millis() - inicioAtencion < 1500) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(5);
  }

  Serial.println("\nModo AP activado");
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());

}

void mostrarMensajeRedConectada(const String &ssid, bool conectado, const String &password, unsigned long retrasoMs, unsigned long duracionMs) {
  ultimaRedConfigurada = ssid;
  ultimaContrasenaConfigurada = password;
  conexionExitosa = conectado;
  inicioMensajeConexion = millis();
  retrasoMensajeConexion = retrasoMs;
  duracionMensajeConexion = duracionMs;
  mostrarMensajeConexion = true;
  detenerAnimacionWifi();
  animandoWifi = false;
  frameWifi = 0;
  ultimoCambioWifi = millis();
  Serial.printf("\n📶 %s a la red '%s'\n", conectado ? "Conectado" : "Fallo de conexión", ssid.c_str());
  if (retrasoMensajeConexion == 0) {
    dibujarMensajeConexion();
  }
}

String escapeForJS(const String &input) {
  String out = input;
  out.replace("\\", "\\\\");
  out.replace("'", "\\'");
  out.replace("\"", "\\\"");
  out.replace("\n", " ");
  out.replace("\r", " ");
  return out;
}

String escapeForHTMLAttr(const String &input) {
  String out = input;
  out.replace("&", "&amp;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  return out;
}

void iniciarEscaneoRedes() {
  if (scanInProgress) return;

  scanInProgress = true;
  lastNetworkScan = millis();
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // escaneo asíncrono
}

void procesarEscaneoRedes() {
  if (!scanInProgress) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    return;  // sigue en progreso
  }

  scanInProgress = false;
  lastNetworkScan = millis();

  if (n <= 0) {
    scannedNetworksCache = "<p>No se encontraron redes cercanas. Intenta nuevamente.</p>";
    WiFi.scanDelete();
    return;
  }

  const int MAX_SCAN_RESULTS = 3;
  int limitedCount = n > MAX_SCAN_RESULTS ? MAX_SCAN_RESULTS : n;
  int *indices = new int[limitedCount];
  for (int i = 0; i < limitedCount; i++) {
    indices[i] = i;
  }

  for (int i = 0; i < limitedCount - 1; i++) {
    for (int j = i + 1; j < limitedCount; j++) {
      if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
      }
    }
  }

  String scannedNetworks = "";
  for (int i = 0; i < limitedCount; i++) {
    int idx = indices[i];
    String ssid = WiFi.SSID(idx);
    int rssi = WiFi.RSSI(idx);
    String safeSsidAttr = escapeForHTMLAttr(ssid);

    scannedNetworks += "<div class='network-item'>";
    scannedNetworks += "<label>" + ssid + "</label>";
    scannedNetworks += "<span class='signal'>" + String(rssi) + " dBm</span>";
    scannedNetworks += "<button class='use-button' data-ssid='" + safeSsidAttr + "' type='button' onclick=\"prefillNetwork(this.dataset.ssid, this)\">Usar</button>";
    scannedNetworks += "</div>";
  }

  scannedNetworksCache = scannedNetworks;
  delete[] indices;
  WiFi.scanDelete();
}

String getCheckedStatus(bool active) {
  return active ? " checked" : "";
}

void handleRoot() {

  // Al servir la página, congela la animación y dedica el ciclo al portal
  portalEnUso = !reinicioSolicitado;
  // Mantener bandera de configuración activa mientras el portal esté en uso
  wifiConfigInProgress = true;
  forceAPMode = true;
  apMode = true;
  if (!reinicioSolicitado) {
    reinicioProgramado = 0;
  }
  mostrarMensajeConexion = false;
  if (animandoWifi) {
    detenerAnimacionWifi();
  }
  if (!portalPantallaFija) {
    mostrarConexionWifi();
    portalPantallaFija = true;
  }

  loadUserProfileFromEEPROM();
  loadNetworksFromEEPROM();

  // Mantener el formulario vacío al cargar la página; se llenará al elegir “Modificar” o “Usar”
  String selectedSsid = "";
  String selectedPass = "";
  String selectedSsidEscaped = escapeForHTMLAttr(selectedSsid);
  String selectedSsidJs = escapeForJS(selectedSsid);
  String selectedPassEscaped = escapeForHTMLAttr(selectedPass);

  String ssidOptions = "<datalist id='ssidOptions'>";
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      ssidOptions += "<option value='" + escapeForHTMLAttr(savedNetworks[i].ssid) + "'></option>";
    }
  }
  ssidOptions += "</datalist>";

  String networksList = "";
  for(int i = 0; i < MAX_NETWORKS; i++) {
    if(savedNetworks[i].ssid.length() > 0) {
      String safeSsidAttr = escapeForHTMLAttr(savedNetworks[i].ssid);
      String safePassAttr = escapeForHTMLAttr(savedNetworks[i].password);
      networksList += "<div class='network-item'>";
      networksList += "<div class='network-info'><strong>" + safeSsidAttr + "</strong><div class='password-label'>Contraseña: " + safePassAttr + "</div></div>";
      networksList += "<div class='network-actions'>";
      networksList += "<button class='use-button' data-ssid='" + safeSsidAttr + "' data-pass='" + safePassAttr + "' type='button' onclick=\"editNetwork(" + String(i) + ", this)\">Modificar</button>";
      networksList += "<button type='button' onclick='deleteNetwork(" + String(i) + ")'>Borrar</button>";
      networksList += "</div>";
      networksList += "</div>";
    }
  }

  // Actualizar escaneo en segundo plano para no bloquear la carga
  procesarEscaneoRedes();
  if (!scanInProgress && scannedNetworksCache.isEmpty()) {
    iniciarEscaneoRedes();
  }

  String scannedNetworks = scannedNetworksCache;

  String devicesList = "";
  int deviceCount = 0;
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    String mac = String(configDispositivos[i].mac);
    mac.trim();
    if (mac.length() > 0) {
      deviceCount++;
      String nombre = String(configDispositivos[i].nombre);
      nombre.trim();
      if (nombre.length() == 0) {
        nombre = "Dispositivo";
      }
      devicesList += "<div class='network-item device-item'>";
      devicesList += "<div class='device-info'><strong>" + nombre + "</strong><br><small>MAC: " + mac + "</small></div>";
      devicesList += "<label class='device-select'><input type='checkbox' name='baja_mac' value='" + escapeForHTMLAttr(mac) + "'> Seleccionar para dar de baja</label>";
      devicesList += "</div>";
    }
  }

  if (deviceCount == 0) {
    devicesList = "<p>No hay dispositivos dados de alta.</p>";
  }


  bool usuarioConfirmado = userID.length() > 0;
  String correoAttr = userEmail.length() > 0 ? " value='" + escapeForHTMLAttr(userEmail) + "'" : "";
  String nombreAttr = userNombre.length() > 0 ? " value='" + escapeForHTMLAttr(userNombre) + "'" : "";
  String telefonoAttr = userTelefono.length() > 0 ? " value='" + escapeForHTMLAttr(userTelefono) + "'" : "";
  String passAttr = ""; // No prellenar password por seguridad

  String userSection = "<div class='network-list'>";
  userSection += "<h3 class='section-title'>Usuario NUUP: su correo</h3>";
  if (usuarioConfirmado) {
    String resumenCorreo = userEmail.length() > 0 ? userEmail : "(sin correo)";
    userSection += "<p>" + escapeForHTMLAttr(resumenCorreo) + "</p>";
  } else {
    String checked = userFlagRegistrado ? " checked" : "";
    String newUserFieldsStyle = userFlagRegistrado ? " style='display:none;'" : "";
    String extraDisabled = userFlagRegistrado ? " disabled" : "";
    String extraRequired = userFlagRegistrado ? "" : " required";
    userSection += "<label class='combo-label'><input type='checkbox' id='userRegistered' name='user_registered' value='1'" + checked + " onchange=\"toggleUserFields()\"> Ya estoy registrado</label>";
    userSection += "<label for='correoInput'>Correo</label>";
    userSection += "<input id='correoInput' type='email' name='correo' placeholder='correo@ejemplo.com' required" + correoAttr + ">";
    userSection += "<div id='newUserFields' class='user-extra-fields'" + newUserFieldsStyle + ">";
    userSection += "  <label for='nombreInput'>Nombre</label>";
    userSection += "  <input id='nombreInput' type='text' name='nombre' placeholder='Nombre completo'" + nombreAttr + extraDisabled + extraRequired + ">";
    userSection += "  <label for='telefonoInput'>Teléfono</label>";
    userSection += "  <input id='telefonoInput' type='tel' name='telefono' placeholder='Teléfono'" + telefonoAttr + extraDisabled + extraRequired + ">";
    userSection += "  <label for='passUserInput'>Password</label>";
    userSection += "  <input id='passUserInput' type='password' name='pass_usuario' placeholder='Contraseña'" + passAttr + extraDisabled + extraRequired + ">";
    userSection += "</div>";
    userSection += "<hr>";
  }
  userSection += "</div>";

  String actionsSection = "<div class='network-list actions-panel'>";
  actionsSection += "<h3 class='section-title'>Acciones del monitor</h3>";
  actionsSection += "<div class='action-card alert-card'>";
  actionsSection += "  <div class='action-text'><div class='icon-badge'>♻️</div><div><strong>Reseteo de fábrica</strong><br><small>Solicita la baja, borra la EEPROM y reinicia.</small></div></div>";
  actionsSection += "  <button type='button' class='danger-btn' onclick=\"factoryReset()\">Reseteo de fábrica</button>";
  actionsSection += "</div>";
  actionsSection += "</div>";



  String html = R"=====(
<!DOCTYPE html>
<html>
  <head>
  <meta charset='UTF-8'>
  <title>Configuración WiFi - NUUP</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body {
      background-color: #121212;
      color: #FFD700;
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 20px;
      min-height: 100vh;
      display: block;
    }
    .container {
      background-color: #1E1E1E;
      border: 2px solid #FFD700;
      border-radius: 10px;
      padding: 30px;
      width: 90%;
      max-width: 500px;
      box-shadow: 0 0 20px rgba(255, 215, 0, 0.3);
    }
    h1 {
      color: #FFD700;
      text-align: center;
      margin-bottom: 25px;
      font-size: 24px;
    }
    .device-title {
      color: #FFD700;
      text-align: center;
      font-size: 18px;
      margin-bottom: 5px;
      font-weight: bold;
    }
    form {
      display: flex;
      flex-direction: column;
    }
    input {
      background-color: #333;
      color: #FFD700;
      border: 2px solid #FFD700;
      border-radius: 5px;
      padding: 12px;
      margin-bottom: 15px;
      font-size: 16px;
    }
    input.locked {
      background-color: #222;
      color: #bbb;
    }
    input:focus {
      outline: none;
      border-color: #FFA500;
      box-shadow: 0 0 5px rgba(255, 215, 0, 0.5);
    }
    button {
      background-color: #FFD700;
      color: #121212;
      border: none;
      border-radius: 5px;
      padding: 12px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      transition: background-color 0.3s, border-color 0.3s, color 0.3s;
      margin: 5px 0;
    }
    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    button:hover {
      background-color: #FFA500;
    }
    button.use-button {
      background-color: rgba(255, 215, 0, 0.28);
      color: #FFD700;
      border: 1px solid #FFD700;
    }
    button.use-button:hover {
      background-color: rgba(255, 215, 0, 0.45);
    }
    .use-button.selected {
      background-color: #2ecc71;
      color: #121212;
      border-color: #2ecc71;
    }
    .network-list {
      margin: 20px 0;
    }
    .user-extra-fields {
      border: 1px dashed #FFD700;
      padding: 10px;
      border-radius: 6px;
      margin-top: 10px;
    }
    .combo-label {
      display: flex;
      gap: 8px;
      align-items: center;
      margin-bottom: 10px;
    }
    .factory-reset {
      background-color: #2b2b2b;
      color: #FFD700;
      border: 1px solid #FFD700;
      font-size: 12px;
      padding: 6px 8px;
      width: fit-content;
      margin-left: auto;
      margin-right: auto;
    }
    .network-item {
      display: flex;
      align-items: center;
      margin: 10px 0;
      padding: 10px;
      background-color: #333;
      border-radius: 5px;
    }
    .network-info {
      flex-grow: 1;
    }
    .network-info .password-label {
      font-size: 11px;
      color: #FFD700;
    }
    .network-actions {
      display: flex;
      flex-direction: column;
      gap: 8px;
      margin-left: auto;
      align-items: flex-end;
    }
    .network-item label {
      flex-grow: 1;
      margin-left: 10px;
    }
    .network-item .signal {
      margin-left: 10px;
      margin-right: 10px;
      font-size: 14px;
    }
    .network-item button {
      padding: 8px 12px;
      background-color: #ff3333;
    }
    .network-item button:hover {
      background-color: #cc0000;
    }
    .selected-ssid {
      background-color: #333;
      border: 1px solid #FFD700;
      border-radius: 6px;
      padding: 10px;
      margin-bottom: 10px;
      font-weight: bold;
    }
    .device-item {
      justify-content: space-between;
      gap: 10px;
    }
    .device-select {
      display: flex;
      align-items: center;
      gap: 8px;
      color: #FFD700;
      font-size: 14px;
    }
    .device-select input {
      width: auto;
      margin: 0;
    }
    .device-info {
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .section-title {
      margin-top: 25px;
      margin-bottom: 10px;
      font-size: 18px;
      color: #FFD700;
    }
    .alert {
      background-color: #331f00;
      border: 1px solid #FFD700;
      border-radius: 6px;
      padding: 10px;
      margin: 10px 0;
      font-weight: bold;
    }
    .hint {
      margin: 0 0 10px 0;
      color: #d8c16a;
      font-size: 13px;
    }
    .actions-panel {
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .action-card {
      background-color: #222;
      border: 1px solid #FFD700;
      border-radius: 8px;
      padding: 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
    }
    .action-text {
      display: flex;
      gap: 10px;
      align-items: center;
    }
    .icon-badge {
      width: 34px;
      height: 34px;
      border-radius: 50%;
      background-color: #FFD700;
      color: #121212;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 18px;
      box-shadow: 0 0 10px rgba(255, 215, 0, 0.35);
    }
    .action-card small { color: #d8c16a; }
    .alert-card { border-color: #ff7f50; box-shadow: 0 0 10px rgba(255, 127, 80, 0.35); }
    .neutral-card { border-color: #6ac6ff; box-shadow: 0 0 10px rgba(106, 198, 255, 0.25); }
    .danger-btn {
      background-color: #ff3333;
      color: #fff;
      border: none;
      padding: 10px 14px;
      border-radius: 6px;
      font-weight: bold;
    }
    .danger-btn:hover { background-color: #cc0000; }
    .secondary-btn {
      background-color: #FFD700;
      color: #121212;
      border: 1px solid #FFD700;
      padding: 10px 14px;
      border-radius: 6px;
      font-weight: bold;
    }
    .secondary-btn:hover { background-color: #ffeb7a; }
    ::placeholder {
      color: #888;
      opacity: 1;
    }
  </style>
  <script>
    function deleteNetwork(index) {
      if (confirm('¿Borrar esta red WiFi?')) {
        fetch('/delete', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'index=' + index })
          .then(response => { if (response.ok) location.reload(); });
      }
    }

    function toggleUserFields() {
      const checkbox = document.getElementById('userRegistered');
      const extra = document.getElementById('newUserFields');
      const hideExtras = checkbox && checkbox.checked;
      if (extra) {
        extra.style.display = hideExtras ? 'none' : 'block';
      }
      ['nombreInput', 'telefonoInput', 'passUserInput'].forEach(id => {
        const el = document.getElementById(id);
        if (el) {
          el.required = !hideExtras;
          el.disabled = hideExtras;
          if (hideExtras) {
            el.value = '';
          }
          el.classList.toggle('locked', hideExtras);
        }
      });
    }

    document.addEventListener('DOMContentLoaded', () => {
      toggleUserFields();
    });

    function factoryReset() {
      const mensaje = 'Se solicitará la baja, se borrará la EEPROM y el monitor se reiniciará. ¿Deseas continuar?';
      if (!confirm(mensaje)) return;
      fetch('/factory_reset', { method: 'POST' })
        .then(resp => resp.text().then(text => ({ ok: resp.ok, text })))
        .then(result => {
          alert(result.text || 'Reinicio de fábrica solicitado. El equipo se reiniciará.');
          setTimeout(() => location.reload(), 1200);
        })
        .catch(() => alert('No se pudo solicitar el reinicio de fábrica.'));
    }

    function validateUserForm(event) {
      if (typeof usuarioConfirmadoPortal !== 'undefined' && usuarioConfirmadoPortal) {
        return true;
      }
      const checkbox = document.getElementById('userRegistered');
      const registered = checkbox && checkbox.checked;
      const correo = document.getElementById('correoInput');
      const nombre = document.getElementById('nombreInput');
      const telefono = document.getElementById('telefonoInput');
      const pass = document.getElementById('passUserInput');

      if (correo && correo.value.trim().length === 0) {
        alert('Captura el correo del usuario.');
        event.preventDefault();
        return false;
      }

      if (!registered) {
        if (!nombre || !telefono || !pass ||
            nombre.value.trim().length === 0 ||
            telefono.value.trim().length === 0 ||
            pass.value.trim().length === 0) {
          alert('Completa nombre, teléfono y password para dar de alta al usuario.');
          event.preventDefault();
          return false;
        }
      }
      return true;
    }

    let ssidLocked = false;

    function clearSelections() {
      document.querySelectorAll('.use-button').forEach(btn => btn.classList.remove('selected'));
    }

    function setSsidLock(locked) {
      const ssidInput = document.getElementById('ssidInput');
      ssidLocked = locked;
      if (ssidInput) {
        ssidInput.readOnly = locked;
        ssidInput.classList.toggle('locked', locked);
      }
    }

    function updateSelected(ssid, lockField = false) {
      const ssidInput = document.getElementById('ssidInput');
      clearSelections();
      document.querySelectorAll('.use-button').forEach(btn => {
        if (btn.dataset && btn.dataset.ssid === ssid) {
          btn.classList.add('selected');
        }
      });
      if (ssidInput) {
        ssidInput.value = ssid || '';
        setSsidLock(lockField && ssid && ssid.length > 0);
      }
    }

    function handleManualSsidInput() {
      const ssidInput = document.getElementById('ssidInput');
      if (!ssidInput) return;
      if (ssidLocked) {
        setSsidLock(false);
      }
      clearSelections();
    }

    function prefillNetwork(ssid, btn) {
      const chosenSsid = (btn && btn.dataset && btn.dataset.ssid) ? btn.dataset.ssid : ssid;
      const passInput = document.getElementById('passInput');
      const editIndex = document.getElementById('editIndex');
      if (passInput) {
        passInput.value = '';
        passInput.focus();
      }
      if (editIndex) {
        editIndex.value = '';
      }
      updateSelected(chosenSsid, false);
      window.scrollTo({ top: 0, behavior: 'smooth' });
    }

    function editNetwork(idx, btn) {
      if (!btn || !btn.dataset) return;
      const chosenSsid = btn.dataset.ssid || '';
      const chosenPass = btn.dataset.pass || '';
      const passInput = document.getElementById('passInput');
      const editIndex = document.getElementById('editIndex');
      if (passInput && editIndex) {
        passInput.value = chosenPass;
        editIndex.value = idx;
        passInput.focus();
      }
      updateSelected(chosenSsid, false);
      window.scrollTo({ top: 0, behavior: 'smooth' });
    }
  </script>
</head>
<body>
  <div class="container">
    <div class="device-title">Dispositivo NUUP</div>
    <h1>Configurar WiFi</h1>
)=====";

  html += "<script>const usuarioConfirmadoPortal = " + String(usuarioConfirmado ? "true" : "false") + "; document.addEventListener('DOMContentLoaded', () => { const initialSsid = \"" + selectedSsidJs + "\"; updateSelected(initialSsid, false); const ssidInput = document.getElementById('ssidInput'); if (ssidInput) { ssidInput.addEventListener('input', handleManualSsidInput); ssidInput.addEventListener('focus', handleManualSsidInput); } const userToggle = document.getElementById('userRegistered'); if (userToggle) { userToggle.addEventListener('change', toggleUserFields); } const configForm = document.getElementById('configForm'); if (configForm) { configForm.addEventListener('submit', validateUserForm); } toggleUserFields(); });</script>";


  html += "<form id='configForm' action='/finalizar' method='POST' novalidate>";
  html += userSection;
  html += R"=====(
    <div class="network-list">
      <h3 class="section-title">Redes guardadas:</h3>
)=====";
  html += networksList;
  html += R"=====(
    </div>

    <div class="network-list">
      <h3 class="section-title">Redes cercanas:</h3>
)=====";
  html += scannedNetworks;
  html += R"=====(
    </div>

    <h3 class="section-title">Red a configurar:</h3>
    <input type='hidden' id='editIndex' name='index' value=''>
)=====";
    html += "    <label for='ssidInput'>Nombre de la red (SSID)</label>";
    html += "    <input id='ssidInput' type='text' list='ssidOptions' name='ssid' placeholder='Nombre de la red (SSID)' value='" + selectedSsidEscaped + "' oninput='handleManualSsidInput()' onfocus='handleManualSsidInput()'>";
    html += ssidOptions;
  html += "    <label for='passInput'>Contraseña</label>";
  html += "    <input id='passInput' type='text' name='pass' placeholder='Contraseña (visible para editar)' value='" + selectedPassEscaped + "'>";
  html += R"=====(
    <div class="network-list">
      <h3 class="section-title">Dispositivos registrados:</h3>
  )=====";
  html += "<p><strong>Selecciona los que desees dar de baja y guarda configuración.</strong></p>";
  html += devicesList;
  html += R"=====(
    </div>

    <div class="network-list">
      <button type='submit'>Configurar y reiniciar</button>
    </div>
  </form>
  )=====";

  html += actionsSection;

  html += R"=====(
    </div>

  </body>
  </html>
  )=====";

  server.send(200, "text/html", html);
}

void handleSaveCredentials() {
  if (userID.isEmpty() && userEmail.isEmpty()) {
    server.send(400, "text/plain", "Configura el usuario (correo) antes de guardar redes");
    return;
  }
  if(server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();

    if (ssid.length() == 0) {
      server.send(400, "text/plain", "SSID vacío");
      return;
    }
    int requestedIndex = -1;
    if (server.hasArg("index")) {
      String idxStr = server.arg("index");
      idxStr.trim();
      if (idxStr.length() > 0) {
        requestedIndex = idxStr.toInt();
      }
    }

    // Usar índice solicitado para editar, sobrescribir duplicados o buscar espacio libre
    int indexToSave = (requestedIndex >= 0 && requestedIndex < MAX_NETWORKS) ? requestedIndex : -1;
    if (indexToSave == -1) {
      for (int i = 0; i < MAX_NETWORKS; i++) {
        if (savedNetworks[i].ssid == ssid) {
          indexToSave = i;
          break;
        }
      }
    }
    if (indexToSave == -1) {
      for(int i = 0; i < MAX_NETWORKS; i++) {
        if(savedNetworks[i].ssid.length() == 0) {
          indexToSave = i;
          break;
        }
      }
    }

    // Si no hay espacio, reemplazar la primera (podría mejorarse con timestamp)
    if(indexToSave == -1) {
      indexToSave = 0;
    }

    savedNetworks[indexToSave].ssid = ssid;
    savedNetworks[indexToSave].password = pass;
    savedNetworks[indexToSave].active = true;
    
    // Desactivar las demás
    for(int i = 0; i < MAX_NETWORKS; i++) {
      if(i != indexToSave) {
        savedNetworks[i].active = false;
      }
    }

    if (!saveNetworksToEEPROM()) {
      Serial.println("❌ Error al guardar redes en EEPROM");
      server.send(500, "text/plain", "No se pudo guardar la red");
      return;
    }
    ultimaRedConfigurada = ssid;
    ultimaContrasenaConfigurada = pass;
    server.send(200, "text/html", "<html><body><h2>Credenciales guardadas. El equipo reiniciará para aplicarlas.</h2><script>setTimeout(()=>window.location='/',600);</script></body></html>");

    forceAPMode = true;
    wifiConfigInProgress = true;
    apMode = true;
    mostrarMensajeRedConectada(ssid, false, pass, 0, 5000);
    portalEnUso = false;
    portalPantallaFija = false;
    reinicioSolicitado = true;
    reinicioProgramado = millis() + retrasoMensajeConexion + duracionMensajeConexion;
  } else {
    server.send(400, "text/plain", "Faltan parámetros");
  }
}

void handleFinalizeConfig() {
  portalEnUso = true;
  wifiConfigInProgress = true;
  forceAPMode = true;
  apMode = true;
  portalPantallaFija = true;
  bool cerrarAPTrasBajas = false;

  String bajasMarcadas[MAX_DISPOSITIVOS];
  int totalBajasMarcadas = 0;
  for (int i = 0; i < server.args() && totalBajasMarcadas < MAX_DISPOSITIVOS; i++) {
    if (server.argName(i) == "baja_mac") {
      String macBaja = server.arg(i);
      macBaja.trim();
      if (macBaja.length() > 0) {
        bajasMarcadas[totalBajasMarcadas++] = macBaja;
      }
    }
  }

  auto liberarPortal = []() {
    portalEnUso = false;
    wifiConfigInProgress = false;
  };

  bool usuarioConfirmado = !userID.isEmpty();
  bool banderaRegistrado = server.hasArg("user_registered");

  String correo = server.hasArg("correo") ? server.arg("correo") : userEmail;
  String nombre = server.hasArg("nombre") ? server.arg("nombre") : userNombre;
  String telefono = server.hasArg("telefono") ? server.arg("telefono") : userTelefono;
  String passUsuario = server.hasArg("pass_usuario") ? server.arg("pass_usuario") : userPassword;

  correo.trim();
  nombre.trim();
  telefono.trim();
  passUsuario.trim();

  if (!usuarioConfirmado) {
    if (correo.length() == 0) {
      server.send(400, "text/plain", "Captura el correo del usuario");
      liberarPortal();
      return;
    }

    if (!banderaRegistrado) {
      if (nombre.length() == 0 || telefono.length() == 0 || passUsuario.length() == 0) {
        server.send(400, "text/plain", "Completa nombre, teléfono y password para dar de alta al usuario");
        liberarPortal();
        return;
      }
    }

    userEmail = correo;
    userNombre = nombre;
    userTelefono = telefono;
    userPassword = passUsuario;
    userFlagRegistrado = banderaRegistrado;
    userID = "";
    saveUserProfileToEEPROM();
  }

  // Guardar la red (nueva o modificada) si se proporcionó
  bool redActualizada = false;
  String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  ssid.trim();
  pass.trim();

  bool tieneDatosSsid = ssid.length() > 0;
  bool tieneDatosPass = pass.length() > 0;

  if (tieneDatosSsid || tieneDatosPass) {
    if (!tieneDatosSsid || !tieneDatosPass) {
      server.send(400, "text/plain", "Debes capturar SSID y contraseña para guardar la red");
      liberarPortal();
      return;
    }

    int requestedIndex = -1;
    if (server.hasArg("index")) {
      String idxStr = server.arg("index");
      idxStr.trim();
      if (idxStr.length() > 0) {
        requestedIndex = idxStr.toInt();
      }
    }

    int indexToSave = (requestedIndex >= 0 && requestedIndex < MAX_NETWORKS) ? requestedIndex : -1;
    if (indexToSave == -1) {
      for (int i = 0; i < MAX_NETWORKS; i++) {
        if (savedNetworks[i].ssid == ssid) {
          indexToSave = i;
          break;
        }
      }
    }
    if (indexToSave == -1) {
      for(int i = 0; i < MAX_NETWORKS; i++) {
        if(savedNetworks[i].ssid.length() == 0) {
          indexToSave = i;
          break;
        }
      }
    }

    if(indexToSave == -1) {
      indexToSave = 0;
    }

    savedNetworks[indexToSave].ssid = ssid;
    savedNetworks[indexToSave].password = pass;
    savedNetworks[indexToSave].active = true;

    for(int i = 0; i < MAX_NETWORKS; i++) {
      if(i != indexToSave) {
        savedNetworks[i].active = false;
      }
    }

    if (!saveNetworksToEEPROM()) {
      server.send(500, "text/plain", "No se pudo guardar la red en EEPROM");
      liberarPortal();
      return;
    }
    ultimaRedConfigurada = ssid;
    ultimaContrasenaConfigurada = pass;
    redActualizada = true;
  }

  String redActual = redActualizada ? ssid : WiFi.SSID();
  if (redActual.length() == 0 && ultimaRedConfigurada.length() > 0) {
    redActual = ultimaRedConfigurada;
  }

  bool conectada = WiFi.status() == WL_CONNECTED || conexionExitosa;
  String passPantalla = pass;
  if (passPantalla.isEmpty()) {
    for (int i = 0; i < MAX_NETWORKS; i++) {
      if (savedNetworks[i].active || savedNetworks[i].ssid == redActual) {
        passPantalla = savedNetworks[i].password;
        break;
      }
    }
  }

  if (totalBajasMarcadas > 0) {
    Serial.printf("🗑️ Baja solicitada desde portal para %d dispositivo(s). Se aplicará el mismo flujo que BLE antes del reinicio.\n", totalBajasMarcadas);
    for (int i = 0; i < totalBajasMarcadas; i++) {
      iniciarBajaPortal(bajasMarcadas[i]);
    }
    if (apMode || forceAPMode) {
      Serial.println("🚪 Cerrando modo AP para completar las bajas vía WiFi/MQTT y retomar el ciclo normal antes del reinicio...");
      cerrarAPTrasBajas = true;
    }
  }
  String redParaMensaje = redActual.length() > 0 ? redActual : "Sin red";
  mostrarMensajeRedConectada(redParaMensaje, conectada, passPantalla, 0, 5000);

  server.send(200, "text/html", "<html><body><h2>Configuración guardada</h2></body></html>");

  if (cerrarAPTrasBajas) {
    detenerConfiguracionWiFi();
    portalEnUso = false;
    portalPantallaFija = false;
    apMode = false;
    forceAPMode = false;
    wifiConfigInProgress = false;
  } else {
    // Mantener el portal atendiendo mientras esperamos el reinicio para evitar errores en el navegador
    portalEnUso = false;
    portalPantallaFija = false;
    apMode = true;
    forceAPMode = true;
    wifiConfigInProgress = false;
  }

  reinicioSolicitado = true;
  reinicioProgramado = millis() + retrasoMensajeConexion + duracionMensajeConexion;
}

void handleDeleteNetwork() {
  if (userID.isEmpty() && userEmail.isEmpty()) {
    server.send(400, "text/plain", "Configura el usuario (correo) antes de borrar redes");
    return;
  }
  if(server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if(index >= 0 && index < MAX_NETWORKS) {
      String removedSsid = savedNetworks[index].ssid;
      savedNetworks[index].ssid = "";
      savedNetworks[index].password = "";
      savedNetworks[index].active = false;
      if (!saveNetworksToEEPROM()) {
        Serial.println("❌ Error al guardar redes en EEPROM");
        server.send(500, "text/plain", "No se pudo borrar la red");
        return;
      }
      if (ultimaRedConfigurada == removedSsid) {
        ultimaRedConfigurada = "";
        ultimaContrasenaConfigurada = "";
      }
      if (currentNetwork == index) {
        currentNetwork = -1;
      }
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Índice inválido");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro index");
  }
}

void prepararAnimacionBajaPortal(const String &macNormalizada, const ConfigDispositivo *config) {
  solicitudAltaBLE = false;
  solicitudBajaBLE = true;
  macBajaEnCurso = macNormalizada;

  if (config != nullptr) {
    ultimoNombreDispositivo = String(config->nombre);
    ultimoNombreDispositivo.trim();
    if (ultimoNombreDispositivo.isEmpty()) {
      ultimoNombreDispositivo = "Nuup01";
    }
    ultimosLitros = config->litrosActuales;
    ultimaAltura = config->alturaConfig;
  } else {
    ultimoNombreDispositivo = "Nuup01";
    ultimosLitros = 0;
    ultimaAltura = 0;
  }
}

bool iniciarBajaPortal(const String &mac) {
  String macNormalizada = normalizarMac(mac);
  if (macNormalizada.isEmpty()) {
    Serial.println("⚠️  Baja desde portal AP: MAC vacía");
    return false;
  }

  int indice = obtenerIndiceDispositivo(macNormalizada);
  ConfigDispositivo respaldo{};
  if (indice >= 0) {
    respaldo = configDispositivos[indice];
    prepararAnimacionBajaPortal(macNormalizada, &respaldo);
  } else {
    Serial.println("⚠️  Baja desde portal AP: MAC no encontrada, se mostrará mensaje de error pero se cerrará el portal para reanudar el ciclo normal.");
    solicitudBajaBLE = true;
    ultimoNombreDispositivo = "No Registrado";
    ultimosLitros = 0;
    ultimaAltura = 0;
    macBajaEnCurso = "";
  }

  bool eliminado = iniciarBajaDispositivo(macNormalizada, "Portal AP", true);
  if (!eliminado) {
    Serial.printf("❌ No se pudo eliminar %s desde el portal\n", macNormalizada.c_str());
  }
  return eliminado;
}

void handleDeleteDevice() {
  if (userID.isEmpty() && userEmail.isEmpty()) {
    server.send(400, "text/plain", "Configura el usuario (correo) antes de borrar dispositivos");
    return;
  }
  if (server.hasArg("mac")) {
    String mac = server.arg("mac");
    mac.trim();

    if (mac.length() == 0) {
      server.send(400, "text/plain", "MAC vacía");
      return;
    }

    bool eliminado = iniciarBajaPortal(mac);
    if (eliminado) {
      server.send(200, "text/plain", "OK");
    } else {
      server.send(404, "text/plain", "Dispositivo no encontrado");
    }

    if (apMode || forceAPMode) {
      Serial.println("🚪 Cerrando modo AP para completar la baja con WiFi/MQTT y animaciones como en BLE...");
      detenerConfiguracionWiFi();
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro mac");
  }
}

void handleSelectNetwork() {
  if (userID.isEmpty() && userEmail.isEmpty()) {
    server.send(400, "text/plain", "Configura el usuario (correo) antes de seleccionar una red");
    return;
  }
  if(server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if(index >= 0 && index < MAX_NETWORKS && savedNetworks[index].ssid.length() > 0) {
      // Desactivar todas
      for(int i = 0; i < MAX_NETWORKS; i++) {
        savedNetworks[i].active = false;
      }
      // Activar la seleccionada
      savedNetworks[index].active = true;
      if (!saveNetworksToEEPROM()) {
        Serial.println("❌ Error al guardar redes en EEPROM");
      }
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Índice inválido o red vacía");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro index");
  }
}


void handleSetID() {
  if (server.hasArg("newid")) {
    String newID = server.arg("newid");
    newID.trim();

    if (newID.length() > 0 && newID.length() <= USER_ID_MAX_LEN) {
      saveUserIDToEEPROM(newID);
      userID = newID;

      // Mantener activo el portal y recargar la vista completa con redes/dispositivos
      wifiConfigInProgress = true;
      forceAPMode = true;
      apMode = true;
      handleRoot();
      return;
    } else {
      server.send(400, "text/plain", "ID inválido");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro newid");
  }
}

String horaLegibleCorta() {
  time_t ahora = time(nullptr);
  if (ahora < 1000) {
    return "hora no disponible";
  }
  struct tm *tiempo = localtime(&ahora);
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", tiempo);
  return String(buffer);
}

void mostrarResumenEstadoInicial() {
  if (!displayReady) return;

  bool tieneRedGuardada = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      tieneRedGuardada = true;
      break;
    }
  }

  int sensoresRegistrados = contarDispositivosRegistrados();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(String("Red guardada: ") + (tieneRedGuardada ? "SI" : "NO"));
  display.setCursor(0, 16);
  display.println(String("MQTT registrado: ") + (mqttConfirmed ? "SI" : "NO"));
  display.setCursor(0, 32);
  display.print("Sensores: ");
  display.println(sensoresRegistrados);
  display.display();
}

void mostrarMensajeFactory(const String &l1, const String &l2, const String &l3) {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  if (l2.length() > 0) {
    display.setCursor(0, 16);
    display.println(l2);
  }
  if (l3.length() > 0) {
    display.setCursor(0, 32);
    display.println(l3);
  }
  display.display();
}

void iniciarFlujoFactoryReset() {
  detenerConfiguracionWiFi();
  factoryResetEnProceso = true;
  factoryResetStage = FACTORY_BUSCANDO_WIFI;
  factoryWifiDeadline = millis() + FACTORY_WIFI_TIMEOUT;
  factoryConfirmDeadline = 0;
  horaConfirmacionBaja = "";
  ultimoIntentoWifiFactory = 0;

  mostrarMensajeFactory("Conectando el", "dispositivo a WiFi", "esperando red...");
  attemptReconnectToAllNetworks();
}

void manejarFlujoFactoryReset() {
  if (!factoryResetEnProceso) return;

  if (WiFi.status() == WL_CONNECTED) {
    client.loop();
  }

  switch (factoryResetStage) {
    case FACTORY_BUSCANDO_WIFI: {
      if (WiFi.status() != WL_CONNECTED) {
        if (millis() - ultimoIntentoWifiFactory > FACTORY_WIFI_RETRY) {
          ultimoIntentoWifiFactory = millis();
          attemptReconnectToAllNetworks();
          mostrarMensajeFactory("Conectando el", "dispositivo a WiFi", "reintentando...");
        }
        if (millis() > factoryWifiDeadline) {
          mostrarMensajeFactory("No se pudo dar", "de baja", "borra manualmente");
          clearEEPROM();
          reinicioSolicitado = true;
          reinicioProgramado = millis() + 4000;
          factoryResetStage = FACTORY_IDLE;
          factoryResetEnProceso = false;
          return;
        }
        return;
      }

      mostrarMensajeFactory("WiFi conectado", "Solicitando baja", "al servidor...");
      factoryResetStage = FACTORY_SOLICITANDO_BAJA;
      return;
    }
    case FACTORY_SOLICITANDO_BAJA: {
      if (WiFi.status() == WL_CONNECTED && !client.connected()) {
        reconnect();
      }
      bool enviado = solicitarBajaMonitorMQTT();
      if (enviado) {
        factoryResetStage = FACTORY_ESPERANDO_CONFIRMACION;
        factoryConfirmDeadline = millis() + FACTORY_BAJA_TIMEOUT;
        mostrarMensajeFactory("Solicitando baja", "al servidor...", "esperando confirmación");
      } else {
        if (WiFi.status() != WL_CONNECTED || !client.connected()) {
          factoryResetStage = FACTORY_BUSCANDO_WIFI;
          return;
        }
        mostrarMensajeFactory("No se pudo enviar", "la baja por WiFi", "borra manualmente");
        clearEEPROM();
        reinicioSolicitado = true;
        reinicioProgramado = millis() + 4000;
        factoryResetStage = FACTORY_IDLE;
        factoryResetEnProceso = false;
      }
      return;
    }
    case FACTORY_ESPERANDO_CONFIRMACION: {
      unsigned long restante = (factoryConfirmDeadline > millis()) ? (factoryConfirmDeadline - millis()) / 1000 : 0;
      String linea3 = "tiempo: " + String(restante) + "s";
      mostrarMensajeFactory("Esperando baja", "del servidor...", linea3);

      if (factoryConfirmDeadline > 0 && millis() > factoryConfirmDeadline) {
        mostrarMensajeFactory("Sin confirmación", "Continuando borrado", "manual obligatorio");
        clearEEPROM();
        reinicioSolicitado = true;
        reinicioProgramado = millis() + 4000;
        factoryResetStage = FACTORY_IDLE;
        factoryResetEnProceso = false;
      }
      return;
    }
    case FACTORY_CONFIRMADA: {
      String linea2 = "Hora: " + horaConfirmacionBaja;
      mostrarMensajeFactory("Dado de baja", linea2, "Reiniciando...");
      clearEEPROM();
      reinicioSolicitado = true;
      reinicioProgramado = millis() + 3000;
      factoryResetStage = FACTORY_IDLE;
      factoryResetEnProceso = false;
      return;
    }
    case FACTORY_FALLO_WIFI:
    case FACTORY_IDLE:
    default:
      return;
  }
}

void handleFactoryReset() {
  server.send(200, "text/plain", "Reseteo solicitado: se pedirá la baja, se borrarán los datos y se reiniciará.");
  iniciarFlujoFactoryReset();
}

void handleBajaMonitor() {
  server.send(410, "text/plain", "La baja directa fue deshabilitada. Usa el reseteo de fábrica para solicitarla.");
}


bool saveNetworksToEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ No se pudo iniciar EEPROM para guardar redes");
    return false;
  }
  // Marcar EEPROM inicializada para que las redes persistan tras reinicios
  EEPROM.write(0, 1);
  int address = 1; // Empezamos en 1 porque 0 es el flag de inicialización

  for(int i = 0; i < MAX_NETWORKS; i++) {
    // Guardar SSID
    int ssidLen = savedNetworks[i].ssid.length();
    if(ssidLen > SSID_LEN) ssidLen = SSID_LEN;
    EEPROM.write(address++, ssidLen);

    for(int j = 0; j < ssidLen; j++) {
      EEPROM.write(address++, savedNetworks[i].ssid[j]);
    }
    // Rellenar con ceros si ssidLen es menor que SSID_LEN
    for(int j = ssidLen; j < SSID_LEN; j++) {
      EEPROM.write(address++, 0);
    }

    // Guardar contraseña
    int passLen = savedNetworks[i].password.length();
    if(passLen > PASS_LEN) passLen = PASS_LEN;
    EEPROM.write(address++, passLen);

    for(int j = 0; j < passLen; j++) {
      EEPROM.write(address++, savedNetworks[i].password[j]);
    }
    // Rellenar con ceros si passLen es menor que PASS_LEN
    for(int j = passLen; j < PASS_LEN; j++) {
      EEPROM.write(address++, 0);
    }

    // Guardar estado activo
    EEPROM.write(address++, savedNetworks[i].active ? 1 : 0);
  }

  bool commitOk = EEPROM.commit();
  if (!commitOk) {
    Serial.println("❌ Error en commit de redes WiFi");
  }

  EEPROM.end();

  return commitOk;
}


bool loadNetworksFromEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ No se pudo iniciar EEPROM para leer redes");
    return false;
  }
  int address = 1; // Empezamos en 1 porque 0 es el flag de inicialización
  bool success = true;

  for (int i = 0; i < MAX_NETWORKS; i++) {
    // Leer SSID
    int ssidLen = EEPROM.read(address++);
    if (ssidLen < 0 || ssidLen > SSID_LEN) {
      Serial.printf("Error: Longitud SSID inválida en red %d: %d\n", i, ssidLen);
      success = false;
      ssidLen = 0; // Usar longitud cero para evitar problemas
    }

    char ssidData[SSID_LEN + 1] = {0};
    for (int j = 0; j < ssidLen; j++) {
      ssidData[j] = EEPROM.read(address++);
      // Solo descartar caracteres de control; permitir acentos/ñ
      if (static_cast<unsigned char>(ssidData[j]) < 32) {
        ssidData[j] = '?';
      }
    }
    // Saltar relleno si es necesario
    address += (SSID_LEN - ssidLen);
    
    savedNetworks[i].ssid = String(ssidData);

    // Leer contraseña
    int passLen = EEPROM.read(address++);
    if (passLen < 0 || passLen > PASS_LEN) {
      Serial.printf("Error: Longitud Pass inválida en red %d: %d\n", i, passLen);
      success = false;
      passLen = 0;
    }

    char passData[PASS_LEN + 1] = {0};
    for (int j = 0; j < passLen; j++) {
      passData[j] = EEPROM.read(address++);
      // No verificamos caracteres de contraseña por seguridad
    }
    // Saltar relleno si es necesario
    address += (PASS_LEN - passLen);
    
    savedNetworks[i].password = String(passData);


    // DEBUG DETALLADO
    Serial.printf("Red %d - SSID: '%s', Password: '%s' (longitud: %d)\n", 
                  i, savedNetworks[i].ssid.c_str(), 
                  savedNetworks[i].password.c_str(),
                  savedNetworks[i].password.length());


    // Leer estado activo
    byte active = EEPROM.read(address++);
    savedNetworks[i].active = (active == 1);
    
    // Verificar si el estado active es válido (0 o 1)
    if (active != 0 && active != 1) {
      Serial.printf("Error: Valor active inválido en red %d: %d\n", i, active);
      success = false;
      savedNetworks[i].active = false;
    }

    // Solo imprimir redes con SSID no vacío
    if (savedNetworks[i].ssid.length() > 0) {
      Serial.printf("Red %d cargada: SSID='%s', Pass=%s, Activa=%d\n", 
                   i, 
                   savedNetworks[i].ssid.c_str(), 
                   savedNetworks[i].password.length() > 0 ? "[oculta]" : "vacía",
                   savedNetworks[i].active ? 1 : 0);
    }
  }

  // Verificación de dirección final
  if (address > EEPROM_SIZE) {
    Serial.printf("Error: Dirección EEPROM excede tamaño máximo (%d > %d)\n", address, EEPROM_SIZE);
    success = false;
  }

  EEPROM.end();

  return success;
}

void checkWiFiStatus() {
  int status = WiFi.status();
  Serial.print("Estado WiFi: ");
  switch(status) {
    case WL_NO_SHIELD: Serial.println("WL_NO_SHIELD (255)"); break;
    case WL_IDLE_STATUS: Serial.println("WL_IDLE_STATUS (0)"); break;
    case WL_NO_SSID_AVAIL: Serial.println("WL_NO_SSID_AVAIL (1)"); break;
    case WL_SCAN_COMPLETED: Serial.println("WL_SCAN_COMPLETED (2)"); break;
    case WL_CONNECTED: Serial.println("WL_CONNECTED (3)"); break;
    case WL_CONNECT_FAILED: Serial.println("WL_CONNECT_FAILED (4) - Contraseña incorrecta"); break;
    case WL_CONNECTION_LOST: Serial.println("WL_CONNECTION_LOST (5)"); break;
    case WL_DISCONNECTED: Serial.println("WL_DISCONNECTED (6)"); break;
    default: Serial.println("Estado desconocido: " + String(status)); break;
  }
}

bool attemptReconnectToAllNetworks() {
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Verificar redes
  debugNetworks();  // ← ESTO MOSTRARÁ LAS CONTRASEÑAS
  
  bool hasNetworks = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      hasNetworks = true;
      
      if (savedNetworks[i].active) {
        Serial.print("Conectando a: '");
        Serial.print(savedNetworks[i].ssid);
        Serial.print("' con pass: '");
        Serial.print(savedNetworks[i].password);
        Serial.println("'");
        
        WiFi.begin(savedNetworks[i].ssid.c_str(), savedNetworks[i].password.c_str());

        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_TIMEOUT) {
          delay(500);
          Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\n✓ CONECTADO EXITOSAMENTE");
          Serial.print("IP: ");
          Serial.println(WiFi.localIP());
          return true;
        } else {
          Serial.println("\n✗ FALLO EN CONEXIÓN");
          checkWiFiStatus();  // ← DIAGNÓSTICO DETALLADO
        }
      }
    }
  }

  if (!hasNetworks) {
    Serial.println("No hay redes WiFi guardadas");
  }

  return hasNetworks;
}

void debugNetworks() {
  Serial.println("=== DEBUG REDES GUARDADAS ===");
  bool anyNetwork = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      anyNetwork = true;
      Serial.printf("Red %d: SSID='%s', Pass='%s', Activa=%d, PassLen=%d\n", 
                   i, 
                   savedNetworks[i].ssid.c_str(),
                   savedNetworks[i].password.c_str(),  // ← MUESTRA LA CONTRASEÑA
                   savedNetworks[i].active ? 1 : 0,
                   savedNetworks[i].password.length());
    }
  }
  if (!anyNetwork) {
    Serial.println("NO hay redes guardadas");
  }
  Serial.println("==============================");
}


void saveUserIDToEEPROM(const String& id) {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ No se pudo iniciar EEPROM para guardar User ID");
    return;
  }
  int len = id.length();
  if (len > USER_ID_MAX_LEN) len = USER_ID_MAX_LEN;

  EEPROM.write(USER_ID_ADDR, len);  // Guardar longitud

  for (int i = 0; i < len; i++) {
    EEPROM.write(USER_ID_ADDR + 1 + i, id[i]);
  }

  // Rellenar con ceros si sobran caracteres
  for (int i = len; i < USER_ID_MAX_LEN; i++) {
    EEPROM.write(USER_ID_ADDR + 1 + i, 0);
  }

  EEPROM.commit();
  Serial.println("📝 ID guardado en EEPROM: " + id);
}

bool loadUserIDFromEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ No se pudo iniciar EEPROM para leer User ID");
    return false;
  }
  bool success = true;
  int len = EEPROM.read(USER_ID_ADDR);
  if (len == 0xFF || len < 0 || len > USER_ID_MAX_LEN) {
    Serial.printf("⚠️  Longitud de UserID inválida (%d). Se limpia y se devuelve false.\n", len);
    saveUserIDToEEPROM("");
    return false;
  }
  if (len > USER_ID_MAX_LEN) len = USER_ID_MAX_LEN;

  char buffer[USER_ID_MAX_LEN + 1] = {0};
  for (int i = 0; i < len; i++) {
    buffer[i] = EEPROM.read(USER_ID_ADDR + 1 + i);
  }

  buffer[len] = '\0';
  userID = String(buffer);
  Serial.println("🔄 ID cargado desde EEPROM: " + userID);
  return success;
}

void saveStringFieldToEEPROM(int address, const String &value, int maxLen) {
  int len = value.length();
  if (len > maxLen) len = maxLen;

  EEPROM.write(address, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(address + 1 + i, value[i]);
  }

  for (int i = len; i < maxLen; i++) {
    EEPROM.write(address + 1 + i, 0);
  }
}

String loadStringFieldFromEEPROM(int address, int maxLen) {
  int len = EEPROM.read(address);
  if (len == 0xFF || len < 0 || len > maxLen) {
    return "";
  }
  char buffer[maxLen + 1] = {0};
  for (int i = 0; i < len; i++) {
    buffer[i] = EEPROM.read(address + 1 + i);
  }
  buffer[len] = '\0';
  return String(buffer);
}

void saveUserProfileToEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ No se pudo iniciar EEPROM para guardar perfil de usuario");
    return;
  }

  saveStringFieldToEEPROM(USER_NAME_ADDR, userNombre, USER_NAME_MAX_LEN);
  saveStringFieldToEEPROM(USER_PHONE_ADDR, userTelefono, USER_PHONE_MAX_LEN);
  saveStringFieldToEEPROM(USER_EMAIL_ADDR, userEmail, USER_EMAIL_MAX_LEN);
  saveStringFieldToEEPROM(USER_PASS_ADDR, userPassword, USER_PASS_MAX_LEN);
  EEPROM.write(USER_REGISTERED_FLAG_ADDR, userFlagRegistrado ? 1 : 0);

  int len = userID.length();
  if (len > USER_ID_MAX_LEN) len = USER_ID_MAX_LEN;
  EEPROM.write(USER_ID_ADDR, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(USER_ID_ADDR + 1 + i, userID[i]);
  }
  for (int i = len; i < USER_ID_MAX_LEN; i++) {
    EEPROM.write(USER_ID_ADDR + 1 + i, 0);
  }

  EEPROM.commit();
  EEPROM.end();
}

void loadUserProfileFromEEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ No se pudo iniciar EEPROM para leer perfil de usuario");
    return;
  }

  userNombre = loadStringFieldFromEEPROM(USER_NAME_ADDR, USER_NAME_MAX_LEN);
  userTelefono = loadStringFieldFromEEPROM(USER_PHONE_ADDR, USER_PHONE_MAX_LEN);
  userEmail = loadStringFieldFromEEPROM(USER_EMAIL_ADDR, USER_EMAIL_MAX_LEN);
  userPassword = loadStringFieldFromEEPROM(USER_PASS_ADDR, USER_PASS_MAX_LEN);
  userFlagRegistrado = EEPROM.read(USER_REGISTERED_FLAG_ADDR) == 1;
  EEPROM.end();

  loadUserIDFromEEPROM();
}

String obtenerTopicoConfirmacionMonitor() {
  asegurarMacMonitorFija("topico_confirmacion");
  if (macMonitorFija.isEmpty()) {
    return "";
  }
  return String("NUUP/") + macMonitorFija + "/confirmacion/";
}

void suscribirTopicoConfirmacionMonitor() {
  String topico = obtenerTopicoConfirmacionMonitor();
  if (topico.isEmpty()) {
    Serial.println("⚠️  No se pudo suscribir a confirmacion: MAC del monitor vacía");
    return;
  }
  client.subscribe(topico.c_str(), 1);
  Serial.println("Subscripcion: " + topico);
}

void publicarConfirmacionModificacionMQTT(const String &macSensor, const String &estado) {
  if (!client.connected()) {
    Serial.println("⚠️  No se envió confirmación MQTT: cliente desconectado");
    return;
  }

  String topico = obtenerTopicoConfirmacionMonitor();
  if (topico.isEmpty()) {
    Serial.println("⚠️  No se envió confirmación: tópico vacío por falta de MAC");
    return;
  }

  String payload = macSensor + "," + estado;
  if (client.publish(topico.c_str(), payload.c_str())) {
    Serial.printf("📡 Confirmación MQTT enviada -> topic:%s payload:%s\n", topico.c_str(), payload.c_str());
  } else {
    Serial.println("❌ Error al publicar la confirmación de modificación");
  }
}

static String obtenerCampoCSV(const String &texto, int indice) {
  int inicio = 0;
  int fin = -1;
  for (int i = 0; i <= indice; i++) {
    inicio = fin + 1;
    fin = texto.indexOf(',', inicio);
    if (fin == -1 && i < indice) {
      return "";
    }
  }
  if (fin == -1) fin = texto.length();
  return texto.substring(inicio, fin);
}

bool procesarConfirmacionBroker(const String &topic, const String &mensaje) {
  if (!topic.startsWith("NUUP/")) return false;
  if (!topic.endsWith("/confirmacion/")) return false;

  int segundoSlash = topic.indexOf('/', 5);
  if (segundoSlash == -1) return false;
  String macTopic = normalizarMac(topic.substring(5, segundoSlash));

  asegurarMacMonitorFija("confirmacion_broker");
  if (macTopic != normalizarMac(macMonitorFija)) {
    Serial.printf("⏭️  Confirmación ignorada: MAC en tópico (%s) no coincide con monitor (%s)\n",
                  macTopic.c_str(), macMonitorFija.c_str());
    return false;
  }

  Serial.println("\n📥 [MQTT][RX] Confirmación desde broker");
  Serial.printf("   Topic   : %s\n", topic.c_str());
  Serial.printf("   Payload : %s\n", mensaje.c_str());
  Serial.println("   ↩️ Respuesta del broker a la telemetría MQTT del monitor");

  String macSensor = normalizarMac(obtenerCampoCSV(mensaje, 0));
  String comando = obtenerCampoCSV(mensaje, 1);
  comando.trim();

  if (macSensor.isEmpty() || comando.isEmpty()) {
    Serial.println("⚠️  Confirmación MQTT sin MAC o comando válido");
    return true;
  }

  int indice = obtenerIndiceDispositivo(macSensor);
  if (indice < 0) {
    Serial.printf("⚠️  Confirmación MQTT para MAC no registrada: %s\n", macSensor.c_str());
    return true;
  }

  if (comando == "modificar") {
    String aliasObjetivo = obtenerCampoCSV(mensaje, 2);
    String alturaStr = obtenerCampoCSV(mensaje, 3);
    String capacidadStr = obtenerCampoCSV(mensaje, 4);
    String litrosStr = obtenerCampoCSV(mensaje, 5);

    aliasObjetivo.trim();
    if (aliasObjetivo.isEmpty()) aliasObjetivo = "NUUP01 NIVEL";

    ConfigDispositivo &config = configDispositivos[indice];
    aliasObjetivo.toCharArray(config.nombre, sizeof(config.nombre));
    config.alturaConfig = alturaStr.toFloat();
    config.litrosConfig = capacidadStr.toFloat();
    config.litrosActuales = litrosStr.toFloat();

    if (config.litrosConfig > 0) {
      float porcentaje = (config.litrosActuales / config.litrosConfig) * 100.0f;
      config.porcentaje = (int)constrain(porcentaje, 0, 100);
    }

    aliasObjetivoBroker[indice] = aliasObjetivo;
    alturaObjetivoBroker[indice] = config.alturaConfig;
    capacidadObjetivoBroker[indice] = config.litrosConfig;
    litrosReportadosBroker[indice] = config.litrosActuales;
    modificacionBrokerActiva[indice] = true;

    if (esperandoConfirmacionBroker && macEsperandoConfirmacion == macSensor) {
      Serial.printf("   📬 Confirmación del broker recibida para %s; fin de espera MQTT\n", macSensor.c_str());
      esperandoConfirmacionBroker = false;
      macEsperandoConfirmacion = "";
    } else if (!esperandoConfirmacionBroker) {
      Serial.printf(
          "   📬 Solicitud de modificación iniciada por servidor para %s (device_modificacion=1, sin telemetría previa)\n",
          macSensor.c_str());
    } else if (macEsperandoConfirmacion != macSensor) {
      Serial.printf(
          "   📬 Solicitud de modificación recibida para %s mientras se esperaba confirmación de %s\n",
          macSensor.c_str(),
          macEsperandoConfirmacion.c_str());
    }

    Serial.println("   📌 Acción del monitor: aplicar alias/altura/capacidad/litros en EEPROM y marcar modificación en curso");
    Serial.printf("      Alias objetivo    : %s\n", aliasObjetivo.c_str());
    Serial.printf("      Altura objetivo   : %.1f cm\n", alturaObjetivoBroker[indice]);
    Serial.printf("      Capacidad objetivo: %.1f L\n", capacidadObjetivoBroker[indice]);
    Serial.printf("      Litros reportados : %.1f L\n", litrosReportadosBroker[indice]);

    guardarDispositivos();
    publicarConfirmacionModificacionMQTT(macSensor, "modificacion_ok");
    Serial.printf("   ✅ Confirmación al broker: %s,modificacion_ok\n", macSensor.c_str());
    Serial.println("   🔁 El monitor seguirá usando los valores de EEPROM en LoRa hasta limpiar la bandera");
    return true;
  }

  if (comando == "sin_cambios") {
    if (modificacionBrokerActiva[indice]) {
      Serial.printf(
          "   ℹ️ El broker envió sin_cambios pero aún falta validar por LoRa; se conserva DEVICE_MODIFICACION para %s\n",
          macSensor.c_str());
      return true;
    }
    modificacionBrokerActiva[indice] = false;
    if (esperandoConfirmacionBroker && macEsperandoConfirmacion == macSensor) {
      Serial.printf("   📬 Confirmación sin cambios recibida para %s; fin de espera MQTT\n", macSensor.c_str());
      esperandoConfirmacionBroker = false;
      macEsperandoConfirmacion = "";
    }
    Serial.printf("   ℹ️ El broker indica sin_cambios para %s; el monitor continúa su ciclo normal\n", macSensor.c_str());
    return true;
  }

  if (comando == "modificacion_aplicada" || comando == "modificacion_ok") {
    modificacionBrokerActiva[indice] = true;
    if (esperandoConfirmacionBroker && macEsperandoConfirmacion == macSensor) {
      Serial.printf("   📬 Confirmación final recibida para %s; fin de espera MQTT\n", macSensor.c_str());
      esperandoConfirmacionBroker = false;
      macEsperandoConfirmacion = "";
    }
    Serial.printf(
        "   ℹ️ Confirmación final recibida para %s; se mantendrá DEVICE_MODIFICACION activo hasta validar datos por LoRa\n",
        macSensor.c_str());
    return true;
  }

  Serial.printf("⏭️  Comando de confirmación no reconocido: %s\n", comando.c_str());
  return true;
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("callback MQTT ejecutado recepcion-->");  // al principio de la función
  // Crear buffer seguro para el mensaje
  char message[length + 1];
  strncpy(message, (char*)payload, length);
  message[length] = '\0';

  String strTopic = String(topic);
  String strMessage = String(message);

  Serial.printf("Mensaje recibido MQTT TOPIC[%s]: message--> %s\n", topic, message);

  if (procesarConfirmacionBroker(strTopic, strMessage)) {
    return;
  }

  asegurarMacMonitorFija("estatus_mqtt");
  String topicEstatus = String("NUUP/") + normalizarMac(macMonitorFija) + "/estatus";
  if (strTopic.equals(topicEstatus)) {
    int estatus = extraerEstatusLlenado(strMessage);
    waterDisplayLlenando = (estatus == 2);
    Serial.printf("💧 Estado llenado OLED: estatus=%d -> %s\n",
                  estatus, waterDisplayLlenando ? "LLENANDO" : "SIN LLENADO");
    return;
  }

// ALTA MONITOR / DISPOSITIVOS Validación estricta para el topic de confirmación
if (strcmp(topic, "alta/0/confirmacion/") == 0) {
    String mensajeRecibido = String(message);
    Serial.println("Confirmación de monitor recibida: " + mensajeRecibido);

    // Esperado: MAC,registrado[,correo,nombre,users_registro_id]
    int indices[4] = {-1, -1, -1, -1};
    int buscador = 0;
    int inicio = 0;
    while (buscador < 4) {
      int coma = mensajeRecibido.indexOf(',', inicio);
      if (coma == -1) break;
      indices[buscador] = coma;
      buscador++;
      inicio = coma + 1;
    }

    if (indices[0] == -1) {
        Serial.println("⚠️  Mensaje de confirmación de monitor sin MAC");
        return;
    }

    String macConfirmada = mensajeRecibido.substring(0, indices[0]);
    macConfirmada = normalizarMac(macConfirmada);

    String estadoConfirmacion = (indices[0] != -1 && indices[1] != -1)
                                   ? mensajeRecibido.substring(indices[0] + 1, indices[1])
                                   : mensajeRecibido.substring(indices[0] + 1);
    estadoConfirmacion.trim();

    String correoConfirmado = (indices[1] != -1 && indices[2] != -1)
                                ? mensajeRecibido.substring(indices[1] + 1, indices[2])
                                : "";
    correoConfirmado.trim();

    String nombreConfirmado = (indices[2] != -1 && indices[3] != -1)
                                ? mensajeRecibido.substring(indices[2] + 1, indices[3])
                                : "";
    nombreConfirmado.trim();

    String registroConfirmado = (indices[3] != -1)
                                  ? mensajeRecibido.substring(indices[3] + 1)
                                  : "";
    registroConfirmado.trim();

    asegurarMacMonitorFija("conf_monitor");
    String miMac = normalizarMac(macMonitorFija);

    Serial.println("📥 [CONF MONITOR01] alta/0/confirmacion/" );
    Serial.println("   Payload RX: " + mensajeRecibido);
    Serial.println("   Se esperaba: MAC,registrado[,correo,nombre,users_registro_id]");

    if (macConfirmada == miMac && (estadoConfirmacion == "registrado" || estadoConfirmacion == "confirmado")) {
        Serial.println("✅ Confirmación MQTT para el monitor recibida");
        mqttConfirmed = true;
        solicitudAltaInicialEnviada = true;

        if (registroConfirmado.length() > 0) {
          userID = registroConfirmado;
        }
        if (correoConfirmado.length() > 0) {
          userEmail = correoConfirmado;
        }
        if (nombreConfirmado.length() > 0) {
          userNombre = nombreConfirmado;
        }
        userFlagRegistrado = true;

        saveUserProfileToEEPROM();
        guardarMQTTConfirmationState(true);
        guardarSolicitudAltaInicialState(true);
        activarDispositivosTrasConfirmacion();
    } else {
        Serial.printf("⚠️ Confirmación de monitor ignorada (MAC/estado no coinciden): %s / %s\n",
                      macConfirmada.c_str(), estadoConfirmacion.c_str());
    }
    return;
}

if (strcmp(topic, "baja/0/confirmacion/") == 0) {
    String mensajeRecibido = String(message);
    Serial.println("📥 [CONF MONITOR01] baja/0/confirmacion/");
    Serial.println("   Payload RX: " + mensajeRecibido);
    Serial.println("   Se esperaba: MAC,eliminado");

    int primeraComa = mensajeRecibido.indexOf(',');
    if (primeraComa == -1) {
        Serial.println("⚠️  Mensaje de confirmación de baja sin MAC");
        return;
    }

    String macConfirmada = normalizarMac(mensajeRecibido.substring(0, primeraComa));
    String estado = mensajeRecibido.substring(primeraComa + 1);
    estado.trim();

    asegurarMacMonitorFija("conf_baja_monitor");
    String macEsperada = macMonitorFija;
    if (macEsperada.isEmpty() && !ultimaMacMonitorBaja.isEmpty()) {
      macEsperada = ultimaMacMonitorBaja;
    }
    macEsperada = normalizarMac(macEsperada);

    if (macEsperada.isEmpty()) {
      Serial.println("⚠️  No hay MAC del monitor para validar la confirmación de baja");
      return;
    }

    if (!macConfirmada.equalsIgnoreCase(macEsperada)) {
      Serial.printf("⚠️  Confirmación de baja ignorada: MAC no coincide (%s vs %s)\n",
                    macConfirmada.c_str(), macEsperada.c_str());
      return;
    }

    if (estado == "eliminado" || estado == "baja" || estado == "confirmado") {
      Serial.println("✅ Baja MQTT del monitor confirmada; limpiando y reiniciando");
      bajaMonitorEsperandoConfirmacion = false;
      if (factoryResetEnProceso) {
        horaConfirmacionBaja = horaLegibleCorta();
        factoryResetStage = FACTORY_CONFIRMADA;
      } else {
        clearEEPROM();
        reinicioSolicitado = true;
        reinicioProgramado = millis() + 2000;
      }
    } else {
      Serial.printf("⚠️ Estado de baja del monitor no reconocido: %s\n", estado.c_str());
    }
    return;
}

if (strcmp(topic, "baja/1/confirmacion/") == 0) {
    String mensajeRecibido = String(message);
    Serial.println("📥 [CONF NUUP01] baja/1/confirmacion/");
    Serial.println("   Payload RX: " + mensajeRecibido);
    Serial.println("   Se esperaba: MAC,eliminado");

    int primeraComa = mensajeRecibido.indexOf(',');
    if (primeraComa == -1) {
        Serial.println("⚠️  Mensaje de confirmación de baja sin MAC");
        return;
    }

    String macConfirmada = normalizarMac(mensajeRecibido.substring(0, primeraComa));
    String estado = mensajeRecibido.substring(primeraComa + 1);
    estado.trim();

    int indice = obtenerIndiceDispositivo(macConfirmada);

    if (estado == "eliminado" || estado == "baja" || estado == "confirmado") {
        if (indice != -1) {
            limpiarEstadoBaja(indice);
            if (eliminarDispositivo(macConfirmada)) {
                Serial.printf("✅ Baja confirmada y dispositivo %s eliminado localmente\n", macConfirmada.c_str());
            } else {
                Serial.printf("❌ No se pudo eliminar %s tras confirmación de baja\n", macConfirmada.c_str());
            }
        } else {
            Serial.printf("ℹ️ Confirmación de baja recibida para MAC ya eliminada: %s\n", macConfirmada.c_str());
        }
        limpiarBajaPendientePorMac(macConfirmada);
    } else {
        Serial.printf("⚠️ Estado de baja no reconocido: %s\n", estado.c_str());
    }
    return;
}

if (strcmp(topic, "alta/1/confirmacion/") == 0) {
    String mensajeRecibido = String(message);

    int primeraComa = mensajeRecibido.indexOf(',');
    int segundaComa = mensajeRecibido.indexOf(',', primeraComa + 1);
    int terceraComa = mensajeRecibido.indexOf(',', segundaComa + 1);

    if (primeraComa == -1) {
        Serial.println("⚠️  Mensaje de confirmación sin MAC");
        return;
    }

    String macConfirmada = mensajeRecibido.substring(0, primeraComa);
    macConfirmada.trim();
    String estadoConfirmacion = (segundaComa != -1)
                                   ? mensajeRecibido.substring(primeraComa + 1, segundaComa)
                                   : mensajeRecibido.substring(primeraComa + 1);
    estadoConfirmacion.trim();

    String miMac = WiFi.macAddress();
    miMac.replace("-", ":");

    bool esMonitor = macConfirmada == miMac;

    if (esMonitor && estadoConfirmacion == "registrado" && mqttConfirmed && ultimoPayloadConfirmMonitor == mensajeRecibido) {
        Serial.printf("ℹ️ [CONF MONITOR01] Confirmación duplicada ignorada para %s\n", macConfirmada.c_str());
        return;
    }

    if (!esMonitor && estadoConfirmacion == "registrado") {
        int indiceTemp = obtenerIndiceDispositivo(macConfirmada);
        if (indiceTemp >= 0 && configDispositivos[indiceTemp].activo && ultimoPayloadConfirmAlta[indiceTemp] == mensajeRecibido) {
            Serial.printf("ℹ️ [CONF NUUP01] Confirmación duplicada ignorada para %s\n", macConfirmada.c_str());
            return;
        }
    }

    Serial.println("📥 [CONF ALTA] alta/1/confirmacion/");
    Serial.println("   Payload RX: " + mensajeRecibido);
    Serial.println("   Se esperaba: MAC,registrado[,usuario,email]");

    if (macConfirmada == miMac) {
        // Confirmación para el monitor
        int cuartaComa = (terceraComa != -1) ? mensajeRecibido.indexOf(',', terceraComa + 1) : -1;
        if (estadoConfirmacion == "registrado" && segundaComa != -1) {
            String correoUsuario = (terceraComa != -1)
                                       ? mensajeRecibido.substring(segundaComa + 1, terceraComa)
                                       : mensajeRecibido.substring(segundaComa + 1);
            String nombreUsuario = (terceraComa != -1 && cuartaComa != -1)
                                       ? mensajeRecibido.substring(terceraComa + 1, cuartaComa)
                                       : "";
            String registroId = (cuartaComa != -1)
                                     ? mensajeRecibido.substring(cuartaComa + 1)
                                     : "";

            userNombre = nombreUsuario;
            userEmail = correoUsuario;
            userID = registroId;
            userFlagRegistrado = true;
            guardarMQTTConfirmationState(true);
            saveUserProfileToEEPROM();

            Serial.println("CONFIRMACION RECIBIDA - Alta del monitor validada correctamente");
            Serial.println("Nombre guardado: " + nombreUsuario);
            Serial.println("Email guardado: " + correoUsuario);
            Serial.println("users_registro_id: " + registroId);

            mqttConfirmed = true;
            solicitudAltaInicialEnviada = true;
            guardarSolicitudAltaInicialState(true);
            ultimoPayloadConfirmMonitor = mensajeRecibido;
            activarDispositivosTrasConfirmacion();
            delay(3000); // Espera para evitar conflictos
        } else {
            Serial.println("Formato de mensaje incorrecto para el monitor. Faltan datos de usuario");
        }
    } else if (estadoConfirmacion == "registrado") {
        int indice = obtenerIndiceDispositivo(macConfirmada);
        if (indice >= 0) {
            if (configDispositivos[indice].activo && ultimoPayloadConfirmAlta[indice] == mensajeRecibido) {
                Serial.printf("ℹ️ [CONF NUUP01] Confirmación duplicada ignorada para %s\n", macConfirmada.c_str());
                return;
            }
            Serial.printf("✅ [CONF NUUP01] Alta confirmada para %s -> activar dispositivo y permitir LoRa->MQTT\n", macConfirmada.c_str());
            configDispositivos[indice].activo = true;
            ultimaSolicitudAlta[indice] = 0;
            solicitudAltaEnviada[indice] = false;
            ultimoPayloadConfirmAlta[indice] = mensajeRecibido;
            if (guardarDispositivos()) {
                Serial.printf("✅ Dispositivo %s confirmado y activado\n", macConfirmada.c_str());
            } else {
                Serial.printf("❌ Error al guardar activación de %s\n", macConfirmada.c_str());
            }
        } else {
            Serial.printf("⚠️ Confirmación recibida para MAC desconocida: %s\n", macConfirmada.c_str());
        }
    } else {
        Serial.printf("ADVERTENCIA - Mensaje no reconocido en alta/1/confirmacion/: '%s'\n", message);

        Serial.println("Contenido hexadecimal del mensaje:");
        for (unsigned int i = 0; i < length; i++) {
            Serial.printf("%02X ", payload[i]);
        }
        Serial.println();
    }
    return;
}
//termina confirmacion de alta

}

void reconnect() {
  static unsigned long lastAttempt = 0;
  const unsigned long retryInterval = 5000;

  if (millis() - lastAttempt < retryInterval) return;
  lastAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se pudo conectar a MQTT --> Error: WiFi no conectado");
    return;
  }

  char clientId[40];
  snprintf(clientId, sizeof(clientId), "Monitor-%08X", (uint32_t)ESP.getEfuseMac());

  if (client.connect(clientId, mqtt_user, mqtt_pass)) {
    Serial.println("Conexión MQTT establecida preparado para el alta de dispositivo.............");
    Serial.println("Cliente:"+String(clientId)+" Usuario: "+ mqtt_user+" PAssword:"+ mqtt_pass);

  
    // 2. Suscripciones con QoS 1 (confirmación de recepción)
    client.subscribe("alta/0/confirmacion/", 1);
Serial.println("Subscripcion: alta/0/confirmacion/");
    delay(50);
    client.subscribe("baja/0/confirmacion/", 1);
Serial.println("Subscripcion: baja/0/confirmacion/");
    delay(50);
    client.subscribe("alta/1/confirmacion/", 1);
Serial.println("Subscripcion: alta/1/confirmacion/");
    delay(50);
    client.subscribe("baja/1/confirmacion/", 1);
Serial.println("Subscripcion: baja/1/confirmacion/");
    delay(50);
    client.subscribe((String(serial_number) + "/command").c_str(), 1);
Serial.println("Subscripcion: /command");
   delay(50);
    asegurarMacMonitorFija("suscripcion_estatus");
    String topicoEstatus = String("NUUP/") + normalizarMac(macMonitorFija) + "/estatus";
    client.subscribe(topicoEstatus.c_str(), 1);
Serial.println("Subscripcion: " + topicoEstatus);
   delay(50);
    suscribirTopicoConfirmacionMonitor();

    imprimirEstadoBajasPendientes("MQTT reconectado");
    procesarBajasPendientes();

  } else {
    Serial.printf("Error en conexión MQTT pero wiffi conectado --> (estado: %d)\n", client.state());
  }
} 

void checkMemory() {
  Serial.println("\n--- Memory Report ---");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
  Serial.printf("Main Task Stack: %d bytes free\n", uxTaskGetStackHighWaterMark(NULL));
  Serial.printf("PSRAM: %d bytes free\n", ESP.getFreePsram());
  Serial.println("---------------------");
}




// Función auxiliar para imprimir dispositivos (para debug)
void imprimirDispositivosRegistrados() {
  Serial.println("--- Dispositivos Registrados (detalle completo) ---");
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (String(configDispositivos[i].mac) != "") {
      Serial.printf("[%02d] MAC: %s | Nombre: %s | Tipo: %d | Activo: %s\n",
                    i,
                    configDispositivos[i].mac,
                    configDispositivos[i].nombre,
                    configDispositivos[i].tipoDispositivo,
                    configDispositivos[i].activo ? "SI" : "NO");
      Serial.printf("      Litros: %.2f/%.2f | Voltaje: %.2f | Temp: %.2f | Altura: %.2f | %%: %d\n",
                    configDispositivos[i].litrosActuales,
                    configDispositivos[i].litrosConfig,
                    configDispositivos[i].voltaje,
                    configDispositivos[i].temperatura,
                    configDispositivos[i].alturaConfig,
                    configDispositivos[i].porcentaje);
      Serial.printf("      Último mensaje LoRa (IA incluido): %s\n",
                    ultimoMensajeLoRaDispositivo[i].c_str());
    }
  }
  Serial.println("----------------------------------------------------");
}

int contarBajasPendientesRAM() {
  int total = 0;
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (bajaPendienteMQTT[i]) total++;
  }
  return total;
}

int contarBajasPendientesPersistentes() {
  int total = 0;
  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    if (bajasPendientesActivas[i]) total++;
  }
  return total;
}

void imprimirEstadoBajasPendientes(const char* origen) {
  int ram = contarBajasPendientesRAM();
  int persistentes = contarBajasPendientesPersistentes();
  Serial.printf("📌 [%s] Estado de bajas pendientes -> RAM:%d | EEPROM:%d\n", origen, ram, persistentes);

  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (!bajaPendienteMQTT[i]) continue;
    Serial.printf("   RAM[%02d]: MAC=%s | activo=%s | ultimo_intento=%lu | espera=%lu\n",
                  i,
                  configDispositivos[i].mac,
                  configDispositivos[i].activo ? "SI" : "NO",
                  ultimaSolicitudBaja[i],
                  inicioEsperaBaja[i]);
  }

  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    if (!bajasPendientesActivas[i]) continue;
    Serial.printf("   EEPROM[%02d]: MAC=%s | ultimo_intento=%lu | espera=%lu\n",
                  i,
                  bajasPendientesMac[i],
                  bajasPendientesUltimoIntento[i],
                  bajasPendientesInicioEspera[i]);
  }
}

bool registrarDispositivo(const String &mac, byte tipo) {
  String macNormalizada = normalizarMac(mac);

  // Verificar si ya existe
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (normalizarMac(String(configDispositivos[i].mac)) == macNormalizada) {
      Serial.println("Dispositivo ya registrado: " + macNormalizada);
      if (bajaPendienteMQTT[i] || buscarSlotBajaPendiente(macNormalizada) != -1) {
        Serial.println("🔁 Re-alta detectada: se limpian banderas/cola de baja pendiente");
      }
      limpiarBajaPendientePorMac(macNormalizada);
      limpiarEstadoBaja(i);
      return true;
    }
  }

  // Buscar espacio libre
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (strlen(configDispositivos[i].mac) == 0) {
      macNormalizada.toCharArray(configDispositivos[i].mac, MAC_LEN + 1); // +1 para '\0'
      configDispositivos[i].nombre[0] = '\0';
      configDispositivos[i].activo = false;
      configDispositivos[i].tipoDispositivo = tipo;
      configDispositivos[i].litrosActuales = 0;
      configDispositivos[i].voltaje = 0;
      configDispositivos[i].temperatura = 0;
      configDispositivos[i].alturaConfig = 0;
      configDispositivos[i].litrosConfig = 0;
      configDispositivos[i].porcentaje = 0;
      ultimoMensajeLoRaDispositivo[i] = "";
      ultimaActualizacionLoRa[i] = millis();
      mostrarSinDatos[i] = false;
      ultimaSolicitudAlta[i] = 0;
      solicitudAltaEnviada[i] = false;
      bajaPendienteMQTT[i] = false;
      ultimaSolicitudBaja[i] = 0;
      inicioEsperaBaja[i] = 0;
      limpiarBajaPendientePorMac(macNormalizada);
      return guardarDispositivos();
    }
  }

  Serial.println("¡No hay espacio para más dispositivos!");
  return false;
}

void cargarDispositivos() {
    Serial.println("\n🔍 CARGANDO DISPOSITIVOS DESDE EEPROM...");
    
    // INICIALIZAR TODOS COMO VACÍOS
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        configDispositivos[i].mac[0] = '\0';
        configDispositivos[i].nombre[0] = '\0';
        configDispositivos[i].activo = false;
        configDispositivos[i].tipoDispositivo = 0;
        configDispositivos[i].litrosActuales = 0;
        configDispositivos[i].voltaje = 0;
        configDispositivos[i].temperatura = 0;
        configDispositivos[i].alturaConfig = 0;
        configDispositivos[i].litrosConfig = 0;
        configDispositivos[i].porcentaje = 0;
        ultimoMensajeLoRaDispositivo[i] = "";
        ultimaActualizacionLoRa[i] = 0;
        mostrarSinDatos[i] = false;
        ultimaSolicitudAlta[i] = 0;
        solicitudAltaEnviada[i] = false;
        bajaPendienteMQTT[i] = false;
        ultimaSolicitudBaja[i] = 0;
        inicioEsperaBaja[i] = 0;
    }

    EEPROM.begin(EEPROM_SIZE);
    int addr = CONFIG_DISPOSITIVOS_ADDR;
    
    // 0. Detectar versión del bloque
    byte version = EEPROM.read(addr);
    bool usaMensajesCompletos = (version == CONFIG_VERSION);
    if (usaMensajesCompletos) {
        addr++; // Saltar versión
    } else {
        Serial.println("⚠️ Formato previo detectado: se asumirá almacenamiento sin mensaje LoRa");
    }

    // 1. Leer contador (2 bytes)
    int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
    count = min(count, MAX_DISPOSITIVOS);

    Serial.printf("📊 Dispositivos encontrados en EEPROM: %d\n", count);

    // 2. Cargar cada dispositivo
    for (int i = 0; i < count; i++) {
        Serial.printf("\n--- Cargando dispositivo %d ---\n", i);
        
        // Leer MAC como string (18 bytes: 17 chars + null terminator)
        char mac[MAC_LEN + 1] = {0};
        for (int j = 0; j < MAC_LEN + 1; j++) {
            mac[j] = EEPROM.read(addr++);
        }
        
        Serial.printf("MAC leída de EEPROM: '%s'\n", mac);
        Serial.printf("Longitud MAC: %d\n", strlen(mac));

        // Validar que sea una MAC válida
        if (strlen(mac) == MAC_LEN && mac[2] == ':' && mac[5] == ':') {
            // COPIAR MAC AL ARREGLO
            strncpy(configDispositivos[i].mac, mac, MAC_LEN + 1);
            Serial.printf("✅ MAC copiada a configDispositivos[%d]: '%s'\n", i, configDispositivos[i].mac);
            
            // LEER NOMBRE (20 bytes)
            char nombre[20] = {0};
            for (int j = 0; j < 20; j++) {
                nombre[j] = EEPROM.read(addr++);
            }
            strncpy(configDispositivos[i].nombre, nombre, 20);
            Serial.printf("📝 Nombre leído: '%s'\n", configDispositivos[i].nombre);
            
            // LEER VALORES FLOAT (5 floats = 20 bytes)
            EEPROM.get(addr, configDispositivos[i].litrosActuales); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].voltaje); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].temperatura); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].alturaConfig); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].litrosConfig); addr += sizeof(float);
            
            Serial.printf("📊 Valores leídos: %.1fL, %.1fV, %.1f°C, %.1fcm, %.1fL\n",
                         configDispositivos[i].litrosActuales,
                         configDispositivos[i].voltaje,
                         configDispositivos[i].temperatura,
                         configDispositivos[i].alturaConfig,
                         configDispositivos[i].litrosConfig);
            
            // LEER PORCENTAJE (2 bytes)
            configDispositivos[i].porcentaje = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
            Serial.printf("📈 Porcentaje leído: %d%%\n", configDispositivos[i].porcentaje);
            
            // Leer tipo
            configDispositivos[i].tipoDispositivo = EEPROM.read(addr++);
            Serial.printf("🔧 Tipo leído: %d\n", configDispositivos[i].tipoDispositivo);
            
            // Leer estado activo
            byte activoByte = EEPROM.read(addr++);
            configDispositivos[i].activo = (activoByte == 1);
            Serial.printf("⚡ Activo leído: %d -> %s\n", activoByte, configDispositivos[i].activo ? "true" : "false");

            // Leer mensaje LoRa completo si el formato lo incluye
            if (usaMensajesCompletos) {
                if (addr >= EEPROM_SIZE) {
                    Serial.println("❌ Sin espacio para leer longitud de mensaje LoRa");
                    ultimoMensajeLoRaDispositivo[i] = "";
                } else {
                    uint16_t mensajeLen = EEPROM.read(addr++);
                    if (addr + mensajeLen <= EEPROM_SIZE) {
                        char mensajeBuf[256] = {0};
                        int bytesALeer = min<int>(mensajeLen, 255);
                        for (int j = 0; j < bytesALeer; j++) {
                            mensajeBuf[j] = EEPROM.read(addr++);
                        }
                        ultimoMensajeLoRaDispositivo[i] = String(mensajeBuf);
                        // Saltar bytes sobrantes si se truncó
                        if (mensajeLen > bytesALeer) {
                            addr += (mensajeLen - bytesALeer);
                        }
                        Serial.printf("🛰️ Mensaje LoRa cargado (%d bytes)\n", bytesALeer);
                    } else {
                        Serial.println("❌ Longitud de mensaje fuera de rango, se descarta");
                        addr = EEPROM_SIZE; // Evitar más lecturas inválidas
                        ultimoMensajeLoRaDispositivo[i] = "";
                    }
                }
            }

            // ASIGNAR NOMBRE POR DEFECTO SI ESTÁ VACÍO
            if (strlen(configDispositivos[i].nombre) == 0) {
                String nombreDefault = "Tanque " + String(i+1);
                nombreDefault.toCharArray(configDispositivos[i].nombre, 20);
                Serial.printf("⚠️  Nombre por defecto asignado: '%s'\n", configDispositivos[i].nombre);
            }
            
        } else {
            Serial.printf("❌ MAC inválida en posición %d\n", i);
            configDispositivos[i].mac[0] = '\0';
            configDispositivos[i].nombre[0] = '\0';
            configDispositivos[i].activo = false;
            
            // SALTAR TODOS LOS BYTES CORRECTAMENTE
            addr += 20; // Nombre (20 bytes)
            addr += sizeof(float) * 5; // 5 floats (20 bytes)
            addr += 4; // Porcentaje (2) + Tipo (1) + Activo (1) = 4 bytes
            if (usaMensajesCompletos && addr < EEPROM_SIZE) {
                uint16_t mensajeLen = EEPROM.read(addr++);
                addr += mensajeLen;
            }
            Serial.println("❌ Saltado dispositivo inválido");
        }
        
        // VERIFICACIÓN INMEDIATA
        Serial.printf("✅ VERIFICACIÓN: configDispositivos[%d].mac = '%s'\n", i, configDispositivos[i].mac);
        Serial.printf("✅ VERIFICACIÓN: configDispositivos[%d].nombre = '%s'\n", i, configDispositivos[i].nombre);
        Serial.printf("✅ VERIFICACIÓN: configDispositivos[%d].activo = %d\n", i, configDispositivos[i].activo);
    }
    
    EEPROM.end();
    
    // VERIFICACIÓN FINAL
    Serial.println("\n--- VERIFICACIÓN FINAL DEL ARREGLO ---");
    int dispositivosCargados = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("📍 configDispositivos[%d]: MAC='%s', Nombre='%s', Activo=%d, Tipo=%d, Porcentaje=%d%%, Litros=%.1fL\n", 
                         i, 
                         configDispositivos[i].mac, 
                         configDispositivos[i].nombre,
                         configDispositivos[i].activo, 
                         configDispositivos[i].tipoDispositivo,
                         configDispositivos[i].porcentaje,
                         configDispositivos[i].litrosActuales);
            dispositivosCargados++;
        }
    }
    Serial.printf("📊 Total dispositivos en arreglo: %d\n", dispositivosCargados);
    Serial.println("----------------------------------------");
}

void debugEEPROMReal() {
    Serial.println("\n=== DEBUG EEPROM REAL ===");
    EEPROM.begin(EEPROM_SIZE);
    
    int addr = CONFIG_DISPOSITIVOS_ADDR;
    
    // Leer contador
    int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
    Serial.printf("Contador: %d\n", count);
    
    // Leer primeros 3 dispositivos como están guardados
    for (int i = 0; i < min(3, count); i++) {
        Serial.printf("Dispositivo %d - Bytes crudos: ", i);
        
        // Leer los primeros 18 bytes (MAC como texto)
        for (int j = 0; j < MAC_LEN + 1; j++) {
            byte b = EEPROM.read(addr++);
            Serial.printf("%02X ", b);
            if (b >= 32 && b <= 126) {
                Serial.printf("('%c') ", b);
            } else {
                Serial.print("(?) ");
            }
        }
        Serial.println();
        
        // Saltar el resto de los datos del dispositivo
        addr += sizeof(float) * VALORES_POR_DISPOSITIVO + 2;
    }
    
    EEPROM.end();
    Serial.println("===========================");
}

//Funciones SETUP de innicializacion
   // 2. Inicialización segura de EEPROM
void inicializa_eeprom(){
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Error al inicializar EEPROM");
  } 
}

//Limpieza de fabrica EEPROM y WIFFI
void clearEEPROM() {
  Serial.println("🧹 INICIANDO BORRADO COMPLETO EEPROM...");
  
  // 1. Borrar EEPROM física
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF); // Escribir 0xFF es más confiable que 0x00
  }
  
  // 2. Borrar flags importantes
  EEPROM.write(0, 0); // Flag de inicialización
  EEPROM.write(MQTT_CONFIRMED_FLAG_ADDR, 0);
  EEPROM.write(MQTT_INITIAL_REQUEST_FLAG_ADDR, 0);
  
  // 3. Borrar contador de dispositivos en la posición específica
  int addr = CONFIG_DISPOSITIVOS_ADDR;
  EEPROM.write(addr++, 0); // High byte del contador
  EEPROM.write(addr++, 0); // Low byte del contador
  
  bool success = EEPROM.commit();
  EEPROM.end();

  if (!success) {
    Serial.println("❌ Error en commit de EEPROM");
  }
  
  // 4. Limpiar estructuras en RAM
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    configDispositivos[i].mac[0] = '\0';
    configDispositivos[i].nombre[0] = '\0';
    configDispositivos[i].activo = false;
    configDispositivos[i].tipoDispositivo = 0;
    ultimoMensajeLoRaDispositivo[i] = "";
    ultimaActualizacionLoRa[i] = 0;
    mostrarSinDatos[i] = false;
    ultimaSolicitudAlta[i] = 0;
    solicitudAltaEnviada[i] = false;
    bajaPendienteMQTT[i] = false;
    ultimaSolicitudBaja[i] = 0;
    inicioEsperaBaja[i] = 0;
  }

  // 5. Limpiar banderas de alta MQTT y reintentos
  mqttConfirmed = false;
  solicitudAltaInicialEnviada = false;
  lastConfirmationAttempt = 0;
  lastAltaPendienteCheck = 0;
  guardarMQTTConfirmationState(false);
  guardarSolicitudAltaInicialState(false);

  macMonitorFija = "";
  macMonitorFijaAnunciada = false;
  
  // 5. Limpiar redes WiFi en RAM
  for (int i = 0; i < MAX_NETWORKS; i++) {
    savedNetworks[i].ssid = "";
    savedNetworks[i].password = "";
    savedNetworks[i].active = false;
  }

  // 6. Limpiar userID
  userID = "";
  userEmail = "";
  userNombre = "";
  userTelefono = "";
  userPassword = "";
  userFlagRegistrado = false;
  
  Serial.println("✅ EEPROM Y MEMORIA RAM BORRADOS COMPLETAMENTE");
  
  // Verificación
  delay(1000);
  verificarEstadoConfigDispositivos();
}


void solicitarAltaMonitorMQTT() {
  // Reintentar la solicitud de alta mientras no exista confirmación
  // incluso si ya se envió previamente.
  if (mqttConfirmed) {
    // El monitor ya está dado de alta, no reenviar solicitud
    return;
  }

  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    if (millis() - lastConfirmationAttempt > confirmationRetryInterval) {
      lastConfirmationAttempt = millis();

      String correo = userEmail;
      if (correo.isEmpty()) {
        Serial.println("⚠️ [ALTA MONITOR01] Falta correo para solicitar alta");
        return;
      }

      asegurarMacMonitorFija("alta_monitor");

      // Bloquear si, por error, la MAC fija coincide con algún sensor registrado
      for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) > 0 &&
            macMonitorFija.equalsIgnoreCase(String(configDispositivos[i].mac))) {
          Serial.printf("⛔ Alta de monitor cancelada: la MAC fija %s coincide con un sensor en índice %d\n",
                        macMonitorFija.c_str(), i);
          return;
        }
      }

      String mensajeCompleto = macMonitorFija + "," + correo;
      if (!userFlagRegistrado) {
        if (userTelefono.isEmpty() || userNombre.isEmpty() || userPassword.isEmpty()) {
          Serial.println("⚠️ [ALTA MONITOR01] Falta nombre/teléfono/password para alta nueva");
          return;
        }
        mensajeCompleto += "," + userTelefono + "," + userNombre + "," + userPassword;
      }

      Serial.println("   Tópico TX: alta/0/solicitud/");
      Serial.println("   Payload TX: " + mensajeCompleto);
      Serial.println("   Espera RX: alta/0/confirmacion/ con 'MAC,registrado,correo,nombre,users_registro_id'");
      if (client.publish("alta/0/solicitud/",  mensajeCompleto.c_str())) {
        Serial.println("✅ [ALTA MONITOR01] Solicitud enviada correctamente");
        solicitudAltaInicialEnviada = true;
        guardarSolicitudAltaInicialState(true);
      } else {
        Serial.println("❌ [ALTA MONITOR01] Error al publicar solicitud de alta");
      }
    }
  }
}

bool solicitarBajaMonitorMQTT() {
  asegurarMacMonitorFija("baja_monitor");
  String mac = normalizarMac(macMonitorFija);

  if (mac.isEmpty()) {
    Serial.println("⚠️ [BAJA MONITOR01] Sin MAC fija para solicitar la baja");
    return false;
  }

  String registroId = userID;
  if (registroId.isEmpty()) {
    Serial.println("⚠️ [BAJA MONITOR01] Falta userID/users_registro_id para baja/0/solicitud");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    Serial.println("⚠️ [BAJA MONITOR01] MQTT desconectado; la solicitud de baja no se envió");
    return false;
  }

  String mensaje = mac + "," + registroId;
  Serial.println("🛰️ [BAJA MONITOR01] Solicitud -> MQTT");
  Serial.println("   Tópico TX: baja/0/solicitud/");
  Serial.println("   Payload TX: " + mensaje);
  Serial.println("   Espera RX: baja/0/confirmacion/ con 'MAC,eliminado' y limpieza total");

  if (client.publish("baja/0/solicitud/", mensaje.c_str())) {
    Serial.println("✅ Solicitud de baja enviada para el monitor");
    bajaMonitorEsperandoConfirmacion = true;
    inicioEsperaBajaMonitor = millis();
    ultimaMacMonitorBaja = mac;
    return true;
  }

  Serial.println("❌ No se pudo publicar la solicitud de baja del monitor");
  return false;
}

void solicitarAltaNuupMQTT(int indice, const String &mac) {
  if (indice < 0 || indice >= MAX_DISPOSITIVOS) {
    return;
  }

  // Evitar registrar el propio monitor como dispositivo (tipo 1)
  asegurarMacMonitorFija("alta_sensor");
  String macMonitor = macMonitorFija;

  if (mac.equalsIgnoreCase(macMonitor)) {
    Serial.printf("⏭️  Alta ignorada para la MAC del monitor (%s)\n", mac.c_str());
    return;
  }

  if (configDispositivos[indice].activo) {
    Serial.printf("ℹ️ Alta ya confirmada para %s, no se reenviará\n", mac.c_str());
    return;
  }

  String registroId = userID;
  if (registroId.isEmpty()) {
    Serial.println("⚠️ No se puede solicitar alta: falta users_registro_id");
    return;
  }

  if (!mqttConfirmed) {
    Serial.println("⚠️ No se puede solicitar alta: el monitor aún no está confirmado en MQTT");
    return;
  }

  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    return;
  }

  unsigned long ahora = millis();
  bool primeraSolicitud = !solicitudAltaEnviada[indice] && ultimaSolicitudAlta[indice] == 0;
  if (!primeraSolicitud && (ahora - ultimaSolicitudAlta[indice] < altaPendienteInterval)) {
    Serial.printf("⏳ Alta de %s ya solicitada; se reintentará en %lu ms\n", mac.c_str(),
                  altaPendienteInterval - (ahora - ultimaSolicitudAlta[indice]));
    return;
  }

  String nombre = String(configDispositivos[indice].nombre);
  nombre.trim();
  bool datosActualizados = false;
  if (nombre.isEmpty()) {
    nombre = mac;
    nombre.toCharArray(configDispositivos[indice].nombre, 20);
    datosActualizados = true;
  }

  byte tipo = configDispositivos[indice].tipoDispositivo;
  if (tipo == 0) {
    Serial.printf("⏭️  Alta cancelada: tipo de dispositivo en cero para %s (corregir registro)\n", mac.c_str());
    return;
  }

  if (datosActualizados) {
    if (guardarDispositivos()) {
      Serial.printf("💾 Datos de alta normalizados para %s (nombre='%s', tipo=%d)\n", mac.c_str(), nombre.c_str(), tipo);
    } else {
      Serial.println("❌ No se pudieron guardar los datos normalizados antes del alta MQTT");
    }
  }

  // Payload completo: MAC,UserID,Nombre,Altura,Litros,Tipo (tipo real registrado)
  String altura = String((int)configDispositivos[indice].alturaConfig);
  String litros = String((int)configDispositivos[indice].litrosConfig);
  String mensaje = mac + "," + registroId + "," + nombre + "," + altura + "," + litros + "," + String(tipo);

  if (solicitudAltaEnviada[indice] && mensaje == ultimoPayloadAltaMQTT[indice]) {
    Serial.printf("⏭️  Alta MQTT ya enviada con el mismo payload para %s, se evita duplicado tipo 0\n", mac.c_str());
    return;
  }

  Serial.println("🛰️ [ALTA NUUP01] Solicitud -> MQTT (ruta separada de monitor)");
  Serial.printf("   Tópico TX: alta/1/solicitud/\n");
  Serial.printf("   Payload TX: %s\n", mensaje.c_str());
  Serial.println("   Espera RX: alta/1/confirmacion/ con 'MAC,registrado' para activarlo");
  if (client.publish("alta/1/solicitud/", mensaje.c_str())) {
    ultimaSolicitudAlta[indice] = ahora;
    solicitudAltaEnviada[indice] = true;
    ultimoPayloadAltaMQTT[indice] = mensaje;
    Serial.printf("✅ Solicitud de alta enviada para %s\n", mac.c_str());
  } else {
    Serial.printf("❌ Error al solicitar alta para %s\n", mac.c_str());
  }
}

int buscarSlotBajaPendiente(const String &mac) {
  String objetivo = normalizarMac(mac);
  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    if (!bajasPendientesActivas[i]) continue;
    if (objetivo.equalsIgnoreCase(String(bajasPendientesMac[i]))) {
      return i;
    }
  }
  return -1;
}

int buscarSlotLibreBajaPendiente() {
  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    if (!bajasPendientesActivas[i]) return i;
  }
  return -1;
}

void guardarBajasPendientesEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  int addr = BAJAS_PENDIENTES_ADDR;

  byte count = 0;
  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    if (bajasPendientesActivas[i]) count++;
  }

  EEPROM.write(addr++, count);

  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    for (int j = 0; j < MAC_LEN + 1; j++) {
      char c = (bajasPendientesActivas[i]) ? bajasPendientesMac[i][j] : '\0';
      EEPROM.write(addr++, c);
      if (addr >= EEPROM_SIZE) break;
    }
    if (addr >= EEPROM_SIZE) break;
  }

  EEPROM.commit();
  EEPROM.end();
  Serial.printf("💾 Bajas pendientes guardadas (%d entradas activas)\n", count);
}

void cargarBajasPendientesEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  int addr = BAJAS_PENDIENTES_ADDR;

  byte count = EEPROM.read(addr++);
  if (count > MAX_BAJAS_PERSISTENTES) {
    count = MAX_BAJAS_PERSISTENTES;
  }

  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    char macBuf[MAC_LEN + 1] = {0};
    for (int j = 0; j < MAC_LEN + 1 && addr < EEPROM_SIZE; j++) {
      macBuf[j] = EEPROM.read(addr++);
    }

    if (i < count && strlen(macBuf) == MAC_LEN) {
      strncpy(bajasPendientesMac[i], macBuf, MAC_LEN + 1);
      bajasPendientesActivas[i] = true;
      Serial.printf("⏳ Baja pendiente cargada: %s\n", bajasPendientesMac[i]);
    } else {
      bajasPendientesMac[i][0] = '\0';
      bajasPendientesActivas[i] = false;
    }
    bajasPendientesUltimoIntento[i] = 0;
    bajasPendientesInicioEspera[i] = 0;
  }

  EEPROM.end();
  imprimirEstadoBajasPendientes("Arranque (EEPROM)");
}

void registrarBajaPendientePersistente(const String &mac) {
  String macNormalizada = normalizarMac(mac);
  if (macNormalizada.length() != MAC_LEN) return;

  int existente = buscarSlotBajaPendiente(macNormalizada);
  int slot = (existente != -1) ? existente : buscarSlotLibreBajaPendiente();
  if (slot == -1) {
    Serial.println("⚠️ No hay espacio para encolar más bajas pendientes en EEPROM");
    return;
  }

  macNormalizada.toCharArray(bajasPendientesMac[slot], MAC_LEN + 1);
  bajasPendientesActivas[slot] = true;
  bajasPendientesUltimoIntento[slot] = 0;
  bajasPendientesInicioEspera[slot] = 0;
  Serial.printf("🗂️ Baja pendiente encolada (slot %d) para reintento al conectar MQTT: %s\n",
                slot,
                bajasPendientesMac[slot]);
  guardarBajasPendientesEEPROM();
  imprimirEstadoBajasPendientes("Encolada");
}

void limpiarBajaPendientePorMac(const String &mac) {
  int slot = buscarSlotBajaPendiente(mac);
  if (slot == -1) return;

  bajasPendientesActivas[slot] = false;
  bajasPendientesMac[slot][0] = '\0';
  bajasPendientesUltimoIntento[slot] = 0;
  bajasPendientesInicioEspera[slot] = 0;
  guardarBajasPendientesEEPROM();
  Serial.printf("✅ Baja pendiente confirmada/cancelada para %s\n", mac.c_str());
}

void limpiarEstadoBaja(int indice) {
  if (indice < 0 || indice >= MAX_DISPOSITIVOS) return;

  bajaPendienteMQTT[indice] = false;
  ultimaSolicitudBaja[indice] = 0;
  inicioEsperaBaja[indice] = 0;
}

bool publicarSolicitudBaja(int indice, const String &mac) {
  if (indice < 0 || indice >= MAX_DISPOSITIVOS) return false;

  // ⭐ VALIDACIÓN: No permitir bajas de tipo 0 (monitor)
  byte tipoDispositivo = configDispositivos[indice].tipoDispositivo;
  if (tipoDispositivo == 0) {
    Serial.println("❌ ERROR: No se permiten bajas de dispositivos tipo 0 (monitor) por este flujo");
    Serial.println("   Use solicitarBajaMonitorMQTT() para dar de baja el monitor");
    return false;
  }

  String registroId = userID;
  if (registroId.isEmpty()) {
    Serial.println("⚠️ No se puede solicitar baja: falta users_registro_id");
    return false;
  }

  if (!mqttConfirmed) {
    Serial.println("⚠️ No se puede solicitar baja: el monitor no está confirmado en MQTT");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    Serial.println("⚠️ No se puede solicitar baja: MQTT desconectado");
    return false;
  }

  // Construir topic dinámicamente según el tipo de dispositivo
  String topicBaja = "baja/" + String(tipoDispositivo) + "/solicitud/";
  String mensaje = mac + "," + registroId;
  
  Serial.println("🛰️ [BAJA DISPOSITIVO] Solicitud -> MQTT");
  Serial.println("   Tipo: " + String(tipoDispositivo));
  Serial.println("   Tópico TX: " + topicBaja);
  Serial.println("   Payload TX: " + mensaje);
  Serial.println("   Espera RX: baja/" + String(tipoDispositivo) + "/confirmacion/ con 'MAC,eliminado'");
  
  if (client.publish(topicBaja.c_str(), mensaje.c_str())) {
    ultimaSolicitudBaja[indice] = millis();
    inicioEsperaBaja[indice] = (inicioEsperaBaja[indice] == 0) ? ultimaSolicitudBaja[indice] : inicioEsperaBaja[indice];
    bajaPendienteMQTT[indice] = true;
    Serial.printf("🗑️ Solicitud de baja MQTT enviada para %s (tipo %d)\n", mac.c_str(), tipoDispositivo);
    return true;
  }

  Serial.printf("❌ Error al solicitar baja MQTT para %s\n", mac.c_str());
  return false;
}

bool publicarSolicitudBajaPersistente(const String &mac, int slot) {
  if (mac.length() != MAC_LEN) return false;

  Serial.printf("🔁 Revisión de baja pendiente (slot %d) para %s\n", slot, mac.c_str());

  String registroId = userID;
  if (registroId.isEmpty()) {
    Serial.println("⚠️ No se puede solicitar baja pendiente: falta users_registro_id");
    return false;
  }

  if (!mqttConfirmed) {
    Serial.println("⚠️ No se puede solicitar baja pendiente: el monitor no está confirmado en MQTT");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    Serial.println("⚠️ No se puede solicitar baja pendiente: MQTT desconectado");
    return false;
  }

  // Buscar el tipo de dispositivo (podría ya no estar en el arreglo)
  byte tipoDispositivo = 1; // Valor por defecto si no se encuentra
  int indice = obtenerIndiceDispositivo(mac);
  if (indice >= 0) {
    tipoDispositivo = configDispositivos[indice].tipoDispositivo;
    // No permitir tipo 0
    if (tipoDispositivo == 0) {
      Serial.println("❌ ERROR: Baja pendiente de tipo 0 (monitor) cancelada");
      limpiarBajaPendientePorMac(mac);
      return false;
    }
  } else {
    Serial.println("   Dispositivo ya no existe en arreglo, asumiendo tipo 1");
  }

  String topicBaja = "baja/" + String(tipoDispositivo) + "/solicitud/";
  String mensaje = mac + "," + registroId;
  
  Serial.println("🛰️ [BAJA PENDIENTE] Reenvío -> MQTT");
  Serial.println("   Tipo: " + String(tipoDispositivo));
  Serial.println("   Tópico TX: " + topicBaja);
  Serial.println("   Payload TX: " + mensaje);
  Serial.println("   Espera RX: baja/" + String(tipoDispositivo) + "/confirmacion/ con 'MAC,eliminado'");

  if (client.publish(topicBaja.c_str(), mensaje.c_str())) {
    bajasPendientesUltimoIntento[slot] = millis();
    bajasPendientesInicioEspera[slot] = (bajasPendientesInicioEspera[slot] == 0)
                                           ? bajasPendientesUltimoIntento[slot]
                                           : bajasPendientesInicioEspera[slot];
    Serial.printf("🗑️ Solicitud de baja reintentada para %s (tipo %d)\n", mac.c_str(), tipoDispositivo);
    return true;
  }

  Serial.printf("❌ Error al reenviar baja pendiente para %s\n", mac.c_str());
  return false;
}

bool iniciarBajaDispositivo(const String &mac, const char* origen, bool forzarMqtt) {
  String macNormalizada = normalizarMac(mac);
  uint32_t consecutivoLocal = 0;
  if (strcmp(origen, "Portal AP") == 0) {
    consecutivoLocal = ++consecutivoBajaPortal;
  }
  Serial.printf("📝 Solicitud de baja (%s) recibida para %s%s\n",
                origen,
                macNormalizada.c_str(),
                consecutivoLocal > 0 ? " (portal AP con consecutivo)" : "");
  if (consecutivoLocal > 0) {
    Serial.printf("   🔢 Consecutivo baja portal: #%lu\n", consecutivoLocal);
  }

  int indice = obtenerIndiceDispositivo(macNormalizada);

  if (indice < 0) {
    Serial.printf("❌ No se encontró dispositivo para baja: %s\n", macNormalizada.c_str());
    return false;
  }

  ConfigDispositivo *config = &configDispositivos[indice];
  Serial.printf("   Estado previo -> activo:%s | baja_RAM:%s\n",
                config->activo ? "SI" : "NO",
                bajaPendienteMQTT[indice] ? "SI" : "NO");

  // Resetear cualquier intento de alta previo para evitar estados fantasma
  solicitudAltaEnviada[indice] = false;
  ultimaSolicitudAlta[indice] = 0;

  // Si nunca estuvo activo en MQTT, eliminar inmediatamente
  if (!config->activo && !forzarMqtt) {
    Serial.printf("ℹ️ Dispositivo %s sin alta MQTT previa, se elimina localmente\n", macNormalizada.c_str());
    bool eliminadoLocal = eliminarDispositivo(macNormalizada);
    imprimirEstadoBajasPendientes("Baja local sin alta MQTT");
    return eliminadoLocal;
  }
  if (!config->activo && forzarMqtt) {
    Serial.printf("🔁 Baja solicitada desde portal AP: se intentará MQTT aunque no haya alta previa (%s)\n",
                  macNormalizada.c_str());
  }

  // Marcar como pendiente de baja y pausar publicaciones mientras enviamos la solicitud
  config->activo = false;
  bajaPendienteMQTT[indice] = true;
  registrarBajaPendientePersistente(macNormalizada);
  imprimirEstadoBajasPendientes("Encolada desde baja activa");

  bool enviada = publicarSolicitudBaja(indice, macNormalizada);
  if (!enviada) {
    Serial.println("⚠️ Baja encolada para reintento cuando MQTT se reconecte");
  } else {
    Serial.println("⏳ Esperando confirmación de baja hasta 30s");
  }

  // Eliminar inmediatamente del arreglo/EEPROM para que no aparezca más como registrado
  bool eliminado = eliminarDispositivo(macNormalizada);
  if (!eliminado) {
    Serial.printf("⚠️ No se pudo eliminar %s de EEPROM tras solicitar baja\n", macNormalizada.c_str());
  }

  imprimirEstadoBajasPendientes("Tras eliminar de RAM/EEPROM");
  return eliminado || enviada;
}

void procesarBajasPendientes() {
  int pendientesRam = contarBajasPendientesRAM();
  int pendientesEEPROM = contarBajasPendientesPersistentes();
  if (pendientesRam == 0 && pendientesEEPROM == 0) return;

  static unsigned long ultimoAvisoBajas = 0;
  unsigned long ahora = millis();
  const unsigned long intervaloAviso = 5000;

  if (WiFi.status() != WL_CONNECTED) {
    if (ahora - ultimoAvisoBajas > intervaloAviso) {
      Serial.printf("⏸️  Bajas pendientes (RAM:%d/EEPROM:%d) en espera: WiFi desconectado\n", pendientesRam, pendientesEEPROM);
      ultimoAvisoBajas = ahora;
    }
    return;
  }

  if (!client.connected()) {
    if (ahora - ultimoAvisoBajas > intervaloAviso) {
      Serial.printf("⏸️  Bajas pendientes (RAM:%d/EEPROM:%d) en espera: MQTT desconectado\n", pendientesRam, pendientesEEPROM);
      ultimoAvisoBajas = ahora;
    }
    return;
  }

  if (!mqttConfirmed) {
    if (ahora - ultimoAvisoBajas > intervaloAviso) {
      Serial.printf("⏸️  Bajas pendientes (RAM:%d/EEPROM:%d) en espera: monitor sin confirmar en MQTT\n", pendientesRam, pendientesEEPROM);
      ultimoAvisoBajas = ahora;
    }
    return;
  }

  if (userID.isEmpty()) {
    if (ahora - ultimoAvisoBajas > intervaloAviso) {
      Serial.printf("⏸️  Bajas pendientes (RAM:%d/EEPROM:%d) en espera: falta users_registro_id\n", pendientesRam, pendientesEEPROM);
      ultimoAvisoBajas = ahora;
    }
    return;
  }

  imprimirEstadoBajasPendientes("Reenvío");

  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (!bajaPendienteMQTT[i]) continue;

    if (inicioEsperaBaja[i] != 0 && (ahora - inicioEsperaBaja[i]) < bajaConfirmTimeout) {
      continue;
    }

    if (ultimaSolicitudBaja[i] == 0 || (ahora - ultimaSolicitudBaja[i]) >= bajaPendienteInterval) {
      publicarSolicitudBaja(i, normalizarMac(String(configDispositivos[i].mac)));
    }
  }

  for (int i = 0; i < MAX_BAJAS_PERSISTENTES; i++) {
    if (!bajasPendientesActivas[i]) continue;

    String mac = String(bajasPendientesMac[i]);
    if (mac.length() != MAC_LEN) continue;

    if (bajasPendientesInicioEspera[i] != 0 && (ahora - bajasPendientesInicioEspera[i]) < bajaConfirmTimeout) {
      continue;
    }

    if (bajasPendientesUltimoIntento[i] == 0 || (ahora - bajasPendientesUltimoIntento[i]) >= bajaPendienteInterval) {
      publicarSolicitudBajaPersistente(mac, i);
    }
  }
}


bool loadMQTTConfirmationState() {
  EEPROM.begin(EEPROM_SIZE);
  bool flag = EEPROM.read(MQTT_CONFIRMED_FLAG_ADDR) == 1;
  EEPROM.end();
  return flag;
}

void guardarMQTTConfirmationState(bool estado) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(MQTT_CONFIRMED_FLAG_ADDR, estado ? 1 : 0);
  EEPROM.commit();
  EEPROM.end();
}

bool loadSolicitudAltaInicialState() {
  EEPROM.begin(EEPROM_SIZE);
  bool flag = EEPROM.read(MQTT_INITIAL_REQUEST_FLAG_ADDR) == 1;
  EEPROM.end();
  return flag;
}

void guardarSolicitudAltaInicialState(bool estado) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(MQTT_INITIAL_REQUEST_FLAG_ADDR, estado ? 1 : 0);
  EEPROM.commit();
  EEPROM.end();
}

// Fijar la MAC del monitor una sola vez y evitar que se imprima en bucle
void asegurarMacMonitorFija(const char* motivo) {
  if (macMonitorFija.isEmpty()) {
    macMonitorFija = WiFi.macAddress();
    macMonitorFija.replace("-", ":");
    macMonitorFijaAnunciada = false; // permitir un anuncio fresco cuando se obtenga de cero
  }

  if (!macMonitorFija.isEmpty() && !macMonitorFijaAnunciada) {
    Serial.printf("🔒 MAC fija del monitor: %s", macMonitorFija.c_str());
    if (motivo && strlen(motivo) > 0) {
      Serial.printf(" (origen: %s)", motivo);
    }
    Serial.println();
    macMonitorFijaAnunciada = true;
  }
}

void activarDispositivosTrasConfirmacion() {
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (strlen(configDispositivos[i].mac) > 0 && !configDispositivos[i].activo) {
      solicitarAltaNuupMQTT(i, String(configDispositivos[i].mac));
    }
  }
}

void procesarAltasPendientes() {
  if (!mqttConfirmed) return;
  if (WiFi.status() != WL_CONNECTED || !client.connected()) return;

  unsigned long ahora = millis();
  if (lastAltaPendienteCheck != 0 && (ahora - lastAltaPendienteCheck < altaPendienteInterval)) return;

  lastAltaPendienteCheck = ahora;

  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (strlen(configDispositivos[i].mac) > 0 && !configDispositivos[i].activo) {
      solicitarAltaNuupMQTT(i, String(configDispositivos[i].mac));
    }
  }
}

// Obtener configuración de un dispositivo por MAC
ConfigDispositivo* getConfigDispositivo(const String &mac) {
  String macNormalizada = normalizarMac(mac);
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (normalizarMac(String(configDispositivos[i].mac)) == macNormalizada) {
      return &configDispositivos[i];
    }
  }
  return nullptr;
}

int obtenerIndiceDispositivo(const String &mac) {
  String macNormalizada = normalizarMac(mac);
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (normalizarMac(String(configDispositivos[i].mac)) == macNormalizada) {
      return i;
    }
  }
  return -1;
}

bool macPresenteEnEEPROM(const String &mac) {
  String buscada = normalizarMac(mac);

  EEPROM.begin(EEPROM_SIZE);
  int addr = CONFIG_DISPOSITIVOS_ADDR;

  byte version = EEPROM.read(addr++);
  int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);

  (void)version; // Solo se lee para avanzar el puntero

  bool encontrada = false;

  for (int i = 0; i < count && addr < EEPROM_SIZE; i++) {
    char macLeida[MAC_LEN + 1] = {0};

    for (int j = 0; j < MAC_LEN && addr < EEPROM_SIZE; j++) {
      macLeida[j] = EEPROM.read(addr++);
    }
    macLeida[MAC_LEN] = '\0';

    // Saltar nombre (20 bytes)
    addr += 20;

    // Saltar floats de telemetría/config (5 valores)
    addr += sizeof(float) * 5;

    // Porcentaje (uint16), tipo (byte), activo (byte)
    addr += 2 + 1 + 1;

    // Mensaje LoRa almacenado (longitud + contenido)
    if (addr + 2 > EEPROM_SIZE) {
      break;
    }
    uint16_t mensajeLen = (EEPROM.read(addr++) << 8);
    mensajeLen |= EEPROM.read(addr++);
    addr += mensajeLen;

    if (addr > EEPROM_SIZE) {
      break;
    }

    if (buscada == normalizarMac(String(macLeida))) {
      encontrada = true;
      break;
    }
  }

  EEPROM.end();
  return encontrada;
}

// Imprimir configuración de un dispositivo
void imprimirConfigDispositivo(const String &mac) {
    ConfigDispositivo* config = getConfigDispositivo(mac);
    if (config) {
        Serial.printf("Configuración para MAC %s:\n", mac.c_str());
        Serial.printf("Nombre: %s\n", config->nombre);
        Serial.printf("Tipo: %d\n", config->tipoDispositivo);
        Serial.printf("Litros Actuales: %.2f\n", config->litrosActuales);
        Serial.printf("Voltaje: %.2f\n", config->voltaje);
        Serial.printf("Temperatura: %.2f\n", config->temperatura);
        Serial.printf("Altura Config: %.2f\n", config->alturaConfig);
        Serial.printf("Litros Config: %.2f\n", config->litrosConfig);
        Serial.printf("Porcentaje: %d%%\n", config->porcentaje);
        Serial.printf("Activo: %s\n", config->activo ? "SI" : "NO");
    } else {
        Serial.println("Dispositivo no encontrado");
    }
}

String normalizarPayloadParaMQTT(const String &payload) {
  int primerDelimitador = payload.indexOf(',');
  if (primerDelimitador <= 0) {
    return payload;
  }

  String tipo = payload.substring(0, primerDelimitador);
  tipo.trim();

  while (tipo.length() > 0 && tipo.length() < 3) {
    tipo = "0" + tipo;
  }

  return tipo + payload.substring(primerDelimitador);
}

String construirPayloadEEPROMParaMQTT(const ConfigDispositivo &config) {
  String payload = String(config.tipoDispositivo) + "," + String(config.mac) + "," +
                   String(config.litrosActuales, 1) + "," + String(config.voltaje, 2) + "," +
                   String(config.temperatura, 1) + "," + String(config.alturaConfig, 1) + "," +
                   String(config.litrosConfig, 1) + "," + String(config.nombre);

  return normalizarPayloadParaMQTT(payload);
}

void iniciarLoRaConReintentos() {
  int intentos = 0;
  bool estadoLED = false;

  Serial.println("📡 INICIANDO CONFIGURACIÓN LoRa...");
  
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setSPI(loraSPI);
  LoRa.setPins(LORA_SS, -1, -1);

  while (!LoRa.begin(433E6)) {
    Serial.printf("❌ Error al iniciar LoRa. Intento %d/10\n", intentos + 1);
    estadoLED = !estadoLED;
    intentos++;
    if (intentos >= 10) {
      Serial.println("🚨 ERROR CRÍTICO: No se pudo inicializar LoRa después de 10 intentos");
      return;
    }
    delay(3000);
  }

  // ⭐ CONFIGURACIÓN EXPLÍCITA
  LoRa.setTxPower(20);
  potenciaLoRaMonitorDbm = 20;
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x12);
  LoRa.setPreambleLength(8);
  
  Serial.println("✅ LoRa inicializado correctamente!");
  Serial.println("📊 Configuración aplicada:");
  Serial.println("   - Frecuencia: 433 MHz");
  Serial.println("   - Potencia: 20 dBm");
  Serial.println("   - SF: 12");
  Serial.println("   - BW: 125 kHz");
  Serial.println("   - CR: 4/8");
  Serial.println("   - Sync Word: 0x12");
  Serial.println("   - Preámbulo: 8");
  Serial.println("📐 Alineación esperada con Nuup01: Frec 433 MHz | SF12 | BW 125 kHz | CR 4/8 | Sync 0x12 | Preámbulo 8 | RX inmediato tras configurar");
  Serial.println("ℹ️  Potencia recomendada: 17-20 dBm para máxima distancia; bajar a 10-14 dBm en pruebas cercanas para evitar saturar el receptor.");
  Serial.println("ℹ️  SF/BW/CR arriba explican alcance vs velocidad: mantener SF12/BW125/CR4/8 para robustez mientras depuramos confirmaciones.");
  Serial.println("ℹ️  Confirmaciones configuradas: 3 envíos separados 100ms (se puede ajustar en constantes REPETICIONES_CONFIRMACION_LORA / INTERVALO_CONFIRMACION_LORA_MS).");

  // Escucha inmediata tras la configuración para no perder el primer paquete del NUUP01
  reanudarRecepcionLoRa("inicio tras configuración");
}

void reanudarRecepcionLoRa(const char *motivo) {
  LoRa.receive();
  if (!loraEnEscucha) {
    Serial.printf("👂 LoRa en modo escucha continua (%s)\n", motivo);
  } else {
    Serial.printf("🔁 LoRa vuelve a modo escucha (%s)\n", motivo);
  }
  loraEnEscucha = true;
}

void imprimirDetalleParametrosLoRa() {
  Serial.println("📡 Parámetros LoRa activos y explicación rápida:");
  Serial.printf("   - Potencia TX: %u dBm (mín 2, máx 20). Recomendado 17-20 dBm para máxima distancia; 10-14 dBm para pruebas cercanas.\n",
                potenciaLoRaMonitorDbm);
  Serial.println("   - SF12 = más alcance y sensibilidad, pero menor velocidad. (Rango: SF7-SF12)");
  Serial.println("   - BW 125 kHz = equilibrio entre alcance y velocidad. (Rango: 7.8 kHz a 500 kHz)");
  Serial.println("   - CR 4/8 = robustez alta contra errores. (Rango: 4/5 a 4/8)");
  Serial.println("   - SyncWord 0x12 = red privada NUUP (evita interferencias con redes públicas)");
  Serial.println("   - Preámbulo 8 = tiempo de sincronía; subir a 12-16 en entornos con ruido o gateways lejanos.");
  Serial.println("   - Confirmaciones inmediatas: 3 envíos separados 100ms para asegurar recepción sin perder la escucha.");
}

void imprimirResumenLoRa(const char *motivo) {
  Serial.println("\n📝 RESUMEN LoRa en español (modo escucha continuo)");
  Serial.printf("   - Motivo del resumen: %s\n", motivo);
  Serial.println("   - Núcleo dedicado desactivado: toda la escucha corre en el loop principal para priorizar sensores.");
  Serial.printf("   - Confirmaciones duplicadas: %u veces cada %lums (ajustables en REPETICIONES_CONFIRMACION_LORA / INTERVALO_CONFIRMACION_LORA_MS).\n",
                REPETICIONES_CONFIRMACION_LORA,
                INTERVALO_CONFIRMACION_LORA_MS);
  Serial.printf("   - Consecutivo de confirmaciones TX (reinicia al encender): %lu\n", consecutivoConfirmacionesLoRa);
  Serial.println("   - La radio permanece en receive() salvo breves pausas para imprimir o enviar.");
  imprimirDetalleParametrosLoRa();
  Serial.println();
}

void Reintentar_Wiffi(){
    // Reintentar conexión periódicamente
    if (millis() - lastReconnectAttempt > reconnectInterval) {
      Serial.println("Reintentando conexión a redes guardadas...");
      lastReconnectAttempt = millis();
      attemptReconnectToAllNetworks();
    }
}

bool esMacValida(const String &mac) {
  if (mac.length() != 17) {
    return false;
  }

  for (int i = 0; i < 17; i++) {
    char c = mac.charAt(i);
    if ((i + 1) % 3 == 0) {
      if (c != ':') return false;
    } else {
      bool esHex = (c >= '0' && c <= '9') ||
                   (c >= 'A' && c <= 'F') ||
                   (c >= 'a' && c <= 'f');
      if (!esHex) return false;
    }
  }

  return true;
}

String normalizarMac(const String &macRaw) {
  String mac = macRaw;
  mac.trim();
  mac.toUpperCase();
  return mac;
}

String extraerMacDeMensajeLoRa(const String &mensaje) {
  int firstComma = mensaje.indexOf(',');
  int secondComma = mensaje.indexOf(',', firstComma + 1);
  if (firstComma == -1 || secondComma == -1) {
    return "";
  }
  return normalizarMac(mensaje.substring(firstComma + 1, secondComma));
}

ConfigDispositivo obtenerConfigPorMac(const String &macBuscada) {
  String buscada = normalizarMac(macBuscada);
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (normalizarMac(String(configDispositivos[i].mac)) == buscada) {
      return configDispositivos[i];
    }
  }
  return ConfigDispositivo{};
}

bool mensajeLoRaTieneDatos(const String &mensaje) {
  int commaCount = 0;
  for (int pos = 0; pos < mensaje.length(); pos++) {
    if (mensaje.charAt(pos) == ',') {
      commaCount++;
    }
  }

  if (commaCount < 6) {
    Serial.printf("❌ Mensaje LoRa con campos insuficientes (comas=%d)\n", commaCount);
    return false;
  }

  return true;
}

bool datosLoRaCoincidenConBroker(const String &mensaje, int indice) {
  int commas[8];
  int commaCount = 0;

  for (int pos = 0; pos < mensaje.length() && commaCount < 8; pos++) {
    if (mensaje.charAt(pos) == ',') {
      commas[commaCount] = pos;
      commaCount++;
    }
  }

  if (commaCount < 7) {
    Serial.printf("⚠️  No hay suficientes campos para validar modificación (comas=%d)\n", commaCount);
    return false;
  }

  String litrosActualesStr = mensaje.substring(commas[1] + 1, commas[2]);
  String alturaConfigStr = mensaje.substring(commas[4] + 1, commas[5]);
  String litrosConfigStr = mensaje.substring(commas[5] + 1, commas[6]);
  String nombreExtraido = mensaje.substring(commas[6] + 1, commas[7]);
  nombreExtraido.trim();

  float litrosRecibidos = litrosActualesStr.toFloat();
  float alturaRecibida = alturaConfigStr.toFloat();
  float capacidadRecibida = litrosConfigStr.toFloat();
  String nombreRecibido = nombreExtraido.length() > 0 ? nombreExtraido : String(configDispositivos[indice].nombre);

  const ConfigDispositivo &configEEPROM = configDispositivos[indice];
  float alturaObjetivo = configEEPROM.alturaConfig;
  float capacidadObjetivo = configEEPROM.litrosConfig;
  String aliasObjetivo = String(configEEPROM.nombre);

  bool coincideAltura = fabsf(alturaRecibida - alturaObjetivo) < 0.1f;
  bool coincideCapacidad = fabsf(capacidadRecibida - capacidadObjetivo) < 0.1f;
  bool coincideNombre = aliasObjetivo.length() == 0 ? true : (nombreRecibido == aliasObjetivo);

  Serial.println("   🧭 Comparando datos solicitados vs recibidos (LoRa)");
  Serial.printf("      Objetivo altura/capacidad/nombre (EEPROM): %.1f / %.1f / %s\n",
                alturaObjetivo,
                capacidadObjetivo,
                aliasObjetivo.c_str());
  Serial.printf("      Recibido altura/capacidad/nombre: %.1f / %.1f / %s\n",
                alturaRecibida,
                capacidadRecibida,
                nombreRecibido.c_str());
  Serial.printf("      ℹ️ Litros recibidos (solo lectura, no bloquea validación): %.1f L\n",
                litrosRecibidos);

  return coincideAltura && coincideCapacidad && coincideNombre;
}

struct DatosConfirmacionLoRa {
  String nombre;
  float alturaConfig;
  float litrosConfig;
  bool origenMensaje;
};

DatosConfirmacionLoRa construirConfirmacionLoRa(const String &mensaje, const ConfigDispositivo &config, bool preferirEEPROM) {
  DatosConfirmacionLoRa datos{String(config.nombre), config.alturaConfig, config.litrosConfig, false};

  if (preferirEEPROM) {
    return datos;
  }

  int commas[8];
  int commaCount = 0;

  for (int pos = 0; pos < mensaje.length() && commaCount < 8; pos++) {
    if (mensaje.charAt(pos) == ',') {
      commas[commaCount] = pos;
      commaCount++;
    }
  }

  if (commaCount >= 6) {
    String litrosConfigStr = mensaje.substring(commas[5] + 1, commas[6]);
    String alturaConfigStr = mensaje.substring(commas[4] + 1, commas[5]);

    datos.litrosConfig = litrosConfigStr.toFloat();
    datos.alturaConfig = alturaConfigStr.toFloat();
    datos.origenMensaje = true;

    if (commaCount >= 7) {
      String nombreExtraido = mensaje.substring(commas[6] + 1, commas[7]);
      nombreExtraido.trim();
      if (nombreExtraido.length() > 0) {
        datos.nombre = nombreExtraido;
      }
    }
  }

  return datos;
}

bool enviarPaqueteLoRa(const String &descripcion,
                       const String &macDestino,
                       const String &payload,
                       uint8_t repeticiones = REPETICIONES_CONFIRMACION_LORA,
                       unsigned long intervaloMs = INTERVALO_CONFIRMACION_LORA_MS) {
  bool exito = false;
  uint32_t consecutivoEnvio = ++consecutivoConfirmacionesLoRa;

  Serial.printf("🚀 Encolando %s para %s con %u repeticiones cada %lums\n",
                descripcion.c_str(),
                macDestino.c_str(),
                repeticiones,
                intervaloMs);
  Serial.printf("   ↳ TX %u dBm = potencia de salida | Último RSSI %d dBm = señal recibida | SNR %.1f dB = relación señal/ruido\n",
                potenciaLoRaMonitorDbm,
                ultimoRssiLoRaRx,
                ultimoSnrLoRaRx);
  Serial.printf("   ↳ Consecutivo de confirmación: #%lu (se reinicia al arrancar)\n", consecutivoEnvio);

  for (uint8_t intento = 1; intento <= repeticiones; intento++) {
    Serial.printf("📡 (%u/%u) Enviando %s a %s | Payload: %s\n",
                  intento,
                  repeticiones,
                  descripcion.c_str(),
                  macDestino.c_str(),
                  payload.c_str());

    LoRa.setTxPower(potenciaLoRaMonitorDbm);
    LoRa.idle();
    loraEnEscucha = false;

    int beginResult = LoRa.beginPacket();
    if (!beginResult) {
      Serial.println("❌ No se pudo iniciar el paquete LoRa para envío de confirmación");
    } else {
      LoRa.print(payload);

      // Envío bloqueante para garantizar finalización sin depender de APIs privadas
      int endResult = LoRa.endPacket();
      if (!endResult) {
        Serial.println("❌ Falló el envío LoRa (endPacket retornó 0)");
      } else {
        Serial.println("✅ Paquete LoRa enviado correctamente");
        exito = true;
      }
    }

    if (intento < repeticiones) {
      vTaskDelay(pdMS_TO_TICKS(intervaloMs));
    }
  }

  Serial.println("🔁 Se reanuda recepción inmediata tras respuestas duplicadas");
  reanudarRecepcionLoRa("confirmaciones enviadas");
  return exito;
}

void LORA_bidireccional_borrar() {
  static unsigned long ultimaRecepcionDevMs = 0;
  unsigned long marcaRecepcion = millis();

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  LoRaProcessingGuard guard(loraProcesando);

  String recibido = "";
  while (LoRa.available()) {
    recibido += (char)LoRa.read();
  }
  recibido.trim();

  ultimoRssiLoRaRx = LoRa.packetRssi();
  ultimoSnrLoRaRx = LoRa.packetSnr();

  unsigned long deltaRecepcion = ultimaRecepcionDevMs == 0 ? 0 : (marcaRecepcion - ultimaRecepcionDevMs);
  ultimaRecepcionDevMs = marcaRecepcion;

  Serial.println("\n🧪 [DEV] BIDIRECCIONAL - paquete entrante");
  Serial.printf("📥 Contenido: '%s' | RSSI %d dBm | SNR %.1f dB\n",
                recibido.c_str(),
                ultimoRssiLoRaRx,
                ultimoSnrLoRaRx);
  if (deltaRecepcion > 0) {
    Serial.printf("⏱️ Tiempo desde el último paquete nuup_: %lums\n", deltaRecepcion);
  }
  Serial.println("   ↳ RSSI = potencia recibida (más negativo es peor) | SNR = limpieza de señal (mayor es mejor) | Δt = separación entre paquetes");

  uint32_t consecutivo = ++consecutivoMonitorBidireccional;
  if (recibido.startsWith("nuup_")) {
    uint32_t candidato = recibido.substring(5).toInt();
    if (candidato > 0) {
      consecutivoMonitorBidireccional = candidato;
      consecutivo = candidato;
    }
  }

  String respuesta = "monitor_" + String(consecutivo);

  LoRa.idle();
  loraEnEscucha = false;
  LoRa.setTxPower(potenciaLoRaMonitorDbm);

  Serial.printf("📤 [DEV] Respondiendo %s | TX %u dBm | Último RSSI %d dBm | SNR %.1f dB\n",
                respuesta.c_str(),
                potenciaLoRaMonitorDbm,
                ultimoRssiLoRaRx,
                ultimoSnrLoRaRx);
  Serial.println("   ↳ TX = potencia de salida configurada | RSSI = fuerza con la que llegó nuup_* | SNR = claridad de la señal recibida");

  if (LoRa.beginPacket()) {
    LoRa.print(respuesta);
    if (LoRa.endPacket()) {
      unsigned long finEnvio = millis();
      Serial.printf("✅ [DEV] Respuesta dev enviada correctamente (t=%lums desde recepción)\n", finEnvio - marcaRecepcion);
    } else {
      Serial.println("❌ [DEV] endPacket devolvió 0 al responder");
    }
  } else {
    Serial.println("❌ [DEV] No se pudo iniciar el paquete de respuesta dev");
  }

  reanudarRecepcionLoRa("LORA_bidireccional_borrar");
}

void procesarPaqueteLoRaRecibido(int packetSize) {
    if (packetSize <= 0) {
        return;
    }

    LoRaProcessingGuard guard(loraProcesando);

    ultimoRssiLoRaRx = LoRa.packetRssi();
    ultimoSnrLoRaRx = LoRa.packetSnr();
    unsigned long marcaRecepcionLoRa = millis();

    Serial.println("\n🎉 ===========================================");
    Serial.println("📡 PAQUETE LoRa DETECTADO - DEBUG COMPLETO");
    Serial.println("🎉 ===========================================");

    String received = "";
    while (LoRa.available()) {
        received += (char)LoRa.read();
    }
    received.trim();

    Serial.printf("📊 Longitud mensaje: %d bytes\n", received.length());
    Serial.printf("📶 RSSI: %d, SNR: %.2f\n", ultimoRssiLoRaRx, ultimoSnrLoRaRx);
    Serial.print("📨 Mensaje RAW: '");
    Serial.print(received);
    Serial.println("'");
    Serial.printf("📥 LORA RX OK (%dB): %s\n", LoRa.packetRssi(), received.c_str());
    Serial.println("🧭 Detalle de parámetros activos durante esta recepción/envío:");
    imprimirDetalleParametrosLoRa();

        if (received.startsWith("configuracion/")) {
            int primera = received.indexOf('/');
            int segunda = received.indexOf('/', primera + 1);
            int coma = received.indexOf(',', segunda + 1);

            if (primera == -1 || segunda == -1 || coma == -1) {
                Serial.println("⚠️  Mensaje de configuración con formato inválido");
                return;
            }

            String macConfig = normalizarMac(received.substring(primera + 1, segunda));
            String etapa = received.substring(segunda + 1, coma);
            uint8_t potenciaSolicitada = received.substring(coma + 1).toInt();

            if (!esMacValida(macConfig)) {
                Serial.printf("❌ MAC inválida en configuración: '%s'\n", macConfig.c_str());
                return;
            }

            if (etapa != "solicitud") {
                Serial.printf("⏭️  Configuración ignorada por etapa distinta: %s\n", etapa.c_str());
                return;
            }

            bool activo = false;
            for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
                if (normalizarMac(String(configDispositivos[i].mac)) == macConfig && configDispositivos[i].activo) {
                    activo = true;
                    break;
                }
            }

            if (!activo) {
                Serial.println("⚠️  Configuración recibida de MAC no activa o no registrada, se confirma para compatibilidad");
            }

            String respuesta = "configuracion/" + macConfig + "/confirmacion," + String(potenciaSolicitada);
            Serial.printf("📡 Confirmando potencia LoRa a %s con %u dBm\n", macConfig.c_str(), potenciaSolicitada);
            LoRa.beginPacket();
            LoRa.print(respuesta);
            LoRa.endPacket();
            return;
        }
        
        // Normalizar payloads que lleguen como nuup/MAC,... a formato clásico "1,MAC,..."
        if (received.startsWith("nuup/")) {
            int slash = received.indexOf('/');
            int coma = received.indexOf(',', slash + 1);

            if (coma == -1) {
                Serial.println("⚠️  Mensaje nuup/MAC sin datos, se descarta");
                return;
            }

            String macNuup = normalizarMac(received.substring(slash + 1, coma));
            if (!esMacValida(macNuup)) {
                Serial.printf("❌ MAC inválida en formato nuup/MAC: '%s'\n", macNuup.c_str());
                return;
            }

            String datos = received.substring(coma + 1);
            received = "1," + macNuup + "," + datos;

            Serial.printf("🔄 Formato nuup/MAC detectado, normalizado a: '%s'\n", received.c_str());
        }

        // Debug detallado del formato
        debugMensajeLoRa(received);
        
        // Extraer MAC
        int firstComma = received.indexOf(',');
        int secondComma = received.indexOf(',', firstComma + 1);

        if (firstComma != -1 && secondComma != -1) {
            String mac = normalizarMac(received.substring(firstComma + 1, secondComma));

            // Determinar tipo de dispositivo (por defecto 1 si no se puede leer)
            byte tipoDispositivo = 1;
            if (firstComma > 0) {
                String tipoStr = received.substring(0, firstComma);
                tipoStr.trim();
                if (tipoStr.length() == 0) {
                    Serial.println("⏭️  Mensaje LoRa ignorado: tipo de dispositivo vacío");
                    return;
                }

                for (size_t i = 0; i < tipoStr.length(); i++) {
                    if (!isDigit(tipoStr.charAt(i))) {
                        Serial.printf("⏭️  Mensaje LoRa ignorado: tipo no numérico '%s'\n", tipoStr.c_str());
                        return;
                    }
                }
                int tipoValor = tipoStr.toInt();
                if (tipoStr.length() > 0) {
                    if (tipoValor < 0) tipoValor = 0;
                    if (tipoValor > 255) tipoValor = 255;
                    tipoDispositivo = (byte)tipoValor;
                }
            }

            if (!esMacValida(mac)) {
                Serial.printf("❌ MAC inválida en mensaje LoRa: '%s'\n", mac.c_str());
                return;
            }

            if (tipoDispositivo == 0) {
                Serial.println("⏭️  Mensaje LoRa ignorado: tipo de dispositivo 0 (reservado)");
                return;
            }

            if (!mensajeLoRaTieneDatos(received)) {
                Serial.println("⏭️  Mensaje LoRa ignorado: no contiene todos los datos de sensado");
                return;
            }

            Serial.printf("🔍 MAC extraída (normalizada): '%s'\n", mac.c_str());
            Serial.printf("🔍 Longitud MAC: %d\n", mac.length());
            
            // Buscar dispositivo
            bool encontrado = false;
            for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
                String storedMac = normalizarMac(String(configDispositivos[i].mac));

                Serial.printf("   🔎 Comparando con [%d]: '%s'\n", i, storedMac.c_str());

                if (storedMac == mac) {
                    encontrado = true;
                    Serial.printf("✅ DISPOSITivo ENCONTRADO en índice: %d\n", i);

                    // Procesar datos
                    actualizarDatosDesdeLoRa(mac, received, "");

                    bool modificacionActiva = modificacionBrokerActiva[i];
                    DatosConfirmacionLoRa datosConfirmacion =
                        construirConfirmacionLoRa(received, configDispositivos[i], modificacionActiva);

                    if (modificacionActiva) {
                        mensajeLoRa = construirPayloadEEPROMParaMQTT(configDispositivos[i]);
                        Serial.println("ℹ️  MQTT usará los datos de EEPROM mientras se confirma la modificación solicitada por el broker.");
                    } else {
                        mensajeLoRa = normalizarPayloadParaMQTT(received);
                    }

                    nuevoMensajeLoRa = configDispositivos[i].activo && mqttConfirmed;

                    if (!configDispositivos[i].activo) {
                        Serial.println("ℹ️  Dispositivo inactivo: se confirma por LoRa y se agenda alta si aplica");
                        intentarAltaTrasRegistro(i, mac, "LoRa (existente)");
                    }

                    Serial.printf("⚡ Preparando confirmación inmediata hacia NUUP01 (respuesta en %lums, TX %u dBm)\n",
                                  millis() - marcaRecepcionLoRa,
                                  potenciaLoRaMonitorDbm);
                    Serial.printf("🚀 Se enviarán %u confirmaciones cada %lums para garantizar recepción. TX = potencia actual de salida, Último RSSI = potencia recibida, SNR = claridad de la señal\n",
                                  REPETICIONES_CONFIRMACION_LORA,
                                  INTERVALO_CONFIRMACION_LORA_MS);
                    String confirmacion = "CONFIRMACION," + mac + "," +
                                             datosConfirmacion.nombre + "," +
                                             String(datosConfirmacion.alturaConfig, 0) + "," +
                                             String(datosConfirmacion.litrosConfig, 0);

                    if (!datosConfirmacion.origenMensaje) {
                        Serial.println("ℹ️  Confirmación armada con datos locales al no encontrar campos completos en el mensaje");
                    }

                    if (!enviarPaqueteLoRa("confirmación LoRa", mac, confirmacion)) {
                        Serial.println("⚠️  Revisa la potencia o interferencias: la confirmación podría no haber salido");
                    }
                    break;
                }
            }

            if (!encontrado) {
                Serial.println("❌ DISPOSITIVO NO REGISTRADO - Mensaje LoRa ignorado sin auto-alta ni publicación MQTT");
                Serial.println("ℹ️  Registre el dispositivo por BLE o portal antes de aceptar datos LoRa");

                String errorConfirmacion = "ERROR," + mac + ",NO_REGISTRADO";
                enviarPaqueteLoRa("error LoRa (no registrado)", mac, errorConfirmacion);
                return;
            }
        } else {
            Serial.println("❌ ERROR: No se pudieron extraer las comas del mensaje");
        }
        
        Serial.println("🎉 ===========================================\n");
    }

void recepcion_lora() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        procesarPaqueteLoRaRecibido(packetSize);
    }
}




// Función para eliminar un dispositivo
bool eliminarDispositivo(const String &mac) {
    int posicion = -1;
    
    // Buscar el dispositivo
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == mac) {
            posicion = i;
            break;
        }
    }
    
    if (posicion == -1) {
        Serial.println("Dispositivo no encontrado para eliminar");
        return false;
    }

    // Debug: Mostrar antes de eliminar
    Serial.println("Dispositivo encontrado en posición: " + String(posicion));
    Serial.println("Contenido antes de eliminar:");
    imprimirConfigDispositivo(mac);

    // Reorganizar el array
    for (int i = posicion; i < MAX_DISPOSITIVOS - 1; i++) {
        configDispositivos[i] = configDispositivos[i + 1];
        ultimoMensajeLoRaDispositivo[i] = ultimoMensajeLoRaDispositivo[i + 1];
        ultimaActualizacionLoRa[i] = ultimaActualizacionLoRa[i + 1];
        mostrarSinDatos[i] = mostrarSinDatos[i + 1];
        ultimaSolicitudAlta[i] = ultimaSolicitudAlta[i + 1];
        solicitudAltaEnviada[i] = solicitudAltaEnviada[i + 1];
        bajaPendienteMQTT[i] = bajaPendienteMQTT[i + 1];
        ultimaSolicitudBaja[i] = ultimaSolicitudBaja[i + 1];
        inicioEsperaBaja[i] = inicioEsperaBaja[i + 1];
        modificacionBrokerActiva[i] = modificacionBrokerActiva[i + 1];
        aliasObjetivoBroker[i] = aliasObjetivoBroker[i + 1];
        alturaObjetivoBroker[i] = alturaObjetivoBroker[i + 1];
        capacidadObjetivoBroker[i] = capacidadObjetivoBroker[i + 1];
        litrosReportadosBroker[i] = litrosReportadosBroker[i + 1];
    }

    // Limpiar la última posición
    configDispositivos[MAX_DISPOSITIVOS - 1].mac[0] = '\0';
    configDispositivos[MAX_DISPOSITIVOS - 1].nombre[0] = '\0';
    configDispositivos[MAX_DISPOSITIVOS - 1].tipoDispositivo = 0;
    configDispositivos[MAX_DISPOSITIVOS - 1].activo = false;
    configDispositivos[MAX_DISPOSITIVOS - 1].litrosActuales = 0;
    configDispositivos[MAX_DISPOSITIVOS - 1].voltaje = 0;
    configDispositivos[MAX_DISPOSITIVOS - 1].temperatura = 0;
    configDispositivos[MAX_DISPOSITIVOS - 1].alturaConfig = 0;
    configDispositivos[MAX_DISPOSITIVOS - 1].litrosConfig = 0;
    configDispositivos[MAX_DISPOSITIVOS - 1].porcentaje = 0;
    ultimoMensajeLoRaDispositivo[MAX_DISPOSITIVOS - 1] = "";
    ultimaActualizacionLoRa[MAX_DISPOSITIVOS - 1] = 0;
    mostrarSinDatos[MAX_DISPOSITIVOS - 1] = false;
    ultimaSolicitudAlta[MAX_DISPOSITIVOS - 1] = 0;
    solicitudAltaEnviada[MAX_DISPOSITIVOS - 1] = false;
    bajaPendienteMQTT[MAX_DISPOSITIVOS - 1] = false;
    ultimaSolicitudBaja[MAX_DISPOSITIVOS - 1] = 0;
    inicioEsperaBaja[MAX_DISPOSITIVOS - 1] = 0;
    modificacionBrokerActiva[MAX_DISPOSITIVOS - 1] = false;
    aliasObjetivoBroker[MAX_DISPOSITIVOS - 1] = "";
    alturaObjetivoBroker[MAX_DISPOSITIVOS - 1] = 0;
    capacidadObjetivoBroker[MAX_DISPOSITIVOS - 1] = 0;
    litrosReportadosBroker[MAX_DISPOSITIVOS - 1] = 0;

    // Debug: Mostrar después de eliminar
    Serial.println("Contenido después de eliminar:");
    imprimirDispositivosRegistrados();

    // Guardar cambios
    if (guardarDispositivos()) {
        Serial.println("Dispositivo eliminado y EEPROM actualizada correctamente");
        return true;
    } else {
        Serial.println("Error al guardar cambios en EEPROM");
        return false;
    }
}

// ============================================================================
// DESARROLLO DE FUNCIONES TFT (después del loop)
// ============================================================================

void dibujarTituloDispositivo(Dispositivo disp) {
  // Segunda línea (1/3 del display) - Título del dispositivo
  display.setTextSize(2);
  display.setCursor(0, 22); // 21 pixels desde arriba
  
  // Ajustar texto si es muy largo
  String titulo = disp.nombre;
  if (titulo.length() > 12) {
    titulo = titulo.substring(0, 12);
  }
  
  display.println(titulo);
}


void dibujarCirculoGiratorio(int centroX, int centroY, int radio, int angulo) {
  // Dibujar círculo base
  display.drawCircle(centroX, centroY, radio, SSD1306_WHITE);
  
  // Calcular punto en el círculo según el ángulo
  float radianes = angulo * PI / 180.0;
  int puntoX = centroX + radio * cos(radianes);
  int puntoY = centroY + radio * sin(radianes);
  
  // Dibujar punto giratorio
  display.fillCircle(puntoX, puntoY, 3, SSD1306_WHITE);
}

void mostrarEmparejamiento() {
    display.clearDisplay();
    
    // Centro de la pantalla
    int centroX = SCREEN_WIDTH / 2;
    int centroY = 20;
    
    // Dibujar círculo giratorio
    int angulo = (frameAnimacion * 45) % 360;
    dibujarCirculoGiratorio(centroX, centroY, 15, angulo);
    
    // Texto según el tipo de operación
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    
    if (solicitudAltaBLE) {
        display.setCursor(centroX - 24, 40);
        display.print("ALTA");
    } else if (solicitudBajaBLE) {
        display.setCursor(centroX - 24, 40);
        display.print("BAJA");
    } else {
        display.setCursor(centroX - 36, 40);
        display.print("CONFIG");
    }
    
    // Puntos animados
    display.setTextSize(1);
    int puntosAncho = 3 * 6;
    display.setCursor((SCREEN_WIDTH - puntosAncho) / 2, 55);
    
    int numPuntos = (frameAnimacion / 2) % 4;
    for(int i = 0; i < numPuntos; i++) {
        display.print(".");
    }
    
    display.display();
}


void iniciarEmparejamiento() {
  emparejando = true;
  frameAnimacion = 0;
  ultimoCambioAnimacion = millis();
  Serial.println("Iniciando emparejamiento...");
}

void detenerEmparejamiento() {
  emparejando = false;
  Serial.println("Emparejamiento completado");
}

void emparejarNuevoDispositivo() {
  iniciarEmparejamiento();
}

void actualizarDatos(int index, int porcentaje, float bateria, int litros) {
  if (index >= 0 && index < (sizeof(dispositivos) / sizeof(dispositivos[0]))) {
    dispositivos[index].porcentaje = porcentaje;
    dispositivos[index].bateria = bateria;
    dispositivos[index].litros = litros;
  }
}

void setWifiStatus(bool conectado) {
  wifiConectado = conectado;
}


// Función para dibujar WiFi animado (creciente)
void dibujarWifiAnimado(int centroX, int centroY, int frame) {
  int maxNiveles = 4; // Niveles del icono WiFi
  
  for(int nivel = 0; nivel < maxNiveles; nivel++) {
    int radio = 3 + (nivel * 4); // Radios: 3, 7, 11, 15
    
    // Solo dibujar niveles hasta el frame actual
    if (nivel <= frame) {
      if (nivel < maxNiveles - 1) {
        // Arcos del WiFi (niveles 0, 1, 2)
        display.drawCircle(centroX, centroY, radio, SSD1306_WHITE);
      } else {
        // Punto central (nivel 3)
        display.fillCircle(centroX, centroY, 2, SSD1306_WHITE);
      }
    }
  }
}

// Función para mostrar conexión WiFi
void mostrarConexionWifi() {
  if (!displayReady) {
    return;
  }
  display.clearDisplay();
  
  // Centro de la pantalla
  int centroX = SCREEN_WIDTH / 2;
  int centroY = 20;
  
  // Dibujar WiFi animado
  dibujarWifiAnimado(centroX, centroY, frameWifi);
  
  // Texto "Conectando WiFi" - ALINEADO A LA IZQUIERDA
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  // Capturar y fijar la MAC del monitor para evitar confundirla con la de un sensor
  asegurarMacMonitorFija("wifi_anim");
  
  display.setCursor(0, 45);
  display.print("WiFi");
   
  // Puntos animados
  display.setTextSize(2);
  int posicionPuntos = 4 * 12;
  display.setCursor(posicionPuntos, 45);
  
  // Animación de puntos (0, 1, 2, 3 puntos)
  int numPuntos = (frameWifi / 2) % 4;
  for(int i = 0; i < numPuntos; i++) {
    display.print(".");
  }

  display.display();
}

// Aviso breve de WiFi al inicio sin activar el portal
void mostrarWifiInicioTemporal() {
  iniciarAnimacionWifi();
  unsigned long inicioAviso = millis();

  while (millis() - inicioAviso < 2000) { // solo 2 segundos
    if (animandoWifi && millis() - ultimoCambioWifi >= INTERVALO_WIFI) {
      frameWifi = (frameWifi + 1) % 4;
      ultimoCambioWifi = millis();
    }
    mostrarConexionWifi();
    delay(50);
  }

  detenerAnimacionWifi();

  // Limpiar pantalla después del aviso
  if (displayReady) {
    display.clearDisplay();
    display.display();
  }
}

void mostrarAvisoPortalAutomatico() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("⚠️  Sin WiFi estable");
  display.println("Pulsa el boton WiFi");
  display.println("para abrir el portal");
  display.println("y configurar redes");
  display.display();
}

// Función para iniciar animación WiFi
void iniciarAnimacionWifi() {
  animandoWifi = true;
  frameWifi = 0;
  ultimoCambioWifi = millis();
  Serial.println("Iniciando animación WiFi...");
}

// Función para detener animación WiFi
void detenerAnimacionWifi() {
  animandoWifi = false;
  wifiConectado = true; // Marcar como conectado
  Serial.println("Animación WiFi completada");
}

void dibujarMensajeConexion() {
  if (!displayReady) {
    return;
  }
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(conexionExitosa ? "Configuración guardada" : "Error WiFi");

  display.setCursor(0, 20);
  display.print("Red: ");
  if (ultimaRedConfigurada.length() > 0) {
    display.print(ultimaRedConfigurada);
  } else {
    display.print("(sin red)");
  }

  display.setCursor(0, 32);
  display.print("Pass: ");
  if (ultimaContrasenaConfigurada.length() > 0) {
    display.print(ultimaContrasenaConfigurada);
  } else {
    display.print("(vacía)");
  }

  display.setCursor(0, 44);
  display.print("User ID: ");
  display.print(userID.length() > 0 ? userID : "(vacío)");

  display.display();
}

// Función para conectar WiFi (desde otras partes del código)
void conectarWifi() {
  iniciarAnimacionWifi();
}


//pantalla dinamica

int contarDispositivosRegistrados() {
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            count++;
        }
    }
    
    // ⭐⭐ SOLO IMPRIMIR SI CAMBIA EL CONTEO
    static int ultimoConteo = -1;
    if (count != ultimoConteo) {
        ultimoConteo = count;
        Serial.println("📊 Total dispositivos registrados: " + String(count));
    }
    
    return count;
}


Dispositivo obtenerDatosDispositivo(int index) {
    Dispositivo disp;
    disp.nombre = "Sin Dispositivos";
    disp.porcentaje = 0;
    disp.bateria = 0.0;
    disp.litros = 0;
    disp.sinDatos = false; // ⭐ NUEVO: flag para indicar sin datos
    
    // ⭐⭐ VERIFICAR índice válido
    if (index < 0) {
        return disp;
    }
    
    // Buscar el dispositivo en la posición index
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            if (count == index) {
                String nombreDispositivo = String(configDispositivos[i].nombre);
                if (nombreDispositivo.length() == 0) {
                    nombreDispositivo = "Dispositivo " + String(i+1);
                }
                disp.nombre = nombreDispositivo;
                
                // ⭐⭐ VERIFICAR SI HAY DATOS RECIENTES
                if (millis() - ultimaActualizacionLoRa[i] > TIEMPO_SIN_DATOS) {
                    disp.sinDatos = true;
                    mostrarSinDatos[i] = true;
                } else {
                    disp.porcentaje = configDispositivos[i].porcentaje;
                    disp.bateria = configDispositivos[i].voltaje;
                    disp.litros = (int)configDispositivos[i].litrosActuales;
                    disp.sinDatos = false;
                    mostrarSinDatos[i] = false;
                }
                break;
            }
            count++;
        }
    }
    
    return disp;
}

void dibujarHeader() {
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    // WiFi - Esquina superior izquierda
    if (wifiConectado) {
        display.drawBitmap(2, 2, wifiIcon, 16, 16, SSD1306_WHITE);
    } else {
        display.drawBitmap(2, 2, wifiIconOff, 16, 16, SSD1306_WHITE);
    }
    
    // NUUP pequeño (tamaño 1)
    display.setTextSize(1);
    display.setCursor(22, 6);
    display.print("NUUP");
    
    // Indicador de dispositivos (actual/total)
    display.setTextSize(2);
    display.setCursor(22 + 4 * 6, 2);
    display.print(" ");
    if (cantidadDispositivos > 0) {
        display.print((dispositivoActual % cantidadDispositivos) + 1);
        display.print("/");
        display.print(cantidadDispositivos);
    } else {
        display.print("0/0");
    }
    
    // Batería del dispositivo actual
    if (cantidadDispositivos > 0) {
        Dispositivo dispActual = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
        if (dispActual.bateria >= 3.0) {
            display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryFull, 16, 16, SSD1306_WHITE);
        } else {
            display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryEmpty, 16, 16, SSD1306_WHITE);
        }
    } else {
        // Sin dispositivos - batería vacía
        display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryEmpty, 16, 16, SSD1306_WHITE);
    }
}

void dibujarTituloDispositivo() {
    display.setTextSize(2);
    display.setCursor(0, 22);
    
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    if (cantidadDispositivos == 0) {
        display.println("SIN Dispositivos");
    } else {
        Dispositivo disp = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
        
        // Ajustar texto si es muy largo
        String titulo = disp.nombre;
        if (titulo.length() > 12) {
            titulo = titulo.substring(0, 12);
        }
        display.println(titulo);
    }
}

void dibujarContenidoPrincipal() {
    display.fillRect(0, 44, 128, 20, SSD1306_BLACK);
    
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    if (cantidadDispositivos == 0) {
        // Mostrar etiqueta NUUP debajo del título de "SIN Dispositivos"
        display.setTextSize(2);
        display.setCursor((SCREEN_WIDTH - (4 * 6 * 2)) / 2, 48); // Centrado aproximado
        display.print("NUUP");
    } else {
        Dispositivo disp = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
        
        // ⭐⭐ VERIFICAR SI MOSTRAR "SIN DATOS"
        if (disp.sinDatos) {
            // Mostrar "SIN DATOS" centrado
            display.setTextSize(2);
            display.setCursor(SCREEN_WIDTH/2 - 42, 48);
            display.print("SIN DATOS");
        } else {
            // Mostrar datos normales
            // Porcentaje grande (izquierda)
            display.setTextSize(3);
            display.setCursor(0, 44);
            display.print(disp.porcentaje);
            display.print("%");
            
            // Litros (derecha)
            display.setTextSize(2);
            display.setCursor(70, 48);
            display.print(disp.litros);
            display.setTextSize(1);
            display.setCursor(70 + String(disp.litros).length() * 12, 52);
            display.print("L");
        }
    }
}

void iniciarWaterDisplay() {
    waterWire.begin(WATER_OLED_SDA, WATER_OLED_SCL);
    waterDisplayOk = waterDisplay.begin(SSD1306_SWITCHCAPVCC, WATER_OLED_ADDR);
    if (!waterDisplayOk) {
        Serial.println("❌ OLED nivel de agua: fallo al iniciar (SDA15/SCL4).");
        return;
    }
    waterDisplay.setRotation(1); // Vertical
    waterDisplay.clearDisplay();
    waterDisplay.display();
    Serial.println("✅ OLED nivel de agua inicializado en SDA15/SCL4.");
}

int extraerEstatusLlenado(const String &payload) {
    String limpio = payload;
    limpio.trim();
    if (limpio.isEmpty()) {
        return 0;
    }

    int resultIndex = limpio.indexOf("result");
    if (resultIndex != -1) {
        int inicioNumero = limpio.indexOf(':', resultIndex);
        if (inicioNumero != -1) {
            inicioNumero++;
            while (inicioNumero < limpio.length() && !isDigit(limpio[inicioNumero])) {
                inicioNumero++;
            }
            int finNumero = inicioNumero;
            while (finNumero < limpio.length() && isDigit(limpio[finNumero])) {
                finNumero++;
            }
            if (inicioNumero < finNumero) {
                return limpio.substring(inicioNumero, finNumero).toInt();
            }
        }
    }

    if (limpio.indexOf(',') != -1) {
        String ultimoCampo = limpio.substring(limpio.lastIndexOf(',') + 1);
        ultimoCampo.trim();
        if (!ultimoCampo.isEmpty()) {
            return ultimoCampo.toInt();
        }
    }

    return limpio.toInt();
}

void dibujarNivelAguaVertical(int porcentaje, bool mostrarAgua) {
    const int ancho = waterDisplay.width();
    const int alto = waterDisplay.height();
    const int margenIzq = WATER_MARGIN_LEFT;
    const int margenDer = WATER_MARGIN_RIGHT;
    const int margenSup = WATER_MARGIN_TOP;
    const int margenInf = WATER_MARGIN_BOTTOM;
    const int marco = 2;
    const int tanqueW = ancho - (margenIzq + margenDer);
    const int tanqueH = alto - (margenSup + margenInf);
    const int tanqueX = margenIzq;
    const int tanqueY = margenSup;
    const int interiorX = tanqueX + marco;
    const int interiorY = tanqueY + marco;
    const int interiorW = tanqueW - (marco * 2);
    const int interiorH = tanqueH - (marco * 2);

    waterDisplay.drawRect(tanqueX, tanqueY, tanqueW, tanqueH, SSD1306_WHITE);

    const int pipeWidth = 10;
    const int pipeHeight = 12;
    const int pipeX = tanqueX + (tanqueW / 2) - (pipeWidth / 2);
    const int pipeY = tanqueY - pipeHeight - 8;
    const int spoutWidth = 26;
    const int spoutHeight = 6;
    const int spoutX = pipeX - 8;
    const int spoutY = pipeY + pipeHeight - 2;
    const int valveRadius = 5;
    const int valveX = pipeX + pipeWidth / 2;
    const int valveY = pipeY - valveRadius - 2;
    const int capWidth = 16;
    const int capHeight = 4;
    const int capX = valveX - capWidth / 2;
    const int capY = valveY - valveRadius - capHeight + 1;

    waterDisplay.drawRect(pipeX, pipeY, pipeWidth, pipeHeight, SSD1306_WHITE);
    waterDisplay.fillRect(pipeX + 1, pipeY + 1, pipeWidth - 2, pipeHeight - 2, SSD1306_WHITE);
    waterDisplay.drawRect(spoutX, spoutY, spoutWidth, spoutHeight, SSD1306_WHITE);
    waterDisplay.fillRect(spoutX + 1, spoutY + 1, spoutWidth - 2, spoutHeight - 2, SSD1306_WHITE);
    waterDisplay.drawCircle(valveX, valveY, valveRadius, SSD1306_WHITE);
    waterDisplay.drawFastHLine(valveX - 4, valveY, 8, SSD1306_WHITE);
    waterDisplay.drawFastVLine(valveX, valveY - 4, 8, SSD1306_WHITE);
    waterDisplay.drawRect(capX, capY, capWidth, capHeight, SSD1306_WHITE);
    waterDisplay.drawFastHLine(capX, capY + capHeight + 1, capWidth, SSD1306_WHITE);

    if (!mostrarAgua) {
        return;
    }

    int alturaAgua = (interiorH * porcentaje) / 100;
    alturaAgua = constrain(alturaAgua, 0, interiorH);
    int aguaY = interiorY + (interiorH - alturaAgua);

    waterDisplay.fillRect(interiorX, aguaY, interiorW, alturaAgua, SSD1306_WHITE);

    int waveOffset = (millis() / 250) % 4;
    for (int y = aguaY + waveOffset; y < interiorY + interiorH; y += 6) {
        waterDisplay.drawFastHLine(interiorX, y, interiorW, SSD1306_BLACK);
    }

    if (waterDisplayLlenando) {
        int dropOffset = (millis() / 120) % 10;
        int dropX = spoutX + (spoutWidth / 2);
        int dropYStart = spoutY + spoutHeight + 1;
        int dropY = dropYStart + dropOffset;
        int dropYMax = tanqueY + 8;
        if (dropY < dropYMax) {
            waterDisplay.drawFastVLine(dropX, dropYStart, dropY - dropYStart + 1, SSD1306_WHITE);
            waterDisplay.drawPixel(dropX - 1, dropY, SSD1306_WHITE);
            waterDisplay.drawPixel(dropX + 1, dropY, SSD1306_WHITE);
        }
    }
}

void actualizarWaterDisplay(int porcentaje) {
    if (!waterDisplayOk) {
        return;
    }

    porcentaje = constrain(porcentaje, 0, 100);
    bool necesitaRender = false;
    unsigned long ahora = millis();
    int porcentajeRender = porcentaje;

    if (!waterDisplayLlenando || porcentaje <= waterDisplayPorcentajeAnimado) {
        if (waterDisplayPorcentajeAnimado != porcentaje) {
            waterDisplayPorcentajeAnimado = porcentaje;
            necesitaRender = true;
        }
    } else if (ahora - waterDisplayUltimaAnimacion >= 150) {
        waterDisplayUltimaAnimacion = ahora;
        waterDisplayPorcentajeAnimado = min(waterDisplayPorcentajeAnimado + 1, porcentaje);
        necesitaRender = true;
    }

    porcentajeRender = waterDisplayPorcentajeAnimado;

    if (porcentaje < 10) {
        if (ahora - waterDisplayUltimoBlink >= 500) {
            waterDisplayUltimoBlink = ahora;
            waterDisplayBlinkOn = !waterDisplayBlinkOn;
            necesitaRender = true;
        }
    } else if (!waterDisplayBlinkOn) {
        waterDisplayBlinkOn = true;
        necesitaRender = true;
    }

    if (porcentaje != waterDisplayUltimoPorcentaje) {
        waterDisplayUltimoPorcentaje = porcentaje;
        necesitaRender = true;
    }

    if (!necesitaRender) {
        return;
    }

    waterDisplay.clearDisplay();
    dibujarNivelAguaVertical(porcentajeRender, waterDisplayBlinkOn);
    waterDisplay.display();
}

bool actualizarLecturasParcialesDesdeLoRa(int indice, const String &mensaje) {
    if (indice < 0 || indice >= MAX_DISPOSITIVOS) {
        return false;
    }

    String litrosStr = obtenerCampoCSV(mensaje, 2);
    String voltajeStr = obtenerCampoCSV(mensaje, 3);
    String temperaturaStr = obtenerCampoCSV(mensaje, 4);

    litrosStr.trim();
    voltajeStr.trim();
    temperaturaStr.trim();

    if (litrosStr.isEmpty() && voltajeStr.isEmpty() && temperaturaStr.isEmpty()) {
        return false;
    }

    ConfigDispositivo &config = configDispositivos[indice];
    bool actualizado = false;

    if (!litrosStr.isEmpty()) {
        config.litrosActuales = litrosStr.toFloat();
        actualizado = true;
    }
    if (!voltajeStr.isEmpty()) {
        config.voltaje = voltajeStr.toFloat();
        actualizado = true;
    }
    if (!temperaturaStr.isEmpty()) {
        config.temperatura = temperaturaStr.toFloat();
        actualizado = true;
    }

    if (config.litrosConfig > 0) {
        float porcentaje = (config.litrosActuales / config.litrosConfig) * 100.0f;
        config.porcentaje = (int)constrain(porcentaje, 0, 100);
    }

    ultimaActualizacionLoRa[indice] = millis();
    mostrarSinDatos[indice] = false;

    if (actualizado) {
        Serial.println("   📈 Lecturas LoRa actualizadas en modo modificación (litros/voltaje/temperatura).");
        if (guardarDispositivos()) {
            Serial.println("   💾 Lecturas actualizadas guardadas en EEPROM.");
        } else {
            Serial.println("   ❌ Error al guardar lecturas actualizadas en EEPROM.");
        }
    }

    return actualizado;
}

void actualizarDatosDesdeLoRa(const String &mac, const String &mensaje, const String &nombre) {
    Serial.println("\n🔄 ===========================================");
    Serial.println("🔍 ACTUALIZAR DATOS DESDE LORA - DEBUG");
    Serial.println("🔄 ===========================================");
    
    Serial.printf("📱 MAC: '%s'\n", mac.c_str());
    Serial.printf("📨 Mensaje: '%s'\n", mensaje.c_str());
    
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == mac) {
            Serial.printf("✅ Dispositivo encontrado en índice: %d\n", i);
            Serial.printf("📝 Nombre actual: '%s'\n", configDispositivos[i].nombre);

            // Guardar mensaje completo (incluye valores adicionales de IA)
            ultimoMensajeLoRaDispositivo[i] = mensaje;
            mensajeLoRa = mensaje;          // Garantizar que el mensaje más reciente quede listo para MQTT
            nuevoMensajeLoRa = configDispositivos[i].activo && mqttConfirmed;

            if (modificacionBrokerActiva[i]) {
                Serial.printf("🔁 Modificación solicitada por broker para %s: se preservan datos EEPROM y se confirma a sensor\n", mac.c_str());

                bool datosAlineados = false;

                if (mensajeLoRaTieneDatos(mensaje)) {
                    datosAlineados = datosLoRaCoincidenConBroker(mensaje, i);
                } else {
                    Serial.println("⚠️  Mensaje LoRa sin campos suficientes para validar la modificación; se mantendrá la espera.");
                }

                if (!datosAlineados) {
                    actualizarLecturasParcialesDesdeLoRa(i, mensaje);
                    ultimaActualizacionLoRa[i] = millis();
                    mostrarSinDatos[i] = false;
                    mensajeLoRa = construirPayloadEEPROMParaMQTT(configDispositivos[i]);
                    nuevoMensajeLoRa = configDispositivos[i].activo && mqttConfirmed;
                    Serial.println("   ⏳ Se envía telemetría MQTT con datos de EEPROM hasta que NUUP01 confirme la modificación por LoRa.");
                    String confirmacion = "CONFIRMACION," + mac + "," +
                                          String(configDispositivos[i].nombre) + "," +
                                          String(configDispositivos[i].alturaConfig, 0) + "," +
                                          String(configDispositivos[i].litrosConfig, 0);

                    Serial.printf(
                        "   ↪️ Confirmación pendiente: se envían alias/altura/capacidad de EEPROM (%s / %.1f / %.1f)\n",
                        configDispositivos[i].nombre,
                        configDispositivos[i].alturaConfig,
                        configDispositivos[i].litrosConfig);
                    enviarPaqueteLoRa("confirmación EEPROM modificado_broker", mac, confirmacion);
                    return;
                }

                Serial.println("✅ NUUP01 ya reporta alias/altura/capacidad solicitados por el broker; se libera la bandera de modificación.");
                modificacionBrokerActiva[i] = false;
            }

            // Actualizar tipo de dispositivo en caso de que cambie en un mensaje futuro
            int primeraComa = mensaje.indexOf(',');
            if (primeraComa > 0) {
                byte tipoNuevo = (byte)mensaje.substring(0, primeraComa).toInt();
                if (tipoNuevo > 0 && tipoNuevo != configDispositivos[i].tipoDispositivo) {
                    Serial.printf("🔄 TIPO ACTUALIZADO: %d -> %d\n", configDispositivos[i].tipoDispositivo, tipoNuevo);
                    configDispositivos[i].tipoDispositivo = tipoNuevo;
                }
            }

            // Parsear mensaje
            int commas[8];
            int commaCount = 0;
            
            for (int pos = 0; pos < mensaje.length() && commaCount < 8; pos++) {
                if (mensaje.charAt(pos) == ',') {
                    commas[commaCount] = pos;
                    commaCount++;
                }
            }
            
            Serial.printf("🔢 Comas encontradas: %d\n", commaCount);
            
            if (commaCount >= 6) {
                // Extraer valores
                String litrosActualesStr = mensaje.substring(commas[1] + 1, commas[2]);
                String voltajeStr = mensaje.substring(commas[2] + 1, commas[3]);
                String temperaturaStr = mensaje.substring(commas[3] + 1, commas[4]);
                String alturaConfigStr = mensaje.substring(commas[4] + 1, commas[5]);
                String litrosConfigStr = mensaje.substring(commas[5] + 1, commas[6]);
                
                // ⭐⭐ CORRECCIÓN: EXTRAER EL NOMBRE CORRECTAMENTE
                String nombreExtraido = "";
                if (commaCount >= 7) {
                    nombreExtraido = mensaje.substring(commas[6] + 1, commas[7]);
                    nombreExtraido.trim();
                    
                    Serial.printf("📝 NOMBRE EXTRAÍDO: '%s'\n", nombreExtraido.c_str());
                    Serial.printf("📝 LONGITUD: %d\n", nombreExtraido.length());
                    
                    // ⭐⭐ ACTUALIZAR EL NOMBRE SI ES DIFERENTE Y VÁLIDO
                    if (nombreExtraido.length() > 0 && nombreExtraido != String(configDispositivos[i].nombre)) {
                        Serial.println("🔄 ACTUALIZANDO NOMBRE...");
                        nombreExtraido.toCharArray(configDispositivos[i].nombre, 20);
                        Serial.printf("✅ NOMBRE ACTUALIZADO: '%s'\n", configDispositivos[i].nombre);
                    }
                }
                
                Serial.println("📊 VALORES EXTRAÍDOS:");
                Serial.printf("   💧 Litros Actuales: '%s' -> %.1f\n", litrosActualesStr.c_str(), litrosActualesStr.toFloat());
                Serial.printf("   🔋 Voltaje: '%s' -> %.1f\n", voltajeStr.c_str(), voltajeStr.toFloat());
                Serial.printf("   🌡️ Temperatura: '%s' -> %.1f\n", temperaturaStr.c_str(), temperaturaStr.toFloat());
                Serial.printf("   📏 Altura Config: '%s' -> %.1f\n", alturaConfigStr.c_str(), alturaConfigStr.toFloat());
                Serial.printf("   💧 Litros Config: '%s' -> %.1f\n", litrosConfigStr.c_str(), litrosConfigStr.toFloat());
                
                // Actualizar valores
                configDispositivos[i].litrosActuales = litrosActualesStr.toFloat();
                configDispositivos[i].voltaje = voltajeStr.toFloat();
                configDispositivos[i].temperatura = temperaturaStr.toFloat();
                configDispositivos[i].alturaConfig = alturaConfigStr.toFloat();
                configDispositivos[i].litrosConfig = litrosConfigStr.toFloat();
                
                // Calcular porcentaje
                if (configDispositivos[i].litrosConfig > 0) {
                    float porcentaje = (configDispositivos[i].litrosActuales / configDispositivos[i].litrosConfig) * 100;
                    configDispositivos[i].porcentaje = (int)constrain(porcentaje, 0, 100);
                    Serial.printf("📈 Porcentaje calculado: %.1f%% -> %d%%\n", porcentaje, configDispositivos[i].porcentaje);
                }
                
                // Actualizar timestamp
                ultimaActualizacionLoRa[i] = millis();
                mostrarSinDatos[i] = false;
                
                // ⭐⭐ GUARDAR CAMBIOS EN EEPROM
                if (guardarDispositivos()) {
                    Serial.println("💾 DATOS GUARDADOS EN EEPROM (incluyendo nombre)");
                } else {
                    Serial.println("❌ ERROR al guardar en EEPROM");
                }
                
                Serial.println("✅ DATOS ACTUALIZADOS CORRECTAMENTE");
                
            } else {
                Serial.println("❌ ERROR: Mensaje no tiene suficientes campos");
            }
            
            break;
        }
    }
    
    Serial.println("🔄 ===========================================\n");
}



bool guardarDispositivos() {
    EEPROM.begin(EEPROM_SIZE);
    int addr = CONFIG_DISPOSITIVOS_ADDR;

    // Contar dispositivos válidos
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) > 0) {
            count++;
        }
    }

    // Guardar versión y contador (3 bytes)
    EEPROM.write(addr++, CONFIG_VERSION);
    EEPROM.write(addr++, (count >> 8) & 0xFF);
    EEPROM.write(addr++, count & 0xFF);

    // Guardar cada dispositivo
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) > 0) {
            // Guardar MAC (18 bytes)
            for (int j = 0; j < MAC_LEN + 1; j++) {
                EEPROM.write(addr++, configDispositivos[i].mac[j]);
            }
            
            // Guardar nombre (20 bytes)
            for (int j = 0; j < 20; j++) {
                EEPROM.write(addr++, configDispositivos[i].nombre[j]);
            }
            
            // Guardar valores float (5 floats = 20 bytes)
            EEPROM.put(addr, configDispositivos[i].litrosActuales); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].voltaje); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].temperatura); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].alturaConfig); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].litrosConfig); addr += sizeof(float);
            
            // ⭐ GUARDAR PORCENTAJE (2 bytes)
            EEPROM.write(addr++, (configDispositivos[i].porcentaje >> 8) & 0xFF);
            EEPROM.write(addr++, configDispositivos[i].porcentaje & 0xFF);

            // Guardar tipo y estado (2 bytes)
            EEPROM.write(addr++, configDispositivos[i].tipoDispositivo);
            EEPROM.write(addr++, configDispositivos[i].activo ? 1 : 0);

            // Guardar mensaje LoRa completo (longitud + datos)
            uint16_t mensajeLen = ultimoMensajeLoRaDispositivo[i].length();
            if (mensajeLen > 255) {
                Serial.printf("⚠️ Mensaje LoRa truncado a 255 bytes (len=%d)\n", mensajeLen);
                mensajeLen = 255;
            }

            if (addr + 1 + mensajeLen > EEPROM_SIZE) {
                Serial.println("❌ ERROR: No hay espacio en EEPROM para guardar el mensaje LoRa completo");
                EEPROM.end();
                return false;
            }

            EEPROM.write(addr++, mensajeLen);
            for (int j = 0; j < mensajeLen; j++) {
                EEPROM.write(addr++, ultimoMensajeLoRaDispositivo[i].charAt(j));
            }
        }
    }

    bool success = EEPROM.commit();
    EEPROM.end();
    return success;
}

String snapshotEstadoMonitoreo() {
    String snapshot = String(WiFi.status() == WL_CONNECTED ? "W1" : "W0");
    snapshot += client.connected() ? "|M1" : "|M0";
    snapshot += mqttConfirmed ? "|C1" : "|C0";
    snapshot += solicitudAltaInicialEnviada ? "|A1" : "|A0";
    snapshot += "|U" + userID;

    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) == 0) continue;
        snapshot += "|" + String(configDispositivos[i].mac) + ":" +
                    (configDispositivos[i].activo ? "1" : "0") + ":" +
                    String(configDispositivos[i].tipoDispositivo);
    }

    return snapshot;
}

void debugEstadoDispositivos() {
    static String ultimoSnapshot = "";
    String snapshot = snapshotEstadoMonitoreo();

    if (snapshot == ultimoSnapshot) {
        return; // Sin cambios, no saturar consola
    }

    ultimoSnapshot = snapshot;

    Serial.println("\n=== ESTADO DETALLADO MONITOR01 (cambio detectado) ===");
    Serial.printf("📡 WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "CONECTADO" : "DESCONECTADO");
    Serial.printf("🔗 MQTT: %s\n", client.connected() ? "CONECTADO" : "DESCONECTADO");
    Serial.printf("✅ Alta monitor confirmada: %s\n", mqttConfirmed ? "SI" : "NO");
    Serial.printf("📨 Solicitud alta inicial enviada: %s\n", solicitudAltaInicialEnviada ? "SI" : "NO");
    Serial.printf("🆔 UserID: '%s'\n", userID.c_str());
    Serial.printf("📦 Altas pendientes cada 5min: %s\n", mqttConfirmed ? "ACTIVAS" : "EN ESPERA DE CONFIRMACION");

    Serial.println("\n=== ESTADO DETALLADO NUUP01 ===");
    int total = contarDispositivosRegistrados();
    Serial.printf("📊 Total dispositivos registrados: %d\n", total);

    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            unsigned long desdeLoRa = millis() - ultimaActualizacionLoRa[i];
            Serial.printf("📍 [%02d] MAC: %s | Nombre: %s\n", i, configDispositivos[i].mac, configDispositivos[i].nombre);
            Serial.printf("    ⚡ Activo MQTT: %s | Tipo: %d | %%: %d\n",
                          configDispositivos[i].activo ? "SI" : "NO",
                          configDispositivos[i].tipoDispositivo,
                          configDispositivos[i].porcentaje);
            Serial.printf("    📨 Solicitud alta enviada: %s | Última solicitud: %lu ms\n",
                          solicitudAltaEnviada[i] ? "SI" : "NO",
                          ultimaSolicitudAlta[i]);
            Serial.printf("    🗑️ Baja pendiente: %s | Inicio espera: %lu ms\n",
                          bajaPendienteMQTT[i] ? "SI" : "NO",
                          inicioEsperaBaja[i]);
            Serial.printf("    💧 Litros: %.1f/%.1f | Altura: %.1f | Voltaje: %.2f | Temp: %.1f\n",
                          configDispositivos[i].litrosActuales,
                          configDispositivos[i].litrosConfig,
                          configDispositivos[i].alturaConfig,
                          configDispositivos[i].voltaje,
                          configDispositivos[i].temperatura);
            Serial.printf("    🛰️ Último mensaje LoRa hace: %lu ms | Guardado: %s\n",
                          desdeLoRa,
                          ultimoMensajeLoRaDispositivo[i].length() > 0 ? ultimoMensajeLoRaDispositivo[i].c_str() : "SIN DATOS");
        }
    }
    Serial.println("================================\n");
}

 
void setup() {
   // 0. Inicialización básica SERIAL PANTLALLA  EEPROM
  Serial.begin(115200);
    delay(1000);

  Serial.printf("🆕 Inicio Monitor01 - Consecutivo de cambios #%u: bitácora LoRa activa\n",
                CONSECUTIVO_CAMBIO_ACTUAL);

  // Inicializar OLED lo antes posible para evitar llamadas sobre puntero nulo
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Fallo inicializacion OLED");
    while(true);
  }

  displayReady = true;
  Serial.println("OLED inicializado correctamente");
  display.setTextColor(SSD1306_WHITE);
  iniciarWaterDisplay();

    // 11. Inicializar BLE
    iniciarBLE();
    
    Serial.println("Setup completado");

//pinMode(BOTON_S, INPUT_PULLUP);
pinMode(BOTON_W, INPUT_PULLUP);


   // Inicializar timestamps
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        ultimaActualizacionLoRa[i] = millis(); // Iniciar con tiempo actual
        mostrarSinDatos[i] = false;
    }
 

 inicializa_eeprom();
//clearEEPROM();  //solo para configuracion inicial
delay(1000);


  // DEBUG COMPLETO EEPROM AL INICIAR/
  Serial.println("\n💾 ===========================================");
  Serial.println("🔍 DEBUG EEPROM AL INICIAR");
  Serial.println("💾 ===========================================");

  EEPROM.begin(EEPROM_SIZE);

  // Leer flag de registro
  byte registroFlag = EEPROM.read(0);
  Serial.printf("📋 Flag de registro en addr 0: %d\n", registroFlag);
  registroMonitorEEPROM = (registroFlag == 1);

  // Leer userID
  int userIDLen = EEPROM.read(USER_ID_ADDR);
  if (userIDLen == 0xFF || userIDLen < 0 || userIDLen > USER_ID_MAX_LEN) {
    Serial.printf("📋 UserID corrupto: longitud leída %d, se restablece a 0\n", userIDLen);
    userIDLen = 0;
  }

  char userIDBuffer[USER_ID_MAX_LEN + 1] = {0};
  for (int i = 0; i < userIDLen; i++) {
    userIDBuffer[i] = EEPROM.read(USER_ID_ADDR + 1 + i);
  }
  Serial.printf("📋 UserID en EEPROM: '%s' (longitud: %d)\n", userIDBuffer, userIDLen);
  
  // Leer datos de dispositivos
  int addr = CONFIG_DISPOSITIVOS_ADDR;
  int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
  Serial.printf("📋 Contador de dispositivos: %d\n", count);
  
  EEPROM.end();
  
  Serial.println("💾 ===========================================\n");
  
   // FIN DEBUG COMPLETO EEPROM AL INICIAR
  Serial.println("\n💾 ===========================================");
  Serial.println("🔍 FIN DEBUG EEPROM AL INICIAR");
  Serial.println("💾 ===========================================");








// 2. Cargar configuración Wiffi existente y USER_ID capturado por usuario
    if (!loadNetworksFromEEPROM()) {
      Serial.println("Error al cargar redes de EEPROM");
    }
  // MOSTRAR qué redes se cargaron
  debugNetworks();


// 3. Cargar configuración del usuario
  loadUserProfileFromEEPROM();

  if (userID.length() == 0 || userID[0] > 127) {
    Serial.println("🔄 users_registro_id corrupto detectado, limpiando...");
    userID = "";
    saveUserProfileToEEPROM();
  }

  //ID guardado
  if (userID.isEmpty()) {
    Serial.println("No hay users_registro_id en EEPROM");
  } else {
    Serial.println("users_registro_id cargado desde EEPROM");
  }
delay(1000);

 // 4. Manejo de arreglo de redes WiFi si existe alguna actualmente dada de alta

 bool hasNetworks = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      hasNetworks = true;
      break;
    }
  }

// 6. Inicialización LoRa con manejo de errores mejorado
iniciarLoRaConReintentos();

// Se elimina el envío de prueba para no interferir con confirmaciones reales
Serial.println("🎯 LoRa listo para escuchar (sin mensajes de prueba)");
imprimirResumenLoRa("arranque en núcleo único");


//7. configura DISPOSITIVOS
// Cargar dispositivos registrados
cargarDispositivos();  //
// Cargar bajas pendientes persistentes (portal/BLE sin WiFi)
cargarBajasPendientesEEPROM();
delay(1000);
testDispositivosRapido();  // ← Agrega esta línea solo poner para debug



debugEEPROMReal();
delay(1000);

    // VERIFICACIÓN EXTRA
    verificarEstadoConfigDispositivos();

 //Wiffi
 //8. attemptReconnectToAllNetworks();
 if (WiFi.status() != WL_CONNECTED) {
  attemptReconnectToAllNetworks();
}

 // Aviso breve de WiFi solo si seguimos desconectados
 if (WiFi.status() != WL_CONNECTED) {
   mostrarWifiInicioTemporal();
 }

 // 9. Configuración MQTT con parámetros mejorados
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);  // Buffer aumentado para mensajes grandes
  client.setKeepAlive(60);    // Keepalive de 60 segundos
  client.setSocketTimeout(30); // Timeout de 30 segundos
delay(1000);

// 10. Alta de monitor
mqttConfirmed = loadMQTTConfirmationState(); // Cargar estado persistente
solicitudAltaInicialEnviada = loadSolicitudAltaInicialState();

// Si la EEPROM fue limpiada (flag de registro en 0) pero quedó una confirmación previa, reiniciarla
if (!registroMonitorEEPROM && mqttConfirmed) {
  Serial.println("Estado MQTT: Limpieza detectada, se borra confirmación previa");
  mqttConfirmed = false;
  guardarMQTTConfirmationState(false);
}

// Si ya estaba confirmado, imprimir mensaje
if(mqttConfirmed) {
  Serial.println("Estado MQTT: Confirmación alta encontrada en EEPROM");
  if (!solicitudAltaInicialEnviada) {
    solicitudAltaInicialEnviada = true; // Mantener sincronizada la bandera de solicitud
    guardarSolicitudAltaInicialState(true);
  }
  activarDispositivosTrasConfirmacion();
} else {
  Serial.println("Estado MQTT: Esperando configuracion de alta  inicial");
}
if (solicitudAltaInicialEnviada && !mqttConfirmed) {
  Serial.println("Estado MQTT: Solicitud de alta inicial ya enviada, en espera de confirmación");
}

  mostrarResumenEstadoInicial();

Serial.println("Setup completado");

//testWiFiConnection();


}



// Modificar el loop principal para manejar ambas animaciones
void loop() {

  if (LORA_BIDIRECCIONAL_BORRAR) {
    LORA_bidireccional_borrar();
    delay(INTERVALO_BIDIRECCIONAL_LORA_MS);
    return;
  }

  if (loraProcesando) {
    delay(5);
    return;
  }

  manejarBotonWifi();

  if (factoryResetEnProceso) {
    manejarFlujoFactoryReset();
    return;
  }

  // Si el portal está activo, dedicamos el ciclo completo a atenderlo y evitamos reinicios
  bool apActivo = (WiFi.getMode() & WIFI_MODE_AP) || apMode || forceAPMode;
  if (apActivo) {
    dnsServer.processNextRequest();
    // Atender varias peticiones HTTP por ciclo para mantener la página siempre disponible
    for (int i = 0; i < 3; i++) {
      server.handleClient();
    }
    procesarEscaneoRedes();
    // Mantener la página fija: sin reescaneos automáticos que puedan interrumpir la sesión
    // Si el usuario ya abrió la página evitamos reactivar animaciones agresivas
    if (!portalEnUso) {
      if (animandoWifi && millis() - ultimoCambioWifi >= INTERVALO_WIFI) {
        frameWifi = (frameWifi + 1) % 4;
        ultimoCambioWifi = millis();
      }
      mostrarConexionWifi();
    }
    if (mostrarMensajeConexion) {
      unsigned long elapsed = millis() - inicioMensajeConexion;
      if (elapsed >= retrasoMensajeConexion) {
        dibujarMensajeConexion();
      }
      if (elapsed >= retrasoMensajeConexion + duracionMensajeConexion) {
        dnsServer.stop();
        server.stop();
        ESP.restart();
      }
    }
    if (reinicioSolicitado && !portalEnUso && millis() >= reinicioProgramado) {
      dnsServer.stop();
      server.stop();
      ESP.restart();
    }
    return;
  }

  if (reinicioSolicitado && millis() >= reinicioProgramado) {
    forceAPMode = false;
    wifiConfigInProgress = false;
    ESP.restart();
  }

  if (mostrarMensajeConexion) {
    unsigned long elapsed = millis() - inicioMensajeConexion;
    if (elapsed >= retrasoMensajeConexion) {
      dibujarMensajeConexion();
    }
    if (elapsed >= retrasoMensajeConexion + duracionMensajeConexion) {
      ESP.restart();
    }
    return;
  }

// Debug periódico de nombres
static unsigned long lastDebugNombres = 0;
if (millis() - lastDebugNombres > 15000) {
    lastDebugNombres = millis();
    debugNombresDispositivos();
}


   static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 10000) {
        lastDebug = millis();
        debugNombreProblema();
    }

// Verificar tiempos sin datos
    verificarTiemposSinDatos();

      //    2. Recepción de LoRa (solo datos, no comandos de registro/baja)
    recepcion_lora();  

// Llamar en loop():
testLoRaPeriodico();



  // 0. Manejar BLE (antes de todo)
    manejarBLE();
      
    debugEstadoDispositivos();
    
    
    // 1. Procesar solicitudes BLE pendientes (COMPLETAMENTE MODIFICADO)
    if (solicitudAltaBLE || solicitudBajaBLE) {
        static unsigned long inicioAnimacion = 0;
        static int faseAnimacion = 0; // 0: giro, 1: resultado, 2: mensaje baja, 3: reinicio

        if (inicioAnimacion == 0) {
            inicioAnimacion = millis();
            faseAnimacion = 0;
        }

        unsigned long transcurrido = millis() - inicioAnimacion;

        if (faseAnimacion == 0) { // Animación giratoria (3s)
            if (transcurrido <= 3000) {
                if (millis() - ultimoCambioAnimacion >= INTERVALO_ANIMACION) {
                    frameAnimacion++;
                    ultimoCambioAnimacion = millis();
                }
                mostrarEmparejamiento();
            } else {
                faseAnimacion = 1;
                inicioAnimacion = millis();
            }
        } else if (faseAnimacion == 1) { // Resultado genérico (5s)
            if (transcurrido <= 5000) {
                mostrarResultadoOperacion();
            } else if (solicitudBajaBLE) {
                faseAnimacion = 2;
                inicioAnimacion = millis();
            } else {
                solicitudAltaBLE = false;
                solicitudBajaBLE = false;
                inicioAnimacion = 0;
                faseAnimacion = 0;
                ultimoNombreDispositivo = "";
                ultimosLitros = 0;
                ultimaAltura = 0;
                detenerEmparejamiento();
            }
        } else if (faseAnimacion == 2) { // Mensaje final de baja (5s con nombre grande)
            if (transcurrido <= 5000) {
                mostrarMensajeBajaFinal();
            } else {
                bool borradoRAM = macBajaEnCurso.isEmpty() || obtenerIndiceDispositivo(macBajaEnCurso) == -1;
                bool borradoEEPROM = macBajaEnCurso.isEmpty() || !macPresenteEnEEPROM(macBajaEnCurso);

                Serial.printf("🧹 Verificación baja - RAM:%s | EEPROM:%s\n",
                              borradoRAM ? "OK" : "PENDIENTE",
                              borradoEEPROM ? "OK" : "PENDIENTE");

                macBajaEnCurso = "";

                faseAnimacion = 3;
                inicioAnimacion = millis();
                detenerEmparejamiento();
                reinicioSolicitado = true;
                reinicioProgramado = millis() + 20000;
            }
        } else if (faseAnimacion == 3) { // Aviso de reinicio y esperar reinicio programado
            mostrarMensajeReinicioBaja();
            // Mantener flags hasta reinicio
        }

        // Reforzar reconexión y reenvío de bajas MQTT aun durante animación (incluye bajas desde portal AP)
        if (!apMode && !forceAPMode) {
            if (WiFi.status() != WL_CONNECTED) {
                attemptReconnectToAllNetworks();
            }

            if (WiFi.status() == WL_CONNECTED) {
                if (!client.connected()) {
                    reconnect();
                }

                client.loop();

                if (client.connected()) {
                    solicitarAltaMonitorMQTT();
                    procesarAltasPendientes();
                    procesarBajasPendientes();
                }
            }
        }

        return; // Salir del loop mientras se muestra animación/resultado
    }


    // 3. Comportamiento en recepción continua
    // 5. VERIFICACIÓN MÁS ROBUSTA DE CONEXIÓN WIFI
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 10000) { // Cada 10 segundos
        lastWifiCheck = millis();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi desconectado - Intentando reconexión...");
            wifiConectado = false;
            bool redesDisponibles = attemptReconnectToAllNetworks();

            if (WiFi.status() != WL_CONNECTED) {
                if (!redesDisponibles) {
                    conteoReintentosWiFi = 0;
                    static unsigned long ultimoAvisoSinRedes = 0;
                    if (millis() - ultimoAvisoSinRedes > 30000) {
                        Serial.println("⚠️  No hay redes guardadas; se omite la activación automática del portal. Usa el botón WiFi para configurarlo.");
                        ultimoAvisoSinRedes = millis();
                    }
                } else {
                    conteoReintentosWiFi++;
                    Serial.printf("Intento de reconexión fallido #%d\n", conteoReintentosWiFi);

                    if (conteoReintentosWiFi >= 3 && !forceAPMode) {
                        Serial.println("⚠️  Sin WiFi tras 3 intentos. Usa el botón WiFi para abrir el portal de configuración.");
                        mostrarAvisoPortalAutomatico();
                        conteoReintentosWiFi = 0;
                    }
                }
            }
        } else if (!wifiConectado) {
            Serial.println("WiFi reconectado exitosamente");
            wifiConectado = true;
            conteoReintentosWiFi = 0;
        } else {
            conteoReintentosWiFi = 0;
        }
    }

    // 6. En caso de que tenga WIFFI conectado a MQTT
    // y este dado de alta envia el dato si envia lo que recibio en LORA
    if (WiFi.status() == WL_CONNECTED && client.connected() && mqttConfirmed && !forceAPMode) {
        // Publicar inmediatamente si hay datos y estamos conectados
        if (nuevoMensajeLoRa) {
            asegurarMacMonitorFija("mqtt_lora");
            String macDestino = extraerMacDeMensajeLoRa(mensajeLoRa);
            String macTopico = normalizarMac(macMonitorFija);

            if (macTopico.isEmpty()) {
                Serial.println("⚠️  No se envió telemetría MQTT: MAC del monitor vacía (tópico NUUP/<MAC_MONITOR>)");
            } else {
                String topico = String(TOPICO_LORA_BASE) + macTopico;

                Serial.println("\n📡 [MQTT][TX] Telemetría hacia broker");
                Serial.printf("   Topic   : %s\n", topico.c_str());
                Serial.printf("   Payload : %s\n", mensajeLoRa.c_str());
                Serial.println("   ✅ Solicitud: validar DEVICE_MODIFICACION y responder en NUUP/<MONITOR>/confirmacion/");
                Serial.printf("   ℹ️ Ruta fija por monitor: NUUP/%s (MAC del sensor viaja en el payload)\n", macTopico.c_str());

                if (client.publish(topico.c_str(), mensajeLoRa.c_str())) {
                    Serial.println("   📤 Enviada correctamente. Esperando confirmación del broker...");
                    esperandoConfirmacionBroker = true;
                    macEsperandoConfirmacion = macDestino;
                    inicioEsperaConfirmacion = millis();
                    String topicoConfirmacion = obtenerTopicoConfirmacionMonitor();
                    Serial.printf("   🔎 Espera: broker debe responder en %s con %s,modificar,<alias>,<altura>,<capacidad>,<litros>\n",
                                  topicoConfirmacion.c_str(), macEsperandoConfirmacion.c_str());
                    Serial.printf("      Alternativamente: %s,sin_cambios o %s,modificacion_ok/modificacion_aplicada\n",
                                  macEsperandoConfirmacion.c_str(), macEsperandoConfirmacion.c_str());
                    Serial.printf("   ⏱️ MAC en espera de confirmación: %s (timeout: %lus)\n", macEsperandoConfirmacion.c_str(), timeoutConfirmacionBroker / 1000);
                } else {
                    Serial.println("   ❌ Error al publicar telemetría al broker");
                }
            }

            nuevoMensajeLoRa = false; //solo publicar una vez el mensaje y esperar a otro nuevo
        }
    }

    // 7. Manejo básico de conexiones MQTT
    if (!client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode) {
        reconnect();  //Solo para reconectar y configuracion de subscripciones
    }
    client.loop();

    // 8. Solicitar alta en broker si está conectado
    if (client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode) {
        solicitarAltaMonitorMQTT();  //Para solicitar el alta en broker
        procesarAltasPendientes();
        procesarBajasPendientes();
    }

    if (esperandoConfirmacionBroker && (millis() - inicioEsperaConfirmacion) > timeoutConfirmacionBroker) {
        Serial.printf("⏳ Sin respuesta del broker para %s después de %lus; se libera la espera y se reintentará en la siguiente telemetría\n",
                      macEsperandoConfirmacion.c_str(), timeoutConfirmacionBroker / 1000);
        esperandoConfirmacionBroker = false;
        macEsperandoConfirmacion = "";
    }

    // 9. Verificación periódica de memoria (solo para debug)
    static unsigned long lastMemoryCheck = 0;
    const unsigned long memoryCheckInterval = 30000; // 30 segundos en milisegundos
    if (millis() - lastMemoryCheck > memoryCheckInterval) {
        lastMemoryCheck = millis();
        checkMemory();
    }

    // 10. Manejo de reconexión WiFi
    if (WiFi.status() != WL_CONNECTED && !forceAPMode) {
        wifiConectado = false;
        Reintentar_Wiffi();
    } else {
        wifiConectado = true;
    }

    // 11. Mostrar pantalla normal si no estamos en modo AP
// 11. Mostrar pantalla normal si no estamos en modo AP
// 11. Mostrar pantalla normal si no estamos en modo AP
if(!forceAPMode){
    // Actualizar contador de dispositivos
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    // ⭐⭐ CORREGIR: Evitar división por cero
    if (cantidadDispositivos == 0) {
        // Mostrar pantalla de "Sin Dispositivos"
        display.clearDisplay();
        dibujarHeader();
        dibujarTituloDispositivo();
        dibujarContenidoPrincipal();
        display.display();
        actualizarWaterDisplay(0);
        delay(100);
        return; // Salir temprano
    }
    
    // Configurar intervalo de rotación
    if (cantidadDispositivos <= 1) {
        INTERVALO_CAMBIO = 0; // No rotar si hay 0 o 1 dispositivo
    } else {
        INTERVALO_CAMBIO = 3000; // 3 segundos por dispositivo
    }
    
    // ⭐⭐ SEGURO: cantidadDispositivos es al menos 1
    Dispositivo dispActual = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
    actualizarWaterDisplay(dispActual.porcentaje);
    
    // ⭐⭐ SOLO IMPRIMIR SI HAY CAMBIOS
    static String ultimoNombreMostrado = "";
    static int ultimoPorcentajeMostrado = -1;
    static int ultimosLitrosMostrados = -1;
    static float ultimaBateriaMostrada = -1.0;
    
 // En el loop principal, reemplaza esta sección:
if (dispActual.nombre != ultimoNombreMostrado || 
    dispActual.porcentaje != ultimoPorcentajeMostrado ||
    dispActual.litros != ultimosLitrosMostrados ||
    dispActual.bateria != ultimaBateriaMostrada) {
    
    // Actualizar valores de referencia
    ultimoNombreMostrado = dispActual.nombre;
    ultimoPorcentajeMostrado = dispActual.porcentaje;
    ultimosLitrosMostrados = dispActual.litros;
    ultimaBateriaMostrada = dispActual.bateria;
    
    // ⭐⭐ SOLO IMPRIMIR CAMBIOS SIGNIFICATIVOS (cada 30 segundos o cuando cambie)
    static unsigned long ultimoPrintPantalla = 0;
    if (millis() - ultimoPrintPantalla > 30000) { // Cada 30 segundos
        ultimoPrintPantalla = millis();
        Serial.println("📊 Pantalla - " + dispActual.nombre + 
                     " - " + String(dispActual.porcentaje) + "% - " + 
                     String(dispActual.litros) + "L - " + 
                     String(dispActual.bateria) + "V");
    }
}
    
    display.clearDisplay();
    dibujarHeader();
    dibujarTituloDispositivo();
    dibujarContenidoPrincipal();
    
    // Rotar dispositivos solo si hay más de 1
    if (cantidadDispositivos > 1 && INTERVALO_CAMBIO > 0) {
        if (millis() - ultimoCambio >= INTERVALO_CAMBIO) {
            dispositivoActual = (dispositivoActual + 1) % cantidadDispositivos;
            ultimoCambio = millis();
            
            // ⭐⭐ IMPRIMIR SOLO AL ROTAR (opcional)
            Serial.println("🔄 Rotando a siguiente dispositivo");
        }
    }
    
    display.display();
    delay(100);
}



  // DEBUG DE ESTADO: SOLO AL INICIO O CUANDO CAMBIE ALGÚN ESTADO
    static String ultimoSnapshotEstado = "";
    String snapshotActual = "";

    bool wifiConectado = WiFi.status() == WL_CONNECTED;
    bool mqttConectado = client.connected();

    snapshotActual += wifiConectado ? "1" : "0";
    snapshotActual += "|" + String(mqttConectado);
    snapshotActual += "|" + String(mqttConfirmed);
    snapshotActual += "|" + String(solicitudAltaInicialEnviada);
    snapshotActual += "|" + String(deviceConnected);
    snapshotActual += "|" + String(contarDispositivosRegistrados());
    snapshotActual += "|" + String(userID);

    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == "") continue;
        snapshotActual += "|" + String(configDispositivos[i].mac) + ":" +
                          String(configDispositivos[i].activo) + ":" +
                          String(solicitudAltaEnviada[i]) + ":" +
                          String(bajaPendienteMQTT[i]);
    }

    if (ultimoSnapshotEstado != snapshotActual) {
        ultimoSnapshotEstado = snapshotActual;

        Serial.println("\n🛰️ ===== ESTADO DE MONITOR01 =====");
        Serial.printf("📡 WiFi: %s (RSSI %d dBm)\n", wifiConectado ? "CONECTADO" : "DESCONECTADO", wifiConectado ? WiFi.RSSI() : 0);
        Serial.printf("🔗 MQTT: %s | Confirmado: %s | Alta inicial enviada: %s\n", mqttConectado ? "CONECTADO" : "DESCONECTADO", mqttConfirmed ? "SI" : "NO", solicitudAltaInicialEnviada ? "SI" : "NO");
        Serial.printf("👤 UserID: %s\n", userID.c_str());
        Serial.printf("🛂 Subscripciones: alta/0/confirmacion/, alta/1/confirmacion/, baja/1/confirmacion/, %s/command, %s/estatus\n", serial_number.c_str(), serial_number.c_str());
        Serial.printf("📱 BLE: %s\n", deviceConnected ? "CONECTADO" : "DESCONECTADO");
        Serial.printf("💾 Dispositivos registrados: %d\n", contarDispositivosRegistrados());
        Serial.println("🛰️ ================================\n");

        Serial.println("📟 ===== ESTADO DE CADA NUUP01 =====");
        for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
            if (String(configDispositivos[i].mac) == "") continue;

            String resumenLoRa = ultimoMensajeLoRaDispositivo[i];
            if (resumenLoRa.length() > 60) {
                resumenLoRa = resumenLoRa.substring(0, 60) + "...";
            }

            Serial.printf("[%02d] MAC: %s | Nombre: '%s' | ActivoMQTT: %s | AltaSolicitada: %s | BajaPendiente: %s\n",
                          i,
                          configDispositivos[i].mac,
                          configDispositivos[i].nombre,
                          configDispositivos[i].activo ? "SI" : "NO",
                          solicitudAltaEnviada[i] ? "SI" : "NO",
                          bajaPendienteMQTT[i] ? "SI" : "NO");
            Serial.printf("     Últimos datos: %.2fL | %.2fV | %.2f°C | AlturaCfg %.2f | LitrosCfg %.2f | %% %d\n",
                          configDispositivos[i].litrosActuales,
                          configDispositivos[i].voltaje,
                          configDispositivos[i].temperatura,
                          configDispositivos[i].alturaConfig,
                          configDispositivos[i].litrosConfig,
                          configDispositivos[i].porcentaje);
            Serial.printf("     Último LoRa guardado: %s\n", resumenLoRa.c_str());
        }
        Serial.println("📟 ==================================\n");
    }



}


void debugNombresDispositivos() {
    Serial.println("\n=== DEBUG NOMBRES DISPOSITIVOS ===");
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("📍 [%d] MAC: %s, Nombre: '%s'\n", 
                         i, configDispositivos[i].mac, configDispositivos[i].nombre);
        }
    }
    Serial.println("==================================\n");
}

void verificarTiemposSinDatos() {
    static unsigned long ultimaVerificacion = 0;
    if (millis() - ultimaVerificacion > 10000) { // Cada 10 segundos
        ultimaVerificacion = millis();
        
        for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
            if (String(configDispositivos[i].mac) != "") {
                if (millis() - ultimaActualizacionLoRa[i] > TIEMPO_SIN_DATOS && !mostrarSinDatos[i]) {
                    Serial.println("⚠️  Dispositivo " + String(i) + " sin datos por más de " + String(TIEMPO_SIN_DATOS/1000) + " segundos");
                    mostrarSinDatos[i] = true;
                }
            }
        }
    }
}


void debugNombreProblema() {
    Serial.println("\n=== DEBUG NOMBRE PROBLEMA ===");
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("📍 [%d] MAC: %s, Nombre: '%s'\n", 
                         i, configDispositivos[i].mac, configDispositivos[i].nombre);
        }
    }
    Serial.println("==============================\n");
}

//Temporal Debug
void debugMensajeLoRa(const String &mensaje) {
    Serial.println("\n🔍 DEBUG DETALLADO MENSAJE LoRa:");
    Serial.println("Mensaje completo: '" + mensaje + "'");
    
    // Mostrar posiciones de todas las comas
    Serial.print("Posiciones de comas: ");
    for (int i = 0; i < mensaje.length(); i++) {
        if (mensaje.charAt(i) == ',') {
            Serial.print(i + " ");
        }
    }
    Serial.println();
    
    // Mostrar campos individuales
    int start = 0;
    int fieldCount = 0;
    for (int i = 0; i <= mensaje.length(); i++) {
        if (i == mensaje.length() || mensaje.charAt(i) == ',') {
            String field = mensaje.substring(start, i);
            Serial.printf("Campo %d: '%s' (longitud: %d)\n", fieldCount, field.c_str(), field.length());
            start = i + 1;
            fieldCount++;
        }
    }
    Serial.println("🔍 FIN DEBUG MENSAJE\n");
}


//Temporal debug
void testDispositivosRapido() {
    Serial.println("\n🧪 ===========================================");
    Serial.println("🔍 TEST RÁPIDO - DISPOSITIVOS REGISTRADOS");
    Serial.println("🧪 ===========================================");
    
    int totalRegistrados = 0;
    
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            totalRegistrados++;
            Serial.printf("📍 [%d] MAC: %s\n", i, configDispositivos[i].mac);
            Serial.printf("   📝 Nombre: '%s'\n", configDispositivos[i].nombre);
            Serial.printf("   📊 Porcentaje: %d%%\n", configDispositivos[i].porcentaje);
            Serial.printf("   💧 Litros Actuales: %.1f L\n", configDispositivos[i].litrosActuales);
            Serial.printf("   📏 Litros Config: %.1f L\n", configDispositivos[i].litrosConfig);
            Serial.printf("   📐 Altura Config: %.1f cm\n", configDispositivos[i].alturaConfig);
            Serial.printf("   🔋 Voltaje: %.2f V\n", configDispositivos[i].voltaje);
            Serial.printf("   🌡️ Temperatura: %.1f °C\n", configDispositivos[i].temperatura);
            Serial.printf("   ⚡ Activo: %s\n", configDispositivos[i].activo ? "SI" : "NO");
            Serial.printf("   🔧 Tipo: %d\n", configDispositivos[i].tipoDispositivo);
            
            // Verificar si hay datos recientes
            unsigned long tiempoSinDatos = millis() - ultimaActualizacionLoRa[i];
            Serial.printf("   ⏰ Tiempo sin datos: %lu ms\n", tiempoSinDatos);
            Serial.printf("   📡 Estado: %s\n", tiempoSinDatos > TIEMPO_SIN_DATOS ? "SIN DATOS" : "ACTIVO");
            
            Serial.println("   ─────────────────────────");
        }
    }
    
    Serial.printf("📊 TOTAL DISPOSITIVOS REGISTRADOS: %d de %d\n", totalRegistrados, MAX_DISPOSITIVOS);
    
    if (totalRegistrados == 0) {
        Serial.println("❌ NO HAY DISPOSITIVOS REGISTRADOS");
        Serial.println("💡 Usa BLE para registrar un dispositivo primero");
    } else {
        Serial.println("✅ Dispositivos cargados correctamente");
    }
    
    Serial.println("🧪 ===========================================\n");
}

// Llamar esta función después de registrar un dispositivo via BLE
