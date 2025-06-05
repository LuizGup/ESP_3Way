#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// --- Configurações ---
#define DHTPIN_ESP3 4       // Pino onde o sensor DHT11 do ESP3 está conectado (ajuste se necessário)
#define DHTTYPE_ESP3 DHT11  // Tipo do sensor DHT

// !!! IMPORTANTE: Substitua pelos seus dados !!!
#define WLAN_SSID       "SUA_REDE_WIFI"     // Nome da sua rede Wi-Fi
#define WLAN_PASS       "SUA_SENHA_WIFI"    // Senha da sua rede Wi-Fi
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "SEU_USUARIO_ADAFRUIT" // Seu usuário Adafruit IO
#define AIO_KEY         "SUA_CHAVE_ADAFRUIT"   // Sua chave Adafruit IO

// MAC do ESP2 (Opcional, para referência/verificação)
uint8_t esp2_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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
struct_message final_data;

// --- Cliente Wi-Fi e MQTT ---
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// --- Feeds MQTT para Publicação ---
// !!! Crie feeds no Adafruit IO com estes nomes !!!
Adafruit_MQTT_Publish tempFeed1 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temperatura_esp1");
Adafruit_MQTT_Publish humFeed1  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/umidade_esp1");
Adafruit_MQTT_Publish tempFeed2 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temperatura_esp2");
Adafruit_MQTT_Publish humFeed2  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/umidade_esp2");
Adafruit_MQTT_Publish tempFeed3 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temperatura_esp3");
Adafruit_MQTT_Publish humFeed3  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/umidade_esp3");

// Instância do sensor DHT para ESP3
DHT dht_esp3(DHTPIN_ESP3, DHTTYPE_ESP3);

// Variável para controle de tempo MQTT
unsigned long previousMillisMQTT = 0;
const long intervalMQTT = 10000; // Intervalo para verificar/pingar conexão MQTT (10 segundos)

// --- Funções Callback ESP-NOW ---

// Callback executado quando dados são RECEBIDOS do ESP2
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Verifica se o tamanho dos dados recebidos corresponde à estrutura
  if (len == sizeof(final_data)) {
    // Copia os dados recebidos para a estrutura local
    memcpy(&final_data, incomingData, sizeof(final_data));
    Serial.println("\n--- Dados Recebidos do ESP2 ---");
    Serial.print("MAC Remetente (ESP2): ");
    for(int i=0; i<6; i++) { Serial.printf("%02X%s", mac[i], (i<5)?":":""); }
    Serial.println();

    // Imprime dados recebidos (ESP1 e ESP2)
    if (final_data.data1_valid) {
        Serial.printf("ESP1 (via ESP2) -> Temp: %.2f C, Hum: %.2f %%\n", final_data.temp1, final_data.hum1);
    } else { Serial.println("ESP1 (via ESP2) -> Dados inválidos."); }
    if (final_data.data2_valid) {
        Serial.printf("ESP2 (via ESP2) -> Temp: %.2f C, Hum: %.2f %%\n", final_data.temp2, final_data.hum2);
    } else { Serial.println("ESP2 (via ESP2) -> Dados inválidos."); }

    // --- Processamento Local e Publicação MQTT ---
    // Lê o sensor local (ESP3)
    float h3 = dht_esp3.readHumidity();
    float t3 = dht_esp3.readTemperature();

    // Verifica a leitura do sensor local
    if (isnan(h3) || isnan(t3)) {
        Serial.println("Falha ao ler do sensor DHT do ESP3!");
        final_data.temp3 = 0.0 / 0.0; // NaN
        final_data.hum3 = 0.0 / 0.0; // NaN
        final_data.data3_valid = false;
    } else {
        // Adiciona os dados do ESP3 à estrutura
        final_data.temp3 = t3;
        final_data.hum3 = h3;
        final_data.data3_valid = true;
        Serial.printf("ESP3 (Local) -> Temp: %.2f C, Hum: %.2f %%\n", final_data.temp3, final_data.hum3);
    }

    // Tenta conectar ao MQTT se não estiver conectado
    MQTT_connect(); 

    // Publica os dados válidos no Adafruit IO
    if (mqtt.connected()) {
        Serial.println("Publicando dados no Adafruit IO...");
        if (final_data.data1_valid) {
            tempFeed1.publish(final_data.temp1);
            humFeed1.publish(final_data.hum1);
        }
        if (final_data.data2_valid) {
            tempFeed2.publish(final_data.temp2);
            humFeed2.publish(final_data.hum2);
        }
        if (final_data.data3_valid) {
            tempFeed3.publish(final_data.temp3);
            humFeed3.publish(final_data.hum3);
        }
        Serial.println("Publicação MQTT concluída (ou tentada).");
    } else {
        Serial.println("MQTT não conectado. Não foi possível publicar.");
    }
    Serial.println("-----------------------------------");
    // ---------------------------------------------

  } else {
    Serial.println("Recebido pacote ESP-NOW com tamanho incorreto.");
  }
}

// --- Função de Conexão MQTT (Baseada no seu exemplo) ---
void MQTT_connect() {
  int8_t ret;

  // Para se já estiver conectado
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Conectando ao MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect() retorna 0 em sucesso
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Tentando novamente em 5 segundos...");
       mqtt.disconnect();
       delay(5000);  // espera 5 segundos
       retries--;
       if (retries == 0) {
         // Desiste após 3 tentativas
         Serial.println("Falha ao conectar ao MQTT após múltiplas tentativas.");
         // Não trava aqui, permite que o ESP-NOW continue funcionando
         return; 
       }
  }
  Serial.println("MQTT Conectado!");
}

// --- Setup ---
void setup() {
  // Inicializa a Serial
  Serial.begin(115200);
  Serial.println("\n--- ESP32 3: Coletor Final / Gateway MQTT (Cascata) ---");
 
  // Configura o Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  Serial.print("Conectando ao Wi-Fi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi conectado!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address ESP3: ");
  Serial.println(WiFi.macAddress()); // Imprime o MAC deste ESP para configurar no ESP2

  // Inicializa o ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }

  // Registra o callback de recebimento
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW inicializado. Aguardando dados do ESP2...");

  // Inicializa o sensor DHT do ESP3
  dht_esp3.begin();
  Serial.println("Sensor DHT do ESP3 inicializado.");
}

// --- Loop ---
void loop() {
  // Garante que a conexão MQTT está ativa ou tenta reconectar
  unsigned long currentMillis = millis();
  if (!mqtt.connected()) {
      // Tenta reconectar apenas se passou o intervalo, para não bloquear o loop
      if (currentMillis - previousMillisMQTT >= intervalMQTT) {
          previousMillisMQTT = currentMillis;
          Serial.println("Tentando conectar/reconectar ao MQTT...");
          MQTT_connect();
      }
  } else {
      // Mantém a conexão MQTT ativa
      mqtt.processPackets(100); // Processa pacotes MQTT recebidos (se houver subscrições)
      
      // Pinga o servidor periodicamente para manter a conexão viva
      if (currentMillis - previousMillisMQTT >= intervalMQTT) {
          previousMillisMQTT = currentMillis;
          if(! mqtt.ping()) {
              Serial.println("Falha no Ping MQTT. Desconectando.");
              mqtt.disconnect();
          }
      }
  }

  // O recebimento e processamento principal ocorrem no callback OnDataRecv
  delay(100); // Pequeno delay para estabilidade
}

