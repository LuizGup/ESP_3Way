# Sistema ESP32 em Cascata (ESP-NOW + MQTT)

Este projeto demonstra a implementação de um sistema de monitoramento ambiental utilizando três ESP32 em uma arquitetura de cascata. A comunicação local entre os dispositivos é feita via ESP-NOW, e os dados consolidados são enviados para a plataforma Adafruit IO através do protocolo MQTT.

## 1. Visão Geral da Arquitetura

O sistema opera com o seguinte fluxo de dados:

1.  **ESP1 (Coletor Inicial):** Lê dados do seu sensor (DHT11) e envia a informação via ESP-NOW para o ESP2.
2.  **ESP2 (Coletor/Retransmissor):** Recebe os dados do ESP1, lê dados do seu próprio sensor, agrega ambas as informações e retransmite a estrutura combinada via ESP-NOW para o ESP3.
3.  **ESP3 (Coletor Final/Gateway MQTT):** Recebe a estrutura agregada do ESP2, lê dados do seu próprio sensor, adiciona essa informação à estrutura. Em seguida, conecta-se à rede Wi-Fi e publica os dados válidos de todos os três ESPs nos feeds correspondentes da plataforma Adafruit IO via MQTT.

```mermaid
graph TD
  A[ESP 1<br>Sensor DHT11<br>Coleta e Envia] -->|ESP-NOW (Dados ESP1)| B(ESP 2<br>Sensor DHT11<br>Recebe, Coleta, Agrega e Envia);
  B -->|ESP-NOW (Dados ESP1 + ESP2)| C(ESP 3<br>Sensor DHT11<br>Recebe, Coleta, Agrega e Publica);
  C -->|MQTT| D[Adafruit IO<br>Dashboard];
```

## 2. Estrutura de Dados Comum (`struct_message`)

Uma única estrutura é usada para transportar os dados através da cascata, definida de forma idêntica nos três arquivos `.ino`:

```c++
typedef struct struct_message {
  // Dados do ESP1
  float temp1;
  float hum1;
  bool data1_valid; // Indica se os dados do ESP1 são válidos

  // Dados do ESP2
  float temp2;
  float hum2;
  bool data2_valid; // Indica se os dados do ESP2 são válidos

  // Dados do ESP3
  float temp3;
  float hum3;
  bool data3_valid; // Indica se os dados do ESP3 são válidos
} struct_message;
```

- Cada ESP é responsável por preencher sua respectiva seção e marcar seu `dataX_valid` como `true` se a leitura do sensor for bem-sucedida.
- Valores `NaN` (Not a Number) são usados para temperatura/umidade quando a leitura falha ou os dados ainda não foram preenchidos.

## 3. Funcionamento Detalhado dos Códigos

### 3.1. `esp32_1_coletor_emissor.ino`

-   **Função:** Coletar dados do seu sensor DHT11 e enviar a estrutura `struct_message` (preenchida apenas com seus dados) para o ESP2 via ESP-NOW.
-   **Configuração Chave:** Definir o endereço MAC correto do ESP2 em `esp2_mac_address[]`.
-   **Setup:**
    -   Inicializa Serial e Wi-Fi (modo STA).
    -   Imprime o próprio MAC Address (útil para configurar o ESP2, se necessário).
    -   Inicializa ESP-NOW.
    -   Registra o callback `OnDataSent` (informa status do envio).
    -   Adiciona o ESP2 como *peer* (destino do envio).
    -   Inicializa o sensor DHT11 local.
-   **Loop:**
    -   Lê temperatura e umidade locais.
    -   Preenche a seção `temp1`, `hum1`, `data1_valid` da estrutura `cascade_data`. Invalida os campos dos outros ESPs.
    -   Envia a estrutura `cascade_data` para o MAC do ESP2 usando `esp_now_send()`.
    -   Aguarda 15 segundos.

### 3.2. `esp32_2_coletor_retransmissor.ino`

-   **Função:** Receber a estrutura do ESP1, ler seu próprio sensor, adicionar seus dados à estrutura recebida e retransmitir a estrutura atualizada para o ESP3 via ESP-NOW.
-   **Configuração Chave:** Definir o endereço MAC correto do ESP3 em `esp3_mac_address[]`. O MAC do ESP1 (`esp1_mac_address[]`) é opcional, mais para referência.
-   **Setup:**
    -   Inicializa Serial e Wi-Fi (modo STA).
    -   Imprime o próprio MAC Address (necessário para configurar o ESP1).
    -   Inicializa ESP-NOW.
    -   Registra os callbacks `OnDataRecv` (para receber do ESP1) e `OnDataSent` (para status do envio ao ESP3).
    -   Adiciona o ESP3 como *peer*.
    -   Inicializa o sensor DHT11 local.
-   **Callback `OnDataRecv` (Principal Lógica):**
    -   Chamado quando dados chegam do ESP1.
    -   Copia os dados recebidos para a estrutura `cascade_data` local.
    -   Imprime os dados recebidos do ESP1.
    -   Lê o sensor local (ESP2).
    -   Preenche a seção `temp2`, `hum2`, `data2_valid` da estrutura.
    -   Invalida os campos do ESP3.
    -   Envia a estrutura `cascade_data` atualizada para o MAC do ESP3 usando `esp_now_send()`.
-   **Loop:** Praticamente vazio, pois a lógica principal está no callback de recebimento.

### 3.3. `esp32_3_coletor_gateway.ino`

-   **Função:** Receber a estrutura do ESP2, ler seu próprio sensor, adicionar seus dados, conectar ao Wi-Fi e publicar todos os dados válidos (ESP1, ESP2, ESP3) no Adafruit IO via MQTT.
-   **Configuração Chave:**
    -   Definir as credenciais da sua rede Wi-Fi (`WLAN_SSID`, `WLAN_PASS`).
    -   Definir suas credenciais do Adafruit IO (`AIO_USERNAME`, `AIO_KEY`).
    -   Garantir que os nomes dos feeds MQTT (`tempFeed1`, `humFeed1`, etc.) correspondam exatamente aos feeds criados na sua conta Adafruit IO.
-   **Setup:**
    -   Inicializa Serial.
    -   Conecta ao Wi-Fi.
    -   Imprime o próprio MAC Address (necessário para configurar o ESP2).
    -   Inicializa ESP-NOW.
    -   Registra o callback `OnDataRecv` (para receber do ESP2).
    -   Inicializa o sensor DHT11 local.
    -   Configura os objetos `Adafruit_MQTT_Publish` para cada feed.
-   **Callback `OnDataRecv` (Principal Lógica):**
    -   Chamado quando dados chegam do ESP2.
    -   Copia os dados recebidos para a estrutura `final_data` local.
    -   Imprime os dados recebidos (ESP1 e ESP2).
    -   Lê o sensor local (ESP3).
    -   Preenche a seção `temp3`, `hum3`, `data3_valid` da estrutura.
    -   Chama `MQTT_connect()` para garantir a conexão.
    -   Se conectado ao MQTT, publica os dados de cada ESP nos feeds correspondentes, **somente se o respectivo `dataX_valid` for `true`**.
-   **Loop:**
    -   Gerencia a conexão MQTT (verifica, reconecta se necessário, envia pings) usando a função `MQTT_connect()` e `mqtt.ping()`.
    -   A lógica de recebimento e publicação ocorre no callback.

## 4. Guia de Implementação Passo a Passo

1.  **Hardware:**
    *   Conecte um sensor DHT11 (ou outro compatível, ajustando o código) a cada um dos três ESP32. Por padrão, o código usa o pino `GPIO 4`. Se usar pinos diferentes, ajuste as definições `#define DHTPIN_ESPx` em cada arquivo.
    *   Alimente os três ESP32 (via USB, por exemplo).

2.  **Obter Endereços MAC:**
    *   Compile e grave temporariamente cada um dos três códigos (`.ino`) em seus respectivos ESP32 (mesmo com os MACs errados inicialmente).
    *   Abra o Serial Monitor (baud rate 115200) para cada ESP32.
    *   Anote cuidadosamente o endereço MAC que cada ESP imprime ao iniciar (ex: `MAC Address ESP1: AA:BB:CC:11:22:33`).

3.  **Configurar Códigos:**
    *   **`esp32_1_coletor_emissor.ino`:** Edite a linha `uint8_t esp2_mac_address[] = {...};` e insira o MAC anotado do **ESP32 Nº 2**, no formato `0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33`.
    *   **`esp32_2_coletor_retransmissor.ino`:** Edite a linha `uint8_t esp3_mac_address[] = {...};` e insira o MAC anotado do **ESP32 Nº 3**.
    *   **`esp32_3_coletor_gateway.ino`:**
        *   Edite as linhas `#define WLAN_SSID` e `#define WLAN_PASS` com os dados da sua rede Wi-Fi.
        *   Edite as linhas `#define AIO_USERNAME` e `#define AIO_KEY` com suas credenciais do Adafruit IO.

4.  **Configurar Adafruit IO:**
    *   Acesse sua conta Adafruit IO.
    *   Crie os seguintes **Feeds** (os nomes devem ser exatos):
        *   `temperatura_esp1`
        *   `umidade_esp1`
        *   `temperatura_esp2`
        *   `umidade_esp2`
        *   `temperatura_esp3`
        *   `umidade_esp3`
    *   Crie um **Dashboard** e adicione elementos (gráficos, gauges, etc.) vinculados a esses feeds para visualizar os dados.

5.  **Compilar e Gravar (Final):**
    *   Certifique-se de ter as bibliotecas necessárias instaladas na Arduino IDE (`Adafruit MQTT Library`, `DHT sensor library` by Adafruit, `Adafruit Unified Sensor`).
    *   Compile e grave cada código `.ino` configurado no seu respectivo ESP32.

6.  **Testar e Depurar:**
    *   Abra o Serial Monitor para os três ESP32 simultaneamente (se possível, usando múltiplas instâncias da IDE ou um terminal serial).
    *   **ESP1:** Deve ler o sensor e imprimir mensagens de envio para o ESP2.
    *   **ESP2:** Deve imprimir mensagens ao receber dados do ESP1, ler seu sensor e imprimir mensagens de envio para o ESP3.
    *   **ESP3:** Deve imprimir mensagens ao receber dados do ESP2, ler seu sensor, conectar ao Wi-Fi/MQTT e imprimir mensagens de publicação no Adafruit IO.
    *   Verifique seu Dashboard no Adafruit IO para confirmar se os dados estão chegando corretamente.

## 5. Arquivos do Projeto

-   `esp32_1_coletor_emissor.ino`: Código para o ESP32 nº 1.
-   `esp32_2_coletor_retransmissor.ino`: Código para o ESP32 nº 2.
-   `esp32_3_coletor_gateway.ino`: Código para o ESP32 nº 3.
-   `README.md`: Este arquivo.

