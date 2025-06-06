#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <map> // Para armazenar dados recentes

// --- Bibliotecas MQTT (Apenas para o Principal) ---
#include <WiFiClient.h>
#include <PubSubClient.h>

// =======================================================
// ===           CONFIGURAÇÕES PRINCIPAIS              ===
// =======================================================

// --- Identificação ÚNICA deste ESP ---
// !!! MUDE ESTE ID PARA CADA ESP (ex: "esp_sala", "esp_quarto", "esp_cozinha") !!!
#define DEVICE_ID "esp_1"

// --- Configuração do Sensor Local ---
#define DHTPIN 4
#define DHTTYPE DHT11

// --- Configuração da Rede ESP-NOW ---
#define CHANNEL 1 // Canal Wi-Fi para ESP-NOW (0-13)

// --- Configuração do Nó Principal (Gateway MQTT) ---
// !!! APENAS UM ESP NA REDE DEVE TER 'true' !!!
#define IS_PRINCIPAL true

// --- Intervalos (em milissegundos) ---
#define INTERVALO_LEITURA_LOCAL_MS 30000  // Ler sensor local a cada 30 segundos
#define INTERVALO_ENVIO_MQTT_MS    60000  // Enviar dados agregados para MQTT a cada 60 segundos (apenas Principal)
#define TEMPO_EXPIRACAO_DADOS_MS   300000 // Considerar dados de outros ESPs expirados após 5 minutos
#define TEMPO_RETRANSMISSAO_MIN_MS 5000   // Não retransmitir o mesmo pacote antes de 5 segundos

// --- Configurações MQTT (Apenas para o Principal) ---
#if IS_PRINCIPAL
  #define WLAN_SSID       "EL-BIGODON9305"     // Substitua pelo nome da sua rede Wi-Fi
  #define WLAN_PASS       "333333333"    // Substitua pela senha da sua rede Wi-Fi
  #define AIO_SERVER      "io.adafruit.com"
  #define AIO_SERVERPORT  1883
  #define AIO_USERNAME    "Nelsola" // Substitua pelo seu usuário Adafruit IO
  #define AIO_KEY         "aio_leZf40A6hGG9hihlZKhxPx4rnOew"   // Substitua pela sua chave Adafruit IO
#endif

// =======================================================
// ===           ESTRUTURAS E VARIÁVEIS GLOBAIS        ===
// =======================================================

// --- Estrutura de Dados ESP-NOW ---
typedef struct struct_message {
  char device_id[32]; // ID do dispositivo que ORIGINOU o dado
  unsigned long timestamp; // millis() de quando o dado foi lido/enviado
  float temperature;
  float humidity;
  // Adicione mais campos se necessário (dado03, 04, 05...)
} struct_message;

struct_message local_data; // Dados lidos localmente

// --- Variáveis ESP-NOW ---
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// --- Controle de Tempo ---
unsigned long ultimoTempoLeituraLocal = 0;
unsigned long ultimoTempoEnvioMQTT = 0;

// --- Armazenamento de Dados (Todos os ESPs para controle de eco/retransmissão) ---
// Mapeia ID do dispositivo -> último timestamp recebido/processado daquele dispositivo
std::map<String, unsigned long> last_seen_timestamp;

// --- Armazenamento de Dados Agregados (Apenas Principal) ---
#if IS_PRINCIPAL
  // Mapeia ID do dispositivo -> última struct de dados recebida daquele dispositivo
  std::map<String, struct_message> aggregated_data;
  WiFiClient espClient;
  PubSubClient mqttClient(espClient);
#endif

// --- Sensor DHT ---
DHT dht(DHTPIN, DHTTYPE);

// =======================================================
// ===           FUNÇÕES AUXILIARES                    ===
// =======================================================

// --- Função para Enviar Dados via ESP-NOW Broadcast ---
void sendDataESPNow(const struct_message& data_to_send) {
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &data_to_send, sizeof(data_to_send));
  if (result == ESP_OK) {
    // Serial.printf("Dados de %s enviados via ESP-NOW.\n", data_to_send.device_id);
  } else {
    Serial.printf("Erro ao enviar dados de %s via ESP-NOW.\n", data_to_send.device_id);
  }
}

// --- Função para Ler Sensor Local e Preparar Dados ---
void readLocalSensor() {
  strcpy(local_data.device_id, DEVICE_ID);
  local_data.timestamp = millis();
  local_data.temperature = dht.readTemperature();
  local_data.humidity = dht.readHumidity();

  if (isnan(local_data.temperature) || isnan(local_data.humidity)) {
    Serial.println("Falha ao ler sensor DHT local!");
    // Opcional: marcar como inválido ou usar valores padrão
    local_data.temperature = -999.0; // Valor inválido
    local_data.humidity = -999.0;    // Valor inválido
  } else {
    Serial.printf("Leitura Local [%s]: Temp=%.1fC, Hum=%.1f%%\n", 
                  local_data.device_id, local_data.temperature, local_data.humidity);
  }

  // Atualiza o timestamp visto para os próprios dados
  last_seen_timestamp[String(DEVICE_ID)] = local_data.timestamp;

  // Se for o principal, armazena os dados locais
  #if IS_PRINCIPAL
    aggregated_data[String(DEVICE_ID)] = local_data;
  #endif

  // Envia os dados locais via broadcast
  sendDataESPNow(local_data);
}

// --- Funções MQTT (Apenas Principal) ---
#if IS_PRINCIPAL

void reconnectMQTT() {
  // Loop até reconectar
  while (!mqttClient.connected()) {
    Serial.print("Tentando conexão MQTT...");
    // Tenta conectar
    if (mqttClient.connect(DEVICE_ID, AIO_USERNAME, AIO_KEY)) {
      Serial.println("conectado!");
      // Se precisar se inscrever em tópicos, faça aqui
      // mqttClient.subscribe("seu/topico/aqui");
    } else {
      Serial.print("falhou, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando novamente em 5 segundos");
      // Espera 5 segundos antes de tentar novamente
      delay(5000);
    }
  }
}

void publishAggregatedData() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
    if (!mqttClient.connected()) {
        Serial.println("Não foi possível conectar ao MQTT para publicar.");
        return;
    }
  }

  Serial.println("--- Publicando Dados Agregados via MQTT ---");
  unsigned long current_time = millis();

  for (auto const& [key_id, val_data] : aggregated_data) {
    if ((current_time - val_data.timestamp) < TEMPO_EXPIRACAO_DADOS_MS) {
      if (val_data.temperature > -990.0 && val_data.humidity > -990.0) {

        // Feeds fixos
        String feed_temp = "Nelsola/feeds/trabalho-final.trabalho";
        String feed_hum  = "Nelsola/feeds/trabalho-final2.trabalho2";
        String feed_extra = "Nelsola/feeds/trabalho-final3.trabalho3";

        char temp_str[8];
        char hum_str[8];
        dtostrf(val_data.temperature, 4, 1, temp_str);
        dtostrf(val_data.humidity, 4, 1, hum_str);

        Serial.printf("Publicando para %s: %s\n", feed_temp.c_str(), temp_str);
        mqttClient.publish(feed_temp.c_str(), temp_str);

        Serial.printf("Publicando para %s: %s\n", feed_hum.c_str(), hum_str);
        mqttClient.publish(feed_hum.c_str(), hum_str);

        // Opcional: publicar em feed_extra
        // mqttClient.publish(feed_extra.c_str(), "outro dado aqui");

      } else {
         Serial.printf("Dados inválidos para %s (Temp=%.1f, Hum=%.1f). Não publicado.\n", 
                       key_id.c_str(), val_data.temperature, val_data.humidity);
      }
    } else {
      Serial.printf("Dados de %s expirados (timestamp: %lu). Não publicado.\n", key_id.c_str(), val_data.timestamp);
    }
  }
  Serial.println("-------------------------------------------");
}

#endif

// =======================================================
// ===           CALLBACKS ESP-NOW                     ===
// =======================================================

// --- Callback Quando Dados São Recebidos ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  struct_message received_data;
  memcpy(&received_data, incomingData, sizeof(received_data));

  // 1. Ignorar se for pacote próprio (broadcast pode retornar)
  if (strcmp(received_data.device_id, DEVICE_ID) == 0) {
    return;
  }

  // 2. Verificar se já vimos este pacote (ou um mais recente) recentemente
  String sender_id = String(received_data.device_id);
  unsigned long last_seen = 0;
  if (last_seen_timestamp.count(sender_id)) {
      last_seen = last_seen_timestamp[sender_id];
  }

  if (received_data.timestamp > last_seen) {
    Serial.printf("Novo dado recebido de %s (Timestamp: %lu > %lu)\n", 
                  sender_id.c_str(), received_data.timestamp, last_seen);
    
    // Atualiza o último timestamp visto para este remetente
    last_seen_timestamp[sender_id] = received_data.timestamp;

    // 3. Se for o Principal, armazena os dados agregados
    #if IS_PRINCIPAL
      aggregated_data[sender_id] = received_data;
      Serial.printf("  -> Dado de %s armazenado no Principal.\n", sender_id.c_str());
    #endif

    // 4. Retransmitir o pacote para outros nós (evita retransmitir imediatamente)
    //    A verificação de timestamp no início já ajuda a evitar loops infinitos
    //    Adicionamos um delay mínimo para não sobrecarregar a rede
    if (millis() - received_data.timestamp < TEMPO_RETRANSMISSAO_MIN_MS) {
         Serial.printf("  -> Retransmitindo dados de %s...\n", sender_id.c_str());
         sendDataESPNow(received_data); // Retransmite o pacote original
    } else {
         Serial.printf("  -> Pacote de %s muito antigo para retransmitir (delta: %lu ms).\n", 
                       sender_id.c_str(), millis() - received_data.timestamp);
    }

  } else {
     // Serial.printf("Dado antigo/repetido de %s ignorado (Timestamp: %lu <= %lu)\n", 
     //               sender_id.c_str(), received_data.timestamp, last_seen);
  }
}

// --- Callback Quando Dados São Enviados (Opcional) ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Serial.print("Status do último envio ESP-NOW: ");
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sucesso" : "Falha");
}

// =======================================================
// ===           SETUP                                 ===
// =======================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nIniciando ESP32 - ID: " DEVICE_ID);
  Serial.printf("Este no eh Principal? %s\n", IS_PRINCIPAL ? "SIM" : "NAO");

  // Inicializa sensor
  dht.begin();

  // Configura Wi-Fi no modo Estação (necessário para ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Garante que não conecte a nenhuma rede por padrão

  // Inicializa ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }
  Serial.println("ESP-NOW inicializado com sucesso.");

  // Define o papel (receber/enviar/ambos)
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Adiciona o peer de broadcast
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Falha ao adicionar peer de broadcast");
    return;
  }
  Serial.println("Peer de broadcast adicionado.");

  // Configurações específicas do Nó Principal
  #if IS_PRINCIPAL
    Serial.println("Configurando como Nó Principal (Gateway MQTT)...");
    // Conecta ao Wi-Fi
    Serial.printf("Conectando a %s ", WLAN_SSID);
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWi-Fi conectado!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Configura o servidor MQTT
    mqttClient.setServer(AIO_SERVER, AIO_SERVERPORT);
    // Se precisar de um callback para mensagens MQTT recebidas:
    // mqttClient.setCallback(callbackMQTT);
  #endif

  // Lê o sensor local pela primeira vez
  readLocalSensor();
  ultimoTempoLeituraLocal = millis();
}

// =======================================================
// ===           LOOP PRINCIPAL                        ===
// =======================================================
void loop() {
  unsigned long current_time = millis();

  // 1. Ler sensor local periodicamente
  if (current_time - ultimoTempoLeituraLocal >= INTERVALO_LEITURA_LOCAL_MS) {
    readLocalSensor();
    ultimoTempoLeituraLocal = current_time;
  }

  // 2. Se for o Principal, gerenciar MQTT e publicar dados agregados
  #if IS_PRINCIPAL
    // Garante que o cliente MQTT esteja conectado
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    // Mantém a conexão MQTT ativa
    mqttClient.loop(); 

    // Publica dados agregados periodicamente
    if (current_time - ultimoTempoEnvioMQTT >= INTERVALO_ENVIO_MQTT_MS) {
      publishAggregatedData();
      ultimoTempoEnvioMQTT = current_time;
    }
  #endif

  // Pequeno delay para não sobrecarregar
  delay(10);
}

// --- Callback MQTT (se necessário) ---
/*
#if IS_PRINCIPAL
void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensagem recebida [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  // Adicione aqui a lógica para tratar comandos recebidos via MQTT
}
#endif
*/

