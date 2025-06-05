#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>

// --- Configurações ---
#define DHTPIN_ESP1 4       // Pino onde o sensor DHT11 do ESP1 está conectado
#define DHTTYPE_ESP1 DHT11  // Tipo do sensor DHT

// !!! IMPORTANTE: Substitua pelo endereço MAC do SEU ESP32 Nº 2 !!!
uint8_t esp2_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- Estrutura de Dados para a Cascata ---
// Esta estrutura será passada entre os ESPs
typedef struct struct_message {
  // Dados do ESP1
  float temp1;
  float hum1;
  bool data1_valid;

  // Dados do ESP2
  float temp2;
  float hum2;
  bool data2_valid;

  // Dados do ESP3
  float temp3;
  float hum3;
  bool data3_valid;
} struct_message;

// Cria uma instância da estrutura para este ESP
struct_message cascade_data;

// Informações do peer (ESP2)
esp_now_peer_info_t peerInfo;

// Instância do sensor DHT para ESP1
DHT dht_esp1(DHTPIN_ESP1, DHTTYPE_ESP1);

// --- Funções Callback ESP-NOW ---

// Callback executado quando os dados são enviados
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nStatus do último envio para ESP2: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Entregue com Sucesso" : "Falha na Entrega");
}
 
// --- Setup ---
void setup() {
  // Inicializa a Serial
  Serial.begin(115200);
  Serial.println("--- ESP32 1: Coletor Inicial (Cascata) ---");
 
  // Configura o dispositivo como Estação Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address ESP1: ");
  Serial.println(WiFi.macAddress()); // Imprime o MAC deste ESP para referência

  // Inicializa o ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }

  // Registra o callback de envio
  esp_now_register_send_cb(OnDataSent);
 
  // Configura e registra o peer (ESP2)
  memcpy(peerInfo.peer_addr, esp2_mac_address, 6);
  peerInfo.channel = 0;  // Canal padrão
  peerInfo.encrypt = false; // Sem criptografia para simplificar
   
  // Adiciona o peer (ESP2)       
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Falha ao adicionar peer (ESP2)");
    return;
  }
  Serial.println("Peer ESP2 adicionado com sucesso.");

  // Inicializa o sensor DHT do ESP1
  dht_esp1.begin();
  Serial.println("Sensor DHT do ESP1 inicializado.");
}
 
// --- Loop ---
void loop() {
  // Lê a umidade e temperatura do sensor local (ESP1)
  float h1 = dht_esp1.readHumidity();
  float t1 = dht_esp1.readTemperature(); // Lê em Celsius

  // Preenche a estrutura de dados
  // Zera/invalida os dados dos outros ESPs antes de preencher os próprios
  cascade_data.temp2 = 0.0 / 0.0; // NaN
  cascade_data.hum2 = 0.0 / 0.0; // NaN
  cascade_data.data2_valid = false;
  cascade_data.temp3 = 0.0 / 0.0; // NaN
  cascade_data.hum3 = 0.0 / 0.0; // NaN
  cascade_data.data3_valid = false;

  // Verifica se a leitura do sensor local falhou
  if (isnan(h1) || isnan(t1)) {
    Serial.println("Falha ao ler do sensor DHT do ESP1!");
    cascade_data.temp1 = 0.0 / 0.0; // NaN indica falha
    cascade_data.hum1 = 0.0 / 0.0; // NaN indica falha
    cascade_data.data1_valid = false;
  } else {
    // Preenche os dados do ESP1 na estrutura
    cascade_data.temp1 = t1;
    cascade_data.hum1 = h1;
    cascade_data.data1_valid = true;
    Serial.printf("Dados ESP1 -> Temp: %.2f C, Hum: %.2f %%\n", cascade_data.temp1, cascade_data.hum1);
  }

  // Envia a estrutura completa para o ESP2 via ESP-NOW
  esp_err_t result = esp_now_send(esp2_mac_address, (uint8_t *) &cascade_data, sizeof(cascade_data));
   
  if (result == ESP_OK) {
    Serial.println("Estrutura de dados enviada com sucesso para ESP2.");
  } else {
    Serial.println("Erro ao enviar estrutura de dados para ESP2.");
  }

  // Espera 15 segundos antes da próxima leitura/envio
  delay(15000); 
}

