#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <AdafruitIO_WiFi.h>

// === CONFIGURAÇÕES ===
#define ID "Master"
#define DHTPIN 4
#define DHTTYPE DHT11
#define LED_PIN 2
#define INTERVALO_ENVIO_MS 10000
#define INTERVALO_TROCA_MS 15000
#define DEBUG true

// === CREDENCIAIS WI-FI ===
#define WIFI_SSID "EL-BIGODON9305"
#define WIFI_PASS "333333333"

// === Adafruit IO ===
#define IO_USERNAME "LuizGup"
#define IO_KEY "aio_ilxL72oXYXdX2QW6pRxUls6nOlEp"

// === STRUCT DE DADOS ===
typedef struct dados_esp {
  char id[30];
  int dado01; // temperatura
  int dado02; // umidade
  int dado03;
  int dado04;
  int dado05;
} dados_esp;

// === VARIÁVEIS GLOBAIS ===
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
dados_esp dadosRecebidos;
String ultimoIDRecebido = "";
unsigned long ultimoTempoID = 0;
unsigned long ultimoEnvio = 0;
unsigned long ultimoTrocaModo = 0;

bool isMaster = true;
bool emModoReceber = true;

DHT dht(DHTPIN, DHTTYPE);
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *temperatura = io.feed("temperatura");
AdafruitIO_Feed *umidade = io.feed("umidade");

// === FUNÇÕES AUXILIARES ===
void piscarLED(int tempo = 50) {
  digitalWrite(LED_PIN, HIGH);
  delay(tempo);
  digitalWrite(LED_PIN, LOW);
}

void debugSerial(const dados_esp& dados) {
#if DEBUG
  Serial.println("--- Dados Recebidos ---");
  Serial.printf("ID: %s\nTemp: %d °C\nUmid: %d %%\n", dados.id, dados.dado01, dados.dado02);
  Serial.println("------------------------\n");
#endif
}

void enviarDados(const dados_esp& pacote, bool piscarLongo = false) {
  esp_now_send(broadcastAddress, (uint8_t *)&pacote, sizeof(pacote));
  piscarLED(piscarLongo ? 300 : 50);
}

bool montarPacoteSensor(dados_esp& dados) {
  float temp = dht.readTemperature();
  float umid = dht.readHumidity();
  if (isnan(temp) || isnan(umid)) return false;

  strcpy(dados.id, ID);
  dados.dado01 = (int)temp;
  dados.dado02 = (int)umid;
  dados.dado03 = dados.dado04 = dados.dado05 = 0;
  return true;
}

void conectarWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long timeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - timeout < 5000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWi-Fi conectado!" : "\nFalha na conexão Wi-Fi.");
}

void enviarParaAdafruit() {
  Serial.println("Enviando dados para Adafruit...");
  io.connect();
  unsigned long timeout = millis();
  while (io.status() < AIO_CONNECTED && millis() - timeout < 5000) {
    Serial.print(".");
    delay(500);
  }

  if (io.status() != AIO_CONNECTED) {
    Serial.println("\nFalha ao conectar no Adafruit IO!");
    return;
  }
  Serial.println("\nConectado ao Adafruit IO!");

  if (!temperatura->save(dadosRecebidos.dado01)) Serial.println("Erro ao enviar temperatura");
  else Serial.println("Temperatura enviada!");

  if (!umidade->save(dadosRecebidos.dado02)) Serial.println("Erro ao enviar umidade");
  else Serial.println("Umidade enviada!");
}

// === CALLBACKS ESP-NOW ===
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&dadosRecebidos, incomingData, sizeof(dadosRecebidos));
  debugSerial(dadosRecebidos);

  if (String(dadosRecebidos.id) != ID) {
    unsigned long agora = millis();
    if (ultimoIDRecebido != dadosRecebidos.id || (agora - ultimoTempoID > 5000)) {
      ultimoIDRecebido = dadosRecebidos.id;
      ultimoTempoID = agora;
      enviarDados(dadosRecebidos);
    }
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Silencioso
}

void iniciarESPNow() {
  Serial.println("Iniciando ESP-NOW");
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }

  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Erro ao adicionar peer");
    }
  }
}

// === SETUP E LOOP ===
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  WiFi.mode(WIFI_STA);
  iniciarESPNow();

  ultimoEnvio = millis();
  ultimoTrocaModo = millis();
}

void loop() {
  unsigned long agora = millis();

  if (isMaster && agora - ultimoTrocaModo >= INTERVALO_TROCA_MS) {
    ultimoTrocaModo = agora;
    emModoReceber = !emModoReceber;

    if (emModoReceber) {
      Serial.println(">> Modo: ESP-NOW (Recebendo)");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      iniciarESPNow();
    } else {
      Serial.println(">> Esperando 10s antes de desconectar callbacks...");
      delay(10000);

      Serial.println(">> Modo: Wi-Fi (Enviando para Adafruit)");
      io.run();
      esp_now_deinit();
      esp_now_unregister_recv_cb();
      esp_now_unregister_send_cb();
      conectarWiFi();
      enviarParaAdafruit();
    }
  }

  if (!isMaster && agora - ultimoEnvio >= INTERVALO_ENVIO_MS) {
    dados_esp dados;
    if (montarPacoteSensor(dados)) {
      enviarDados(dados, true);
    } else {
      Serial.println("Falha na leitura do DHT!");
    }
    ultimoEnvio = agora;
  }

  delay(100);
}
