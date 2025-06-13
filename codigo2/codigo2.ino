#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>
#include "AdafruitIO_WiFi.h"

#define IO_USERNAME    "arthursena1201"
#define IO_KEY         "aio_pavK20GwVGfV5yOswluJxT41hzX1"
#define WIFI_SSID      "iPhone Matheus"
#define WIFI_PASS      "msl170704"

#define ID "ESP1"
#define DHTPIN 4
#define DHTTYPE DHT11
#define INTERVALO_ENVIO_MS 10000

DHT dht(DHTPIN, DHTTYPE);
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *feedTempRemota = io.feed("temperatura_remota");
AdafruitIO_Feed *feedUmidRemota = io.feed("umidade_remota");

typedef struct dados_esp {
  char id[10];
  int temperatura;
  int umidade;
} dados_esp;

dados_esp dadosRecebidos;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

int tempRemota = 0;
int umidRemota = 0;

unsigned long ultimoEnvio = 0;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&dadosRecebidos, incomingData, sizeof(dadosRecebidos));

  if (strcmp(dadosRecebidos.id, ID) != 0) {
    tempRemota = dadosRecebidos.temperatura;
    umidRemota = dadosRecebidos.umidade;

    Serial.println("[Dados Recebidos do outro ESP]");
    Serial.print("ID: "); Serial.println(dadosRecebidos.id);
    Serial.print("Temp: "); Serial.println(tempRemota);
    Serial.print("Umid: "); Serial.println(umidRemota);
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("[Envio] Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sucesso" : "Falha");
}

void enviarDadosLocais() {
  float temp = dht.readTemperature();
  float umid = dht.readHumidity();

  dados_esp dados;
  strcpy(dados.id, ID);
  dados.temperatura = isnan(temp) ? 0 : (int)temp;
  dados.umidade = isnan(umid) ? 0 : (int)umid;

  esp_now_send(broadcastAddress, (uint8_t *)&dados, sizeof(dados));

  Serial.println("[Dados Enviados]");
  Serial.print("Temp: "); Serial.println(dados.temperatura);
  Serial.print("Umid: "); Serial.println(dados.umidade);
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");

  io.connect();
  while(io.status() < AIO_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Conectado ao Adafruit IO");

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Erro ao adicionar peer");
    return;
  }

  ultimoEnvio = millis();
}

void loop() {
  io.run();

  unsigned long agora = millis();
  if (agora - ultimoEnvio >= INTERVALO_ENVIO_MS) {
    enviarDadosLocais();
    ultimoEnvio = agora;

    if (tempRemota != 0 || umidRemota != 0) {
      feedTempRemota->save(tempRemota);
      feedUmidRemota->save(umidRemota);
      Serial.println("[Enviado ao Adafruit IO]");
    }
  }
}