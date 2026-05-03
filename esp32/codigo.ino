// ============================================================
// Autoras:
// Lara Modif Essa Alssabak - RM567947
// Maria Luisa Boucinhas Franco - RM567355
// Maria Luiza Kochnoff da Matta - RM568459
// Roberta Moreira dos Santos - RM567825
//
// Projeto: CP5 — Vinheria Agnello (FIWARE IoT)
//
// Descrição:
// Sistema embarcado com ESP32 para monitoramento ambiental
// de uma vinheria, utilizando sensores de temperatura,
// umidade e luminosidade, com envio de dados via MQTT.
//
// Hardware:
// - ESP32
// - Sensor DHT11 (temperatura e umidade)
// - LDR digital (luminosidade)
// - Buzzer (alerta sonoro)
// - LED externo (alerta visual)
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// Configurações de rede e MQTT (FIWARE)
// ============================================================

// Credenciais Wi-Fi
const char* default_SSID     = "REDE";
const char* default_PASSWORD = "SENHA";

// Broker MQTT (FIWARE / Orion Context Broker)
const char* default_BROKER_MQTT = "104.197.156.52";
const int   default_BROKER_PORT = 1883;

// Tópicos MQTT (padrão FIWARE)
const char* default_TOPICO_SUBSCRIBE = "/TEF/vinheriaAgnello001/cmd";
const char* default_TOPICO_PUBLISH   = "/TEF/vinheriaAgnello001/attrs";
const char* default_TOPICO_CMDEXE    = "/TEF/vinheriaAgnello001/cmdexe";

// Identificação do dispositivo
const char* default_ID_MQTT = "fiware_vinheria001";

// LED externo
const int default_LED_PIN = 5;

// Variáveis editáveis (permitem customização dinâmica)
char* SSID             = const_cast<char*>(default_SSID);
char* PASSWORD         = const_cast<char*>(default_PASSWORD);
char* BROKER_MQTT      = const_cast<char*>(default_BROKER_MQTT);
int   BROKER_PORT      = default_BROKER_PORT;
char* TOPICO_SUBSCRIBE = const_cast<char*>(default_TOPICO_SUBSCRIBE);
char* TOPICO_PUBLISH   = const_cast<char*>(default_TOPICO_PUBLISH);
char* TOPICO_CMDEXE    = const_cast<char*>(default_TOPICO_CMDEXE);
char* ID_MQTT          = const_cast<char*>(default_ID_MQTT);
int   LED_PIN          = default_LED_PIN;

// ============================================================
// Definição de pinos (hardware)
// ============================================================

#define DHT_PIN    4     // Sensor DHT11 (dados)
#define DHT_TYPE   DHT11
#define LDR_PIN    34    // Sensor de luminosidade (entrada digital)
#define BUZZER_PIN 32    // Buzzer (saída digital)

// ============================================================
// Limiares ideais da vinheria
// ============================================================
// Esses valores definem quando um alerta deve ser disparado

const float TEMP_MIN  = 12.0;
const float TEMP_MAX  = 18.0;
const float HUMID_MIN = 60.0;
const float HUMID_MAX = 80.0;
const int   LUX_MAX   = 30;

// Intervalo de envio de dados (ms)
const unsigned long PUBLISH_INTERVAL = 5000;

// ============================================================
// Objetos globais
// ============================================================

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient   espClient;
PubSubClient MQTT(espClient);

// ============================================================
// Estado do sistema
// ============================================================

bool remoteAlertOn   = false;  // Alerta ativado remotamente (FIWARE)
bool buzzerSilenced  = false;  // Buzzer desativado manualmente
bool ledBlinkState   = false;  // Estado atual do LED

unsigned long lastPublish = 0; // Controle de envio MQTT
unsigned long lastBlink   = 0; // Controle de piscar LED

// ============================================================
// Inicialização
// ============================================================

void initSerial() {
  Serial.begin(115200);
}

// Conecta ao Wi-Fi (bloqueante até conectar)
void initWiFi() {
  Serial.println("------ Conexao WI-FI ------");
  Serial.print("Rede: ");
  Serial.println(SSID);

  reconectWiFi();
}

// Configura o cliente MQTT
void initMQTT() {
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);

  // Define função de callback para mensagens recebidas
  MQTT.setCallback(mqtt_callback);

  // Aumenta buffer para suportar payload maior
  MQTT.setBufferSize(512);
}

// Configuração inicial de pinos
void InitOutput() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);

  // Feedback visual de inicialização
  for (int i = 0; i <= 10; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(200);
  }

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

// ============================================================
// Setup (executa uma vez)
// ============================================================

void setup() {
  InitOutput();
  initSerial();

  dht.begin();     // Inicializa sensor
  initWiFi();      // Conecta Wi-Fi
  initMQTT();      // Conecta MQTT

  delay(3000);

  // Informa ao FIWARE que o dispositivo está online
  MQTT.publish(TOPICO_PUBLISH, "s|on");
}

// ============================================================
// Loop principal (executa continuamente)
// ============================================================

void loop() {
  VerificaConexoesWiFIEMQTT(); // Garante conexão ativa
  handleSensors();             // Processa sensores
  MQTT.loop();                 // Mantém comunicação MQTT
}

// ============================================================
// Reconexões
// ============================================================

// Reconecta ao Wi-Fi caso necessário
void reconectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi conectado!");
}

// Reconecta ao broker MQTT
void reconnectMQTT() {
  while (!MQTT.connected()) {
    if (MQTT.connect(ID_MQTT)) {
      MQTT.subscribe(TOPICO_SUBSCRIBE);
    } else {
      delay(2000);
    }
  }
}

// Verifica e mantém conexões ativas
void VerificaConexoesWiFIEMQTT() {
  if (!MQTT.connected()) reconnectMQTT();
  reconectWiFi();
}

// ============================================================
// Callback MQTT (recebe comandos do FIWARE)
// ============================================================

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  String msg = "";
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];

  // Interpretação de comandos remotos
  if (msg.indexOf("@alert_on") >= 0) {
    remoteAlertOn = true;
    buzzerSilenced = false;
    MQTT.publish(TOPICO_CMDEXE, "alert_on|OK");
  }

  else if (msg.indexOf("@alert_off") >= 0) {
    remoteAlertOn = false;
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    MQTT.publish(TOPICO_CMDEXE, "alert_off|OK");
  }

  else if (msg.indexOf("@silence_buzzer") >= 0) {
    buzzerSilenced = true;
    digitalWrite(BUZZER_PIN, LOW);
    MQTT.publish(TOPICO_CMDEXE, "silence_buzzer|OK");
  }
}