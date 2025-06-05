#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>

// --- Configurações ---
#define DHTPIN_ESP2 4       // Pino onde o sensor DHT11 do ESP2 está conectado (ajuste se necessário)
#define DHTTYPE_ESP2 DHT11  // Tipo do sensor DHT

// !!! IMPORTANTE: Substitua pelos endereços MAC dos SEUS ESP32 Nº 1 e Nº 3 !!!
uint8_t esp1_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // MAC do ESP1 (para referência/verificação, opcional)
uint8_t esp3_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // MAC do ESP3 (Destino)

// --- Estrutura de Dados para a Cascata ---
// Esta estrutura DEVE SER IDÊNTICA à dos outros ESPs
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

// Informações do peer (ESP3)
esp_now_peer_info_t peerInfo_esp3;

// Instância do sensor DHT para ESP2
DHT dht_esp2(DHTPIN_ESP2, DHTTYPE_ESP2);

// Flag para indicar se dados do ESP1 foram recebidos recentemente
volatile bool esp1_data_received = false;

// --- Funções Callback ESP-NOW ---

// Callback executado quando dados são RECEBIDOS
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Verifica se o tamanho dos dados recebidos corresponde à estrutura
  if (len == sizeof(cascade_data)) {
    // Copia os dados recebidos para a estrutura local
    memcpy(&cascade_data, incomingData, sizeof(cascade_data));
    Serial.println("\n--- Dados Recebidos do ESP1 ---");
    Serial.print("MAC Remetente: ");
    for(int i=0; i<6; i++) { Serial.printf("%02X%s", mac[i], (i<5)?":":""); }
    Serial.println();
    
    if (cascade_data.data1_valid) {
        Serial.printf("ESP1 -> Temp: %.2f C, Hum: %.2f %%\n", cascade_data.temp1, cascade_data.hum1);
    } else {
        Serial.println("ESP1 -> Dados inválidos ou falha na leitura.");
    }

    // Marca que dados foram recebidos para processamento no loop ou aqui mesmo
    esp1_data_received = true; 

    // --- Processamento e Retransmissão --- 
    // Lê o sensor local (ESP2)
    float h2 = dht_esp2.readHumidity();
    float t2 = dht_esp2.readTemperature();

    // Verifica a leitura do sensor local
    if (isnan(h2) || isnan(t2)) {
        Serial.println("Falha ao ler do sensor DHT do ESP2!");
        cascade_data.temp2 = 0.0 / 0.0; // NaN
        cascade_data.hum2 = 0.0 / 0.0; // NaN
        cascade_data.data2_valid = false;
    } else {
        // Adiciona os dados do ESP2 à estrutura
        cascade_data.temp2 = t2;
        cascade_data.hum2 = h2;
        cascade_data.data2_valid = true;
        Serial.printf("ESP2 -> Temp: %.2f C, Hum: %.2f %%\n", cascade_data.temp2, cascade_data.hum2);
    }

    // Invalida dados do ESP3 (serão preenchidos por ele)
    cascade_data.temp3 = 0.0 / 0.0; // NaN
    cascade_data.hum3 = 0.0 / 0.0; // NaN
    cascade_data.data3_valid = false;

    // Envia a estrutura ATUALIZADA para o ESP3
    esp_err_t result = esp_now_send(esp3_mac_address, (uint8_t *) &cascade_data, sizeof(cascade_data));
    if (result == ESP_OK) {
        Serial.println("Estrutura agregada enviada com sucesso para ESP3.");
    } else {
        Serial.println("Erro ao enviar estrutura agregada para ESP3.");
    }
    // -------------------------------------

  } else {
    Serial.println("Recebido pacote com tamanho incorreto.");
  }
}

// Callback executado quando os dados são ENVIADOS (para ESP3)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nStatus do último envio para ESP3: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Entregue com Sucesso" : "Falha na Entrega");
  Serial.println("-----------------------------");
}

// --- Setup ---
void setup() {
  // Inicializa a Serial
  Serial.begin(115200);
  Serial.println("\n--- ESP32 2: Coletor/Retransmissor (Cascata) ---");
 
  // Configura o dispositivo como Estação Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address ESP2: ");
  Serial.println(WiFi.macAddress()); // Imprime o MAC deste ESP para configurar no ESP1

  // Inicializa o ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }

  // Registra os callbacks de envio e recebimento
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
 
  // Configura e registra o peer (ESP3)
  memcpy(peerInfo_esp3.peer_addr, esp3_mac_address, 6);
  peerInfo_esp3.channel = 0;  
  peerInfo_esp3.encrypt = false;
   
  // Adiciona o peer (ESP3)       
  if (esp_now_add_peer(&peerInfo_esp3) != ESP_OK){
    Serial.println("Falha ao adicionar peer (ESP3)");
    return;
  }
  Serial.println("Peer ESP3 adicionado com sucesso.");

  // Inicializa o sensor DHT do ESP2
  dht_esp2.begin();
  Serial.println("Sensor DHT do ESP2 inicializado.");
  Serial.println("Aguardando dados do ESP1...");
}
 
// --- Loop ---
void loop() {
  // O processamento principal ocorre no callback OnDataRecv
  // O loop pode ser usado para tarefas de baixa prioridade ou ficar vazio
  delay(100); // Pequeno delay para estabilidade
}

