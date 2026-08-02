# Mini monitor metereológico via internet
### Pré-Requisitos

1. ESP32-CYD (Placa Desenvolvimento Esp32 Display Touch 2,8 Pol)
2. Cabo USB mini
3. Fios com conector femea de um lado e grove femea do outro ou fio adaptado
4. Fonte de 5V
### Links de Referência

Placa Desenvolvimento Esp32 Display Touch 2,8 Pol
https://produto.mercadolivre.com.br/MLB-4616857862-placa-desenvolvimento-esp32-display-touch-28-pol-case-_JM?quantity=1&variation_id=182523721415&sid=purchases
### Passo 1: Preparar a Arduino IDE

1. **Baixe e instale** a versão mais recente da [Arduino IDE](https://www.arduino.cc/en/software) se ainda não tiver.
2. **Adicione o suporte ao ESP32**:
3. Abra a IDE e vá em **File > Preferences** (Arquivo > Preferências).
4. No campo _Additional Boards Manager URLs_, cole o link oficial da Espressif: 
	https://espressif.github.io/arduino-esp32/package_esp32_index.json
5.  Clique em **OK**.
 6. Vá no menu lateral em **Boards Manager** (Gerenciador de Placas) ou pressione `Ctrl + Shift + B`.
7. Digite **ESP32** e instale o pacote da _Espressif Systems_.
8. Em Tools > Boards > Esp32 selecione ESP32 Dev Module
9. Em Tools> Ports localize a porta USB onde foi conectada o dispositivo
10. Envie um codigo vazio para teste de comunicação
11. Se a comunicação ocorreu corretamente ao gravar o codigo vazio vamos para o proximo passo.

## Passo 2: Instalar as Bibliotecas

Para controlar o display TFT e o Touch, você precisará de duas bibliotecas principais:

1. Vá em **Sketch > Include Library > Manage Libraries...** (ou `Ctrl + Shift + I`) e instale:
2. **TFT_eSPI** (por Bodmer) -> Essa é a biblioteca que desenha os gráficos na tela com alta performance.
3. **ArduinoJson** (por Benoit Blanchon) -> Permite serializar dados para o formato json.
4. **XPT2046_Touchscreen** (por Paul Stoffregen) -> Essa controla o toque na tela. (Apenas para ter instalado)    

## Passo 3: O "Pulo do Gato" (Configurar a TFT_eSPI)

A biblioteca `TFT_eSPI` é genérica e serve para dezenas de telas diferentes. Para que ela saiba exatamente quais pinos a sua CYD usa, precisamos editar um arquivo de configuração interno dela.

1. No seu computador, abra a pasta **Documentos > Arduino > libraries > TFT_eSPI**.
2. Vamos criar um arquivo de texto na pasta /User_Setups chamado "Setup252_ESP32_2432S028.h" (poderia ser qualquer nome apenas para não conflitar), adicione o seguinte conteúdo a este arquivo e salve-o:

	```
	#define USER_SETUP_INFO "ESP32_CYD_2432S028"
	
	// Define o driver correto da tela da CYD
	#define ILI9341_2_DRIVER
	
	// Mapeamento real dos pinos do display na placa amarela
	#define TFT_MISO 12
	#define TFT_MOSI 13
	#define TFT_SCLK 14
	#define TFT_CS   15
	#define TFT_DC    2
	#define TFT_RST  -1
	#define TFT_BL   21
	#define TFT_BACKLIGHT_ON HIGH
	
	// Fontes que serão carregadas na memória
	#define LOAD_GLCD
	#define LOAD_FONT2
	#define LOAD_FONT4
	#define LOAD_FONT6
	#define LOAD_FONT7
	#define LOAD_FONT8
	#define LOAD_GFXFF
	
	// Velocidades barramento SPI otimizadas para o ESP32 dessa placa
	#define SPI_FREQUENCY        55000000
	#define SPI_READ_FREQUENCY   20000000
	#define SPI_TOUCH_FREQUENCY  25000000
	```


3. Na seqeucncia Vamos editar o arquivo "libraries/TFT_eSPI/User_Setup_Select.h"
4. Localiza a linha a seguir e comentea com //:

```
//#include <User_Setup.h>	
```

5. Logo abaixo cole a linha a seguir

```
 #include <User_Setups/Setup251_ESP32_Page_ILI9341.h>
```

6. Salve o arquivo User_Setup_Select.h
7. Instale a biblioteca Adafruit_MLX90614 (Adafruit_MLX90614 Library by Adafruit)

## Passo 4: O Código
ESP32-Weather-Station/1.0
1. Cole o código a seguir no Arduino IDE
```C++
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <FS.h>
#include <WebServer.h>
#include <Preferences.h>

// --- CONFIGURAÇÕES ---
const char* ssid     = "Rede Wifi";
const char* password = "Senha Wifi";
const char* hostname = "estacao-meteo";

IPAddress local_IP(192, 168, 15, 150);
IPAddress gateway(192, 168, 15, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);

#define TFT_BL 21
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

// Variáveis Globais
float latitude = 0.0;
float longitude = 0.0;
String cidade = "Buscando...";
float temperatura = 0.0;
float chuvaMm = 0.0;
int probChuva = 0;
int codigoTempo = 0;
bool dadosCarregados = false;
volatile bool precisaAtualizar = false;

unsigned long ultimoFetch = 0;
const unsigned long tempoEspera = 600000;

// --- HTML COM CONTROLE MANUAL E AUTOMÁTICO ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Mapa ESP32</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
  <style>
    body { font-family: sans-serif; margin: 0; padding: 0; overflow: hidden; }
    #map { height: 90vh; width: 100vw; }
    .controls { padding: 10px; height: 10vh; display: flex; align-items: center; gap: 10px; background: #eee; }
  </style>
</head>
<body>
  <div class="controls">
    <input type="text" id="nomeCidade" placeholder="Nome da cidade">
    <button onclick="salvar()">Salvar Local</button>
    <span id="status"></span>
  </div>
  <div id="map"></div>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <script>
    var map = L.map('map').setView([-23.55, -46.63], 13);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png').addTo(map);
    var marker, lat, lng;
    map.on('click', async function(e) {
        lat = e.latlng.lat; lng = e.latlng.lng;
        if(marker) map.removeLayer(marker);
        marker = L.marker([lat, lng]).addTo(map);
        
        try {
            let res = await fetch(`https://nominatim.openstreetmap.org/reverse?format=json&lat=${lat}&lon=${lng}`);
            let data = await res.json();
            document.getElementById('nomeCidade').value = data.address.city || data.address.town || data.address.village || "Local";
        } catch(err) { document.getElementById('nomeCidade').value = "Local"; }
    });
    function salvar(){
        let nome = document.getElementById('nomeCidade').value || "Local";
        document.getElementById('status').innerText = "Salvando...";
        fetch('/set-location?lat='+lat+'&lon='+lng+'&cidade='+encodeURIComponent(nome))
        .then(r => document.getElementById('status').innerText = "Sucesso!")
        .catch(e => document.getElementById('status').innerText = "Erro!");
    }
  </script>
</body>
</html>
)rawliteral";

// --- FUNÇÕES DE DESENHO ---
void desenharSol(int x, int y) {
  tft.fillCircle(x, y, 14, TFT_YELLOW);
  for (int i = 0; i < 360; i += 45) {
    float rad = i * 0.0174533;
    tft.drawLine(x + cos(rad) * 18, y + sin(rad) * 18, x + cos(rad) * 24, y + sin(rad) * 24, TFT_YELLOW);
  }
}

void desenharNuvem(int x, int y, uint16_t cor = TFT_WHITE) {
  tft.fillCircle(x - 12, y + 4, 10, cor);
  tft.fillCircle(x + 12, y + 4, 10, cor);
  tft.fillCircle(x, y - 4, 14, cor);
  tft.fillRect(x - 12, y, 25, 14, cor);
}

void desenharChuva(int x, int y) {
  desenharNuvem(x, y - 5, TFT_SILVER);
  tft.fillCircle(x, y + 15, 4, TFT_CYAN);
}

void desenharTempestade(int x, int y) {
  desenharNuvem(x, y - 5, TFT_DARKGREY);
  tft.drawLine(x - 2, y + 12, x - 6, y + 20, TFT_YELLOW);
  tft.drawLine(x - 6, y + 20, x + 2, y + 20, TFT_YELLOW);
}

void desenharIconeClima(int x, int y, int code) {
  if (code == 0) {
    desenharSol(x, y);
  } else if (code >= 1 && code <= 3) {
    desenharSol(x - 8, y - 6);
    desenharNuvem(x + 6, y + 4, TFT_WHITE);
  } else if (code >= 51 && code <= 82) {
    desenharChuva(x, y);
  } else if (code >= 95) {
    desenharTempestade(x, y);
  } else {
    desenharNuvem(x, y, TFT_WHITE);
  }
}

void desenharInterface() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(cidade, 15, 10, 4);
  tft.drawFastHLine(10, 42, 300, TFT_BLUE);

  if (!dadosCarregados) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Aguardando...", 15, 100, 4);
  } else {
    desenharIconeClima(265, 80, codigoTempo);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Temp", 15, 55, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawFloat(temperatura, 1, 15, 75, 7);
    tft.drawString("C", 165, 75, 4);
    tft.drawFastHLine(10, 140, 300, TFT_DARKGREY);

    desenharChuva(35, 175);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Prob. Chuva:", 70, 160, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawNumber(probChuva, 70, 178, 4);
    tft.drawString("%", 125, 178, 4);

    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString("Volume:", 180, 160, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawFloat(chuvaMm, 1, 180, 178, 4);
    tft.drawString("mm", 245, 178, 4);
  }
}

// --- LÓGICA ---
void buscarDadosMeteorologicos() {
  if (latitude == 0.0) return;

  HTTPClient http;
  http.begin("http://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) +
             "&longitude=" + String(longitude, 4) +
             "&current=temperature_2m,relative_humidity_2m,rain,weather_code" +
             "&daily=precipitation_probability_max&timezone=auto&forecast_days=1");

  http.setUserAgent("NomedoSeuAgente/1.0");

  if (http.GET() == HTTP_CODE_OK) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    temperatura = doc["current"]["temperature_2m"];
    chuvaMm = doc["current"]["rain"];
    codigoTempo = doc["current"]["weather_code"];
    probChuva = doc["daily"]["precipitation_probability_max"][0];
    dadosCarregados = true;
  }

  http.end();
  desenharInterface();
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  WiFi.config(local_IP, gateway, subnet, primaryDNS);
  WiFi.setHostname(hostname);

  prefs.begin("config", true);
  latitude = prefs.getFloat("lat", 0.0);
  longitude = prefs.getFloat("lon", 0.0);
  cidade = prefs.getString("cidade", "Definir...");
  prefs.end();

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/set-location", []() {
    latitude = server.arg("lat").toFloat();
    longitude = server.arg("lon").toFloat();
    cidade = server.arg("cidade");

    prefs.begin("config", false);
    prefs.putFloat("lat", latitude);
    prefs.putFloat("lon", longitude);
    prefs.putString("cidade", cidade);
    prefs.end();

    precisaAtualizar = true;
    server.send(200, "text/plain", "OK");
  });

  server.begin();

  if (latitude != 0.0) {
    buscarDadosMeteorologicos();
  } else {
    desenharInterface();
  }
}

void loop() {
  server.handleClient();

  if (precisaAtualizar) {
    buscarDadosMeteorologicos();
    precisaAtualizar = false;
  }

  if (millis() - ultimoFetch >= tempoEspera || ultimoFetch == 0) {
    ultimoFetch = millis();
    buscarDadosMeteorologicos();
  }
}
```

2. Altere o usuário e senha da rede
3. Clique em Enviar
