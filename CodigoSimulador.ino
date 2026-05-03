// Autor base: Fábio Henrique Cabrini
// Adaptado CP5: Vinheria Agnello — FIWARE IoT
// Hardware: ESP32 + DHT-22 + LDR (módulo 4 pinos AO) + Buzzer + LED externo
//
// Resumo:
//   - Publica temperatura, umidade e luminosidade no FIWARE via MQTT UltraLight.
//   - Avalia ambiente contra limiares da vinheria e publica status/alertas.
//   - Recebe comandos remotos: alert_on, alert_off, silence_buzzer.
//
// Pinagem (diagram.json Cabrini):
//   DHT-22  → D4   (D35 é input-only, não funciona com DHT)
//   LDR AO  → D34  (input only — leitura analógica)
//   Buzzer  → D32  (via resistor 1kΩ)
//   LED     → D5   (LED externo azul + resistor no protoboard)

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ─── Configurações de rede e broker ──────────────────────────
const char* default_SSID            = "Wokwi-GUEST";          // Wi-Fi do Wokwi
const char* default_PASSWORD        = "";                      // sem senha no Wokwi
const char* default_BROKER_MQTT     = "34.60.49.205";          // IP da sua VM
const int   default_BROKER_PORT     = 1883;
const char* default_TOPICO_SUBSCRIBE = "/TEF/vinheriaAgnello001/cmd";    // recebe comandos
const char* default_TOPICO_PUBLISH   = "/TEF/vinheriaAgnello001/attrs";  // publica atributos
const char* default_TOPICO_CMDEXE    = "/TEF/vinheriaAgnello001/cmdexe"; // resposta de comando
const char* default_ID_MQTT          = "fiware_vinheria001";
const int   default_LED_PIN          = 5;                     // GPIO 5 — LED externo no protoboard
const char* topicPrefix              = "vinheriaAgnello001";   // prefixo dos comandos

// Variáveis editáveis (mesmo padrão do Cabrini)
char* SSID             = const_cast<char*>(default_SSID);
char* PASSWORD         = const_cast<char*>(default_PASSWORD);
char* BROKER_MQTT      = const_cast<char*>(default_BROKER_MQTT);
int   BROKER_PORT      = default_BROKER_PORT;
char* TOPICO_SUBSCRIBE = const_cast<char*>(default_TOPICO_SUBSCRIBE);
char* TOPICO_PUBLISH   = const_cast<char*>(default_TOPICO_PUBLISH);
char* TOPICO_CMDEXE    = const_cast<char*>(default_TOPICO_CMDEXE);
char* ID_MQTT          = const_cast<char*>(default_ID_MQTT);
int   LED_PIN          = default_LED_PIN;

// ─── Pinos dos sensores e atuadores ──────────────────────────
#define DHT_PIN    4    // D4 — bidirecional, funciona com DHT22
#define DHT_TYPE   DHT22
#define LDR_PIN    34   // D34 — AO do módulo LDR (input only)
#define BUZZER_PIN 32   // D32 — buzzer via resistor 1kΩ

// ─── Limiares da vinheria (alinhados aos static_attributes da coleção) ──
const float TEMP_MIN  = 12.0;  // °C
const float TEMP_MAX  = 18.0;  // °C
const float HUMID_MIN = 60.0;  // %
const float HUMID_MAX = 80.0;  // %
const int   LUX_MAX   = 30;    // % (escala 0–100)

// ─── Intervalo de publicação ─────────────────────────────────
const unsigned long PUBLISH_INTERVAL = 5000;  // ms

// ─── Objetos globais ─────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient   espClient;
PubSubClient MQTT(espClient);

// ─── Estado do dispositivo ───────────────────────────────────
bool remoteAlertOn   = false;   // alerta acionado por alert_on
bool buzzerSilenced  = false;   // silenciado por silence_buzzer (até nova leitura)
bool ledBlinkState   = false;   // estado atual do pisca-pisca (millis)
unsigned long lastPublish = 0;
unsigned long lastBlink   = 0;

// ============================================================
//  Inicializações
// ============================================================
void initSerial() {
  Serial.begin(115200);
}

void initWiFi() {
  delay(10);
  Serial.println("------Conexao WI-FI------");
  Serial.print("Conectando-se na rede: ");
  Serial.println(SSID);
  Serial.println("Aguarde");
  reconectWiFi();
}

void initMQTT() {
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(mqtt_callback);
  MQTT.setBufferSize(512);   // padrão é 256 — payload completo passa de 256 bytes
}

void InitOutput() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Pisca LED na inicialização (igual ao código original do Cabrini)
  boolean toggle = false;
  for (int i = 0; i <= 10; i++) {
    toggle = !toggle;
    digitalWrite(LED_PIN, toggle);
    delay(200);
  }
  digitalWrite(LED_PIN, LOW);
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  InitOutput();
  initSerial();
  dht.begin();
  initWiFi();
  initMQTT();
  delay(3000);

  // Publica estado inicial: device on
  MQTT.publish(TOPICO_PUBLISH, "s|on");
}

// ============================================================
//  Loop
// ============================================================
void loop() {
  VerificaConexoesWiFIEMQTT();
  handleSensors();
  MQTT.loop();
}

// ============================================================
//  Reconexões
// ============================================================
void reconectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Conectado com sucesso na rede ");
  Serial.println(SSID);
  Serial.print("IP obtido: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_PIN, LOW);
}

void reconnectMQTT() {
  while (!MQTT.connected()) {
    Serial.print("* Tentando se conectar ao Broker MQTT: ");
    Serial.println(BROKER_MQTT);
    if (MQTT.connect(ID_MQTT)) {
      Serial.println("Conectado com sucesso ao broker MQTT!");
      MQTT.subscribe(TOPICO_SUBSCRIBE);
    } else {
      Serial.println("Falha ao reconectar no broker.");
      Serial.println("Haverá nova tentativa de conexão em 2s");
      delay(2000);
    }
  }
}

void VerificaConexoesWiFIEMQTT() {
  if (!MQTT.connected()) reconnectMQTT();
  reconectWiFi();
}

// ============================================================
//  Callback — recebe comandos do IoT Agent FIWARE
//  Formato UltraLight: vinheriaAgnello001@<comando>|
//  Comandos provisionados: alert_on, alert_off, silence_buzzer
// ============================================================
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.print("- Mensagem recebida: ");
  Serial.println(msg);

  if (msg.indexOf("@alert_on") >= 0) {
    remoteAlertOn  = true;
    buzzerSilenced = false;
    Serial.println("→ ALERTA REMOTO LIGADO");
    MQTT.publish(TOPICO_CMDEXE, "alert_on|OK");
  }
  else if (msg.indexOf("@alert_off") >= 0) {
    remoteAlertOn  = false;
    buzzerSilenced = false;
    digitalWrite(LED_PIN, LOW);
    noTone(BUZZER_PIN);
    Serial.println("→ ALERTA REMOTO DESLIGADO");
    MQTT.publish(TOPICO_CMDEXE, "alert_off|OK");
  }
  else if (msg.indexOf("@silence_buzzer") >= 0) {
    buzzerSilenced = true;
    noTone(BUZZER_PIN);
    Serial.println("→ BUZZER SILENCIADO (LED continua piscando)");
    MQTT.publish(TOPICO_CMDEXE, "silence_buzzer|OK");
  }
}

// ============================================================
//  Alertas sonoros — tom distinto por anomalia
//    Temperatura : 3 bips curtos agudos  (2000 Hz)
//    Umidade     : 2 bips longos graves  (800 Hz)
//    Luminosidade: bip rápido contínuo   (3500 Hz)
// ============================================================
void alertTemperature() {
  if (buzzerSilenced) return;
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 2000, 150);
    delay(300);
  }
}

void alertHumidity() {
  if (buzzerSilenced) return;
  for (int i = 0; i < 2; i++) {
    tone(BUZZER_PIN, 800, 500);
    delay(700);
  }
}

void alertLuminosity() {
  if (buzzerSilenced) return;
  tone(BUZZER_PIN, 3500, 100);
  delay(200);
}

// ============================================================
//  Pisca LED via millis() — não trava o loop MQTT
// ============================================================
void handleLedBlink(bool shouldBlink) {
  unsigned long now = millis();
  if (shouldBlink && (now - lastBlink >= 300)) {
    lastBlink = now;
    ledBlinkState = !ledBlinkState;
    digitalWrite(LED_PIN, ledBlinkState);
  } else if (!shouldBlink) {
    ledBlinkState = false;
    digitalWrite(LED_PIN, LOW);
  }
}

// ============================================================
//  Leitura, avaliação e publicação dos sensores
//  Atributos publicados (object_id da coleção):
//    s=state, t=temperature, h=humidity, l=luminosity,
//    e=environmentStatus, c=comfortLevel, r=decisionReason,
//    a=alertStatus, y=alertType, g=triggerViolated,
//    b=blueLedState, z=buzzerState
// ============================================================
void handleSensors() {
  unsigned long now = millis();

  // ── Leitura dos sensores ─────────────────────────────────
  // Lemos toda iteração (rápido) pra manter o pisca-pisca responsivo,
  // mas só publicamos a cada PUBLISH_INTERVAL.
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  int   sensorValue = analogRead(LDR_PIN);
  int   luminosity  = map(sensorValue, 0, 4095, 0, 100); // 0–100%

  if (isnan(temperature) || isnan(humidity)) {
    // sem leitura válida ainda — só atualiza pisca e sai
    handleLedBlink(remoteAlertOn);
    return;
  }

  // ── Avalia condições da vinheria ─────────────────────────
  bool tempAlert  = (temperature < TEMP_MIN || temperature > TEMP_MAX);
  bool humidAlert = (humidity    < HUMID_MIN || humidity   > HUMID_MAX);
  bool luxAlert   = (luminosity  > LUX_MAX);
  bool anyAlert   = tempAlert || humidAlert || luxAlert;

  // LED pisca se há alerta local OU se foi acionado remotamente
  handleLedBlink(anyAlert || remoteAlertOn);

  // Só publica e dispara buzzer no intervalo definido
  if (now - lastPublish < PUBLISH_INTERVAL) return;
  lastPublish = now;

  // ── Dispara alertas sonoros (respeita silence_buzzer) ────
  if (tempAlert)  alertTemperature();
  if (humidAlert) alertHumidity();
  if (luxAlert)   alertLuminosity();

  // ── Monta os campos textuais derivados ───────────────────
  // environmentStatus: ok | alert
  // comfortLevel:      ideal | warning | critical
  // decisionReason:    descreve o porquê (texto curto)
  // alertStatus:       active | inactive
  // alertType:         temperature | humidity | luminosity | multiple | none
  // triggerViolated:   t | h | l | t+h | t+l | h+l | t+h+l | none
  // blueLedState:      on | off  (estado lógico — está piscando se on)
  // buzzerState:       on | off  (off se silenciado)
  String environmentStatus = anyAlert ? "alert" : "ok";

  String comfortLevel;
  int alertCount = (tempAlert ? 1 : 0) + (humidAlert ? 1 : 0) + (luxAlert ? 1 : 0);
  if (alertCount == 0)      comfortLevel = "ideal";
  else if (alertCount == 1) comfortLevel = "warning";
  else                      comfortLevel = "critical";

  String decisionReason;
  if (!anyAlert) {
    decisionReason = "todos os parametros dentro da faixa ideal";
  } else {
    decisionReason = "";
    if (tempAlert)  decisionReason += "temperatura fora ("  + String(temperature, 1) + "C); ";
    if (humidAlert) decisionReason += "umidade fora ("       + String(humidity, 1)    + "%); ";
    if (luxAlert)   decisionReason += "luminosidade alta ("  + String(luminosity)     + "%); ";
  }

  String alertType;
  if (alertCount == 0)        alertType = "none";
  else if (alertCount > 1)    alertType = "multiple";
  else if (tempAlert)         alertType = "temperature";
  else if (humidAlert)        alertType = "humidity";
  else                        alertType = "luminosity";

  String triggerViolated = "";
  if (tempAlert)  triggerViolated += "t";
  if (humidAlert) triggerViolated += (triggerViolated.length() ? "+h" : "h");
  if (luxAlert)   triggerViolated += (triggerViolated.length() ? "+l" : "l");
  if (triggerViolated.length() == 0) triggerViolated = "none";

  String alertStatus  = (anyAlert || remoteAlertOn) ? "active" : "inactive";
  String blueLedState = (anyAlert || remoteAlertOn) ? "on"     : "off";
  String buzzerState  = (anyAlert && !buzzerSilenced) ? "on"   : "off";

  // ── Monta payload UltraLight: chave|valor|chave|valor|... ─
  // Importante: NÃO usar '|' dentro dos valores (UltraLight delimita por |)
  // E o Orion v2 também proíbe estes caracteres em valores: < > " ' = ; ( )
  String safeReason = decisionReason;
  safeReason.replace("|", "/");
  safeReason.replace("(", "[");
  safeReason.replace(")", "]");
  safeReason.replace(";", ",");
  safeReason.replace("\"", "'");
  safeReason.replace("<", "");
  safeReason.replace(">", "");
  safeReason.replace("=", "-");

  String payload = "s|on";
  payload += "|t|" + String(temperature, 1);
  payload += "|h|" + String(humidity, 1);
  payload += "|l|" + String(luminosity);
  payload += "|e|" + environmentStatus;
  payload += "|c|" + comfortLevel;
  payload += "|r|" + safeReason;
  payload += "|a|" + alertStatus;
  payload += "|y|" + alertType;
  payload += "|g|" + triggerViolated;
  payload += "|b|" + blueLedState;
  payload += "|z|" + buzzerState;

  bool ok = MQTT.publish(TOPICO_PUBLISH, payload.c_str());
  if (!ok) {
    Serial.println("[ERRO] MQTT.publish() falhou — payload muito grande ou broker desconectado");
  }

  // ── Log Serial ───────────────────────────────────────────
  Serial.println("════════════════════════════════");
  Serial.printf("Temp: %.1f°C  |  Umid: %.1f%%  |  Lux: %d%%\n",
                temperature, humidity, luminosity);
  Serial.print("Status: "); Serial.print(environmentStatus);
  Serial.print(" | Conforto: "); Serial.print(comfortLevel);
  Serial.print(" | Trigger: "); Serial.println(triggerViolated);
  if (anyAlert) Serial.println("⚠ ALERTA ATIVO!");
  else          Serial.println("✓ Valores normais");
  Serial.print("Payload publicado: "); Serial.println(payload);
}
