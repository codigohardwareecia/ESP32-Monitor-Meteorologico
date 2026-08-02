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

  http.setUserAgent("NomeDoSeuAgente/1.0");

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