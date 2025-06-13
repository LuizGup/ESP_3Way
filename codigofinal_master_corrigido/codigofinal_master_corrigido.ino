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
#define WIFI_SSID "REPLACE_ME"
#define WIFI_PASS "REPLACE_ME"

// === Adafruit IO ===
#define IO_USERNAME "REPLACE_ME"
#define IO_KEY "REPLACE_ME"

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
dados_esp dadosLocais;  // Nova variável para armazenar dados locais do Master
String ultimoIDRecebido = "";
unsigned long ultimoTempoID = 0;
unsigned long ultimoEnvio = 0;
unsigned long ultimoEnvioLocal = 0; // Novo timestamp para controle de envio local

// Armazenamento de dados de cada ESP
dados_esp dadosServer1;
dados_esp dadosServer2;
bool dadosServer1Atualizados = false;
bool dadosServer2Atualizados = false;

DHT dht(DHTPIN, DHTTYPE);
bool isMaster = true;
bool espNowAtivo = false;
bool emModoReceber = true;
unsigned long ultimoTrocaModo = 0;
const unsigned long INTERVALO_TROCA_MS = 15000;

// === ADAFRUIT IO CLIENT ===
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Feeds individuais para cada ESP
AdafruitIO_Feed *master_temp = io.feed("master-temp");
AdafruitIO_Feed *master_hum = io.feed("master-hum");
AdafruitIO_Feed *server1_temp = io.feed("server1-temp");
AdafruitIO_Feed *server1_hum = io.feed("server1-hum");
AdafruitIO_Feed *server2_temp = io.feed("server2-temp");
AdafruitIO_Feed *server2_hum = io.feed("server2-hum");

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

// Função para ler e montar dados locais do Master
void montarDadosLocais() {
  strcpy(dadosLocais.id, ID);

  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  dadosLocais.dado01 = isnan(temperatura) ? 0 : (int)temperatura;
  dadosLocais.dado02 = isnan(umidade) ? 0 : (int)umidade;
  dadosLocais.dado03 = 0;
  dadosLocais.dado04 = 0;
  dadosLocais.dado05 = 0;

  Serial.println("--- Dados Locais do Master ---");
  Serial.print("Temperatura: "); Serial.println(dadosLocais.dado01);
  Serial.print("Umidade: "); Serial.println(dadosLocais.dado02);
  Serial.println("-----------------------------\n");
}

// Função para enviar dados locais do Master via ESP-NOW
void montarEEnviarInternos() {
  montarDadosLocais();
  enviarDados(dadosLocais, true);
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

  // Publicar dados do Master (locais)
  Serial.println("Publicando dados do Master...");
  if (!master_temp->save((int32_t)dadosLocais.dado01)) {
    Serial.println("Falha ao enviar temperatura do Master");
  } else {
    Serial.println("Temperatura do Master enviada com sucesso!");
  }

  if (!master_hum->save((int32_t)dadosLocais.dado02)) {
    Serial.println("Falha ao enviar umidade do Master");
  } else {
    Serial.println("Umidade do Master enviada com sucesso!");
  }

  // Publicar dados do Server1 (se disponíveis)
  if (dadosServer1Atualizados) {
    Serial.println("Publicando dados do Server1...");
    if (!server1_temp->save((int32_t)dadosServer1.dado01)) {
      Serial.println("Falha ao enviar temperatura do Server1");
    } else {
      Serial.println("Temperatura do Server1 enviada com sucesso!");
    }

    if (!server1_hum->save((int32_t)dadosServer1.dado02)) {
      Serial.println("Falha ao enviar umidade do Server1");
    } else {
      Serial.println("Umidade do Server1 enviada com sucesso!");
    }
  }

  // Publicar dados do Server2 (se disponíveis)
  if (dadosServer2Atualizados) {
    Serial.println("Publicando dados do Server2...");
    if (!server2_temp->save((int32_t)dadosServer2.dado01)) {
      Serial.println("Falha ao enviar temperatura do Server2");
    } else {
      Serial.println("Temperatura do Server2 enviada com sucesso!");
    }

    if (!server2_hum->save((int32_t)dadosServer2.dado02)) {
      Serial.println("Falha ao enviar umidade do Server2");
    } else {
      Serial.println("Umidade do Server2 enviada com sucesso!");
    }
  }
}

// === CALLBACKS ESP-NOW ===
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&dadosRecebidos, incomingData, sizeof(dadosRecebidos));
  debugSerial(dadosRecebidos);

  // Armazenar dados recebidos com base no ID
  if (strcmp(dadosRecebidos.id, "server1") == 0) {
    memcpy(&dadosServer1, &dadosRecebidos, sizeof(dadosRecebidos));
    dadosServer1Atualizados = true;
    Serial.println("Dados do Server1 atualizados!");
  } 
  else if (strcmp(dadosRecebidos.id, "server2") == 0) {
    memcpy(&dadosServer2, &dadosRecebidos, sizeof(dadosRecebidos));
    dadosServer2Atualizados = true;
    Serial.println("Dados do Server2 atualizados!");
  }

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
  ultimoEnvioLocal = millis();
  ultimoTrocaModo = millis();
  
  // Inicializar dados locais
  montarDadosLocais();
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
  
  // Master também deve coletar seus próprios dados periodicamente
  if (isMaster && agora - ultimoEnvioLocal >= INTERVALO_ENVIO_MS) {
    montarDadosLocais(); // Apenas coleta, não envia via ESP-NOW
    ultimoEnvioLocal = agora;
  }

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
