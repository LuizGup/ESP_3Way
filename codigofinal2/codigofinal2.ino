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

DHT dht(DHTPIN, DHTTYPE);
bool isMaster = false;
bool espNowAtivo = false;
bool emModoReceber = true;
unsigned long ultimoTrocaModo = 0;
const unsigned long INTERVALO_TROCA_MS = 15000;

// === ADAFRUIT IO CLIENT ===
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

AdafruitIO_Feed *temperatura = io.feed("temperatura"); //Pub
AdafruitIO_Feed *umidade = io.feed("umidade"); //Pub

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

  enviarDados(dados, true);
}

void conectarWiFi() {
  Serial.println("Conectando ao Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long timeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - timeout < 5000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado ao Wi-Fi!");
  } else {
    Serial.println("\nFalha na conexão Wi-Fi");
  }
}

void enviarParaAdafruit() {
  Serial.println("Enviando dados para Adafruit...");

  Serial.print("Connecting to Adafruit IO");
  io.connect();

  // wait for a connection
  while(io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConectado ao Adafruit IO!");

if (!temperatura->save((int32_t)dadosRecebidos.dado01)) {
      Serial.println("Falha ao enviar temperatura");
    } else {
      Serial.println("Temperatura enviada com sucesso!");
    }

if (!umidade->save((int32_t)dadosRecebidos.dado02)) {
      Serial.println("Falha ao enviar umidade");
    } else {
      Serial.println("Umidade enviada com sucesso!");
    }
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
  

  if (isMaster && agora - ultimoTrocaModo >= INTERVALO_TROCA_MS) {
    ultimoTrocaModo = agora;
    emModoReceber = !emModoReceber;

    if (emModoReceber) {
      Serial.println("Modo: ESP-NOW (recebendo)");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      iniciarESPNow();
    } else {
      Serial.println("10 segundos de Delay para finaliar os callbacks");
      delay(10000);

      Serial.println("Modo: Wi-Fi (enviando para Adafruit)");
      io.run();
      esp_now_del_peer(broadcastAddress);
      esp_now_unregister_recv_cb();
      esp_now_unregister_send_cb();
      esp_now_deinit();
      conectarWiFi();
      enviarParaAdafruit();
    }
  }

  if (!isMaster) {
    if (agora - ultimoEnvio >= INTERVALO_ENVIO_MS) {
      montarEEnviarInternos();
      ultimoEnvio = agora;
    }
  }

delay(100);
}