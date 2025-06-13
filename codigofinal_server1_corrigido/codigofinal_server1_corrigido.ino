#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <AdafruitIO_WiFi.h>

// === CONFIGURAÇÕES ===
#define ID "server1"
#define DHTPIN 4
#define DHTTYPE DHT11
#define LED_PIN 2
#define INTERVALO_ENVIO_MS 10000  

// === CREDENCIAIS WI-FI ===
// Não são necessárias para os nós não-master, mas mantidas para compatibilidade
#define WIFI_SSID "EL-BIGODON9305"
#define WIFI_PASS "333333333"

// === Adafruit IO ===
// Não são necessárias para os nós não-master, mas mantidas para compatibilidade
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

DHT dht(DHTPIN, DHTTYPE);
bool isMaster = false; // Este é o server1, não o master
bool espNowAtivo = false;
bool emModoReceber = true;
unsigned long ultimoTrocaModo = 0;
const unsigned long INTERVALO_TROCA_MS = 15000;

// === ADAFRUIT IO CLIENT ===
// Não usado neste ESP, mas mantido para compatibilidade
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// === FUNÇÕES AUXILIARES ===
void piscarLED(int tempo = 50) {
  digitalWrite(LED_PIN, HIGH);
  delay(tempo);
  digitalWrite(LED_PIN, LOW);
}

void debugSerial(const dados_esp& dados) {
  Serial.println("--- Dados Recebidos ---");
  Serial.print("ID: "); Serial.println(dados.id);
  Serial.print("dado01 (Temp): "); Serial.println(dados.dado01);
  Serial.print("dado02 (Umid): "); Serial.println(dados.dado02);
  Serial.println("------------------------\n");
}

void enviarDados(const dados_esp& pacote, bool piscarLongo = false) {
  esp_now_send(broadcastAddress, (uint8_t *)&pacote, sizeof(pacote));
  piscarLED(piscarLongo ? 300 : 50);
}

void montarEEnviarInternos() {
  dados_esp dados;
  strcpy(dados.id, ID);

  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  dados.dado01 = isnan(temperatura) ? 0 : (int)temperatura;
  dados.dado02 = isnan(umidade) ? 0 : (int)umidade;
  dados.dado03 = 0;
  dados.dado04 = 0;
  dados.dado05 = 0;

  Serial.println("--- Dados Locais do Server1 ---");
  Serial.print("Temperatura: "); Serial.println(dados.dado01);
  Serial.print("Umidade: "); Serial.println(dados.dado02);
  Serial.println("-----------------------------\n");

  enviarDados(dados, true);
}

// === CALLBACKS ESP-NOW ===
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&dadosRecebidos, incomingData, sizeof(dadosRecebidos));
  debugSerial(dadosRecebidos);

  if (String(dadosRecebidos.id) != ID) {
    unsigned long agora = millis();
    if (ultimoIDRecebido != String(dadosRecebidos.id) || (agora - ultimoTempoID > 5000)) {
      ultimoIDRecebido = String(dadosRecebidos.id);
      ultimoTempoID = agora;
      enviarDados(dadosRecebidos, false);
    }
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  WiFi.mode(WIFI_STA);
  iniciarESPNow();

  ultimoEnvio = millis();
  ultimoTrocaModo = millis();
}

void iniciarESPNow() {
  Serial.println("Função Esp Now iniciada");
  esp_now_deinit(); // sempre limpa antes

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao reiniciar ESP-NOW");
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

// === LOOP PRINCIPAL ===
void loop() {
  unsigned long agora = millis();
  
  // Este bloco não será executado pois isMaster = false
  if (isMaster && agora - ultimoTrocaModo >= INTERVALO_TROCA_MS) {
    // Código do master (não usado aqui)
  }

  // Este bloco será executado pois isMaster = false
  if (!isMaster) {
    if (agora - ultimoEnvio >= INTERVALO_ENVIO_MS) {
      montarEEnviarInternos();
      ultimoEnvio = agora;
    }
  }

  delay(100);
}
