// =====================================================
// GUIDING HAND - PROTOTIPO NO WOKWI
// Oculos inteligente para navegacao de pessoas cegas
// ESP32 + HC-SR04 + vibracao (LEDs) + voz (OLED e buzzer)
// Project-Based Maker Lab - Global Solution CitizenTech
// =====================================================
//
// O que cada componente representa no Wokwi:
//   HC-SR04 .............. sensor de distancia na frente dos oculos
//   LED esquerdo/direito . motores de vibracao das hastes (brilho = intensidade PWM)
//   OLED SSD1306 ......... frase que o alto-falante falaria + estado do sistema
//   Buzzer ............... marca cada fala e faz o alerta continuo de PERIGO
//   Botao ................ toque: repete a instrucao | 3 s: pede ajuda
//   Potenciometro ........ nivel da bateria
//   Monitor Serial ....... comandos que viriam do servidor de visao e de rotas
//
// Protocolo de comandos (o mesmo que o servidor envia pelo Wi-Fi):
//   E vire a esquerda  D vire a direita  F siga em frente  H voce chegou
//   A obstaculo na altura da cabeca  C carro na calcada  B buraco ou degrau
//   T faixa de pedestre  P so pessoas a frente  S nada relevante
//   Somente no Monitor Serial: W liga/desliga o Wi-Fi (teste)  ? ajuda
//
// Regra principal: o alerta de proximidade e decidido aqui, no ESP32,
// e NUNCA depende da internet. A camera e o servidor so acrescentam
// o tipo do obstaculo e as instrucoes de rota.
// =====================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// ---------------- Pinos ----------------
#define TRIG_PIN     5    // HC-SR04 TRIG (mesmo pino do Smart Parking)
#define ECHO_PIN     18   // HC-SR04 ECHO (no hardware real: divisor de tensao 5 V -> 3,3 V)
#define VIB_ESQ_PIN  25   // vibracao da haste esquerda
#define VIB_DIR_PIN  27   // vibracao da haste direita
#define BUZZER_PIN   14   // marca as falas e alerta de PERIGO
#define BOTAO_PIN    13   // botao da haste (INPUT_PULLUP)
#define BATERIA_PIN  34   // potenciometro simulando a bateria (pino somente de entrada)

// ---------------- Display OLED ----------------
#define LARGURA_OLED 128
#define ALTURA_OLED  64
Adafruit_SSD1306 display(LARGURA_OLED, ALTURA_OLED, &Wire, -1);
bool displayOk = false;

// ---------------- Faixas de alerta (cm) ----------------
// Ajustadas para a caminhada: cerca de 1 m por segundo,
// entao 2 m dao quase dois segundos para reagir.
const float LIMITE_LIVRE   = 200;   // acima disso: LIVRE
const float LIMITE_ATENCAO = 120;   // 120 a 200 cm: ATENCAO
const float LIMITE_CUIDADO = 60;    // 60 a 119 cm: CUIDADO | abaixo de 60 cm: PERIGO

// Intensidade da vibracao (PWM de 0 a 255), como a velocidade do motor nas aulas.
// No hardware real, limitar o valor maximo para respeitar os 3 V dos motores.
const int PWM_ATENCAO = 90;
const int PWM_CUIDADO = 170;
const int PWM_PERIGO  = 255;
const int PWM_DIRECAO = 255;

// ---------------- Tempos (ms) ----------------
const unsigned long INTERVALO_LEITURA   = 60;     // HC-SR04: pelo menos 60 ms entre medicoes
const unsigned long INTERVALO_DISPLAY   = 200;
const unsigned long INTERVALO_BATERIA   = 2000;
const unsigned long INTERVALO_CONSULTA  = 1000;   // consulta ao servidor
const unsigned long ESPERA_APOS_FALHAS  = 10000;  // depois de 3 falhas seguidas
const unsigned long INTERVALO_RECONEXAO = 30000;
const unsigned long DURACAO_FRASE       = 3000;   // tempo da frase no OLED
const unsigned long VALIDADE_CAMERA     = 3000;   // classificacao da camera vale 3 s
const unsigned long VALIDADE_PESSOAS    = 2000;   // "so pessoas" vale 2 s
const unsigned long ANTI_REPETICAO      = 5000;   // nao repetir o mesmo obstaculo em 5 s
const unsigned long TEMPO_AJUDA         = 3000;   // botao segurado por 3 s = pedido de ajuda
const unsigned long DURACAO_PULSO       = 200;    // pulsos de direcao

const int BATERIA_FRACA = 15;  // %

// ---------------- Estados do sistema ----------------
enum Estado { LIVRE, ATENCAO, CUIDADO, PERIGO };
const char* NOMES_ESTADO[] = {"LIVRE", "ATENCAO", "CUIDADO", "PERIGO"};

Estado estado = LIVRE;
float distanciaAtual = -1;
float leituras[3] = {-1, -1, -1};   // ultimas 3 leituras, para filtrar ruido
int indiceLeitura = 0;

// ---------------- Pulsos de direcao ----------------
struct Pulsos {
  int restantes;
  bool ligado;
  unsigned long proximaTroca;
};
Pulsos pulsosEsq = {0, false, 0};
Pulsos pulsosDir = {0, false, 0};
int saidaEsqAnterior = -1;
int saidaDirAnterior = -1;

// ---------------- Voz e camera ----------------
String fraseAtual = "";
unsigned long fraseVisivelAte = 0;
String ultimaInstrucao = "";
char ultimoObstaculo = 0;
unsigned long ultimoObstaculoEm = 0;
char classificacaoPendente = 0;
unsigned long classificacaoValidaAte = 0;
unsigned long soPessoasAte = 0;

// ---------------- Rede ----------------
bool wifiHabilitado = true;
bool wifiConectadoAntes = false;
unsigned long proximaReconexao = 0;
unsigned long proximaConsulta = 0;
int falhasSeguidas = 0;
unsigned long ultimoEventoProximidade = 0;
Estado estadoUltimoEvento = LIVRE;

// ---------------- Botao e bateria ----------------
bool botaoEstavel = HIGH;
bool botaoUltimaLeitura = HIGH;
unsigned long botaoMudouEm = 0;
unsigned long botaoPressionadoEm = 0;
bool ajudaEnviada = false;
int bateria = 100;
unsigned long ultimoAvisoBateria = 0;

// ---------------- Temporizadores ----------------
unsigned long ultimaLeitura = 0;
unsigned long ultimoDisplay = 0;
unsigned long ultimaBateria = 0;

// ---------------- Declaracoes das funcoes ----------------
// Declaradas aqui para que a ordem das funcoes no arquivo nao importe.
bool aindaValido(unsigned long ate);
bool servidorConfigurado();
void imprimirAjuda();
float medirDistancia();
float mediana3(float a, float b, float c);
Estado calcularEstado(float d);
void atualizarDistancia();
void mudarEstado(Estado novo);
int intensidadeDaZona(unsigned long agora);
void iniciarPulsos(Pulsos &p, int quantidade);
int atualizarPulsos(Pulsos &p, unsigned long agora);
void atualizarVibracao(unsigned long agora);
void falar(const String &texto, bool guardarComoInstrucao);
const char* fraseDoObstaculo(char c);
void falarObstaculo(char c);
void receberClassificacao(char c);
void processarComando(char c);
void lerSerial();
void repetirInstrucao();
void pedirAjuda();
void verificarBotao(unsigned long agora);
void verificarBateria(unsigned long agora);
void conectarWiFi();
void alternarWiFi();
void verificarRede(unsigned long agora);
void consultarServidor(unsigned long agora);
void enviarEvento(const char* tipo, const String &detalhe);
void imprimirComQuebra(const String &texto, int y);
void atualizarDisplay();

// =====================================================
// FUNCOES AUXILIARES
// =====================================================

// Verdadeiro enquanto o instante "ate" ainda nao chegou (seguro contra estouro do millis)
bool aindaValido(unsigned long ate) {
  return (long)(ate - millis()) > 0;
}

bool servidorConfigurado() {
  return strlen(SERVIDOR_URL) > 0;
}

void imprimirAjuda() {
  Serial.println("Comandos (simulam o servidor de visao e de rotas):");
  Serial.println("  E esquerda | D direita | F frente | H chegou");
  Serial.println("  A obstaculo na cabeca | C carro na calcada | B piso");
  Serial.println("  T faixa de pedestre | P so pessoas | S nada relevante");
  Serial.println("  W liga/desliga o Wi-Fi (teste) | ? ajuda");
  Serial.println("Arraste o controle do HC-SR04 para mudar a distancia.");
  Serial.println();
}

// =====================================================
// SENSOR DE DISTANCIA E ESTADOS
// =====================================================

float medirDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000UL);  // espera no maximo 30 ms (~5 m)
  if (duracao == 0) return -1;                      // sem eco: nada no alcance

  return duracao * 0.0343 / 2;                      // mesma conta do Smart Parking
}

float mediana3(float a, float b, float c) {
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
  return c;
}

Estado calcularEstado(float d) {
  if (d < 0 || d > LIMITE_LIVRE) return LIVRE;
  if (d >= LIMITE_ATENCAO) return ATENCAO;
  if (d >= LIMITE_CUIDADO) return CUIDADO;
  return PERIGO;
}

void atualizarDistancia() {
  leituras[indiceLeitura] = medirDistancia();
  indiceLeitura = (indiceLeitura + 1) % 3;
  distanciaAtual = mediana3(leituras[0], leituras[1], leituras[2]);

  Estado novo = calcularEstado(distanciaAtual);
  if (novo != estado) {
    mudarEstado(novo);
  }
}

void mudarEstado(Estado novo) {
  Estado anterior = estado;
  estado = novo;

  Serial.print("[SENSOR] ");
  Serial.print(NOMES_ESTADO[anterior]);
  Serial.print(" -> ");
  Serial.print(NOMES_ESTADO[novo]);
  Serial.print(" (");
  Serial.print(distanciaAtual, 0);
  Serial.println(" cm)");

  // A camera viu algo antes do sensor? Agora que o sensor confirmou, fala o tipo.
  if (anterior == LIVRE && novo != LIVRE && classificacaoPendente != 0 && aindaValido(classificacaoValidaAte)) {
    falarObstaculo(classificacaoPendente);
    classificacaoPendente = 0;
  }

  // PERIGO: buzzer continuo (como no Smart Parking) e a fala "PARE"
  if (novo == PERIGO) {
    tone(BUZZER_PIN, 1000);
    falar("PARE", false);
  } else if (anterior == PERIGO) {
    noTone(BUZZER_PIN);
  }

  // Evento para o mapa de barreiras (sem dados pessoais).
  // Envia sempre que a situacao piora; repeticoes do mesmo nivel, no maximo a cada 2 s.
  if (novo >= CUIDADO && (novo > estadoUltimoEvento || millis() - ultimoEventoProximidade > 2000)) {
    ultimoEventoProximidade = millis();
    estadoUltimoEvento = novo;
    enviarEvento("proximidade", NOMES_ESTADO[novo]);
  }
}

// =====================================================
// VIBRACAO DAS HASTES
// =====================================================

int intensidadeDaZona(unsigned long agora) {
  switch (estado) {
    case ATENCAO:
      if (aindaValido(soPessoasAte)) return 0;          // so pessoas: silencia o aviso de longe
      return (agora % 800) < 150 ? PWM_ATENCAO : 0;      // pulsos lentos
    case CUIDADO:
      return (agora % 300) < 150 ? PWM_CUIDADO : 0;      // pulsos rapidos
    case PERIGO:
      return PWM_PERIGO;                                 // continua
    default:
      return 0;                                          // LIVRE
  }
}

void iniciarPulsos(Pulsos &p, int quantidade) {
  p.restantes = quantidade;
  p.ligado = true;
  p.proximaTroca = millis() + DURACAO_PULSO;
}

// Retorna -1 quando nao ha pulsos de direcao; senao, a intensidade do momento
int atualizarPulsos(Pulsos &p, unsigned long agora) {
  if (p.restantes <= 0) return -1;

  if ((long)(agora - p.proximaTroca) >= 0) {
    if (p.ligado) {
      p.ligado = false;
      p.restantes--;
    } else {
      p.ligado = true;
    }
    p.proximaTroca = agora + DURACAO_PULSO;
  }

  if (p.restantes <= 0) return -1;
  return p.ligado ? PWM_DIRECAO : 0;
}

void atualizarVibracao(unsigned long agora) {
  int zona = intensidadeDaZona(agora);
  int esq = atualizarPulsos(pulsosEsq, agora);
  int dir = atualizarPulsos(pulsosDir, agora);

  if (esq < 0) esq = zona;
  if (dir < 0) dir = zona;

  // PERIGO tem prioridade sobre qualquer instrucao de rota
  if (estado == PERIGO) {
    esq = PWM_PERIGO;
    dir = PWM_PERIGO;
  }

  if (esq != saidaEsqAnterior) {
    analogWrite(VIB_ESQ_PIN, esq);
    saidaEsqAnterior = esq;
  }
  if (dir != saidaDirAnterior) {
    analogWrite(VIB_DIR_PIN, dir);
    saidaDirAnterior = dir;
  }
}

// =====================================================
// VOZ (OLED + BUZZER NO WOKWI; ALTO-FALANTE NO DISPOSITIVO REAL)
// =====================================================

void falar(const String &texto, bool guardarComoInstrucao) {
  fraseAtual = texto;
  fraseVisivelAte = millis() + DURACAO_FRASE;
  if (guardarComoInstrucao) {
    ultimaInstrucao = texto;
  }

  Serial.print("[VOZ] ");
  Serial.println(texto);

  if (estado != PERIGO) {
    tone(BUZZER_PIN, 1800, 80);  // bip curto marca a fala na simulacao
  }
}

const char* fraseDoObstaculo(char c) {
  switch (c) {
    case 'A': return "OBSTACULO NA ALTURA DA CABECA";
    case 'C': return "CARRO NA CALCADA";
    case 'B': return "ATENCAO AO PISO";
    default:  return "";
  }
}

void falarObstaculo(char c) {
  unsigned long agora = millis();

  // Evita repetir o mesmo aviso e cansar o usuario
  if (c == ultimoObstaculo && agora - ultimoObstaculoEm < ANTI_REPETICAO) return;

  ultimoObstaculo = c;
  ultimoObstaculoEm = agora;
  falar(fraseDoObstaculo(c), true);
  enviarEvento("obstaculo_confirmado", String(c));
}

// Classificacao da camera: so vira fala quando o sensor confirma a aproximacao
void receberClassificacao(char c) {
  if (estado != LIVRE) {
    falarObstaculo(c);
  } else {
    classificacaoPendente = c;
    classificacaoValidaAte = millis() + VALIDADE_CAMERA;
    Serial.print("[CAMERA] ");
    Serial.print(fraseDoObstaculo(c));
    Serial.println(" -> aguardando o sensor confirmar a aproximacao");
  }
}

// =====================================================
// COMANDOS (SERVIDOR OU MONITOR SERIAL)
// =====================================================

void processarComando(char c) {
  c = toupper(c);

  switch (c) {
    case 'E':
      iniciarPulsos(pulsosEsq, 2);
      falar("VIRE A ESQUERDA", true);
      break;
    case 'D':
      iniciarPulsos(pulsosDir, 2);
      falar("VIRE A DIREITA", true);
      break;
    case 'F':
      iniciarPulsos(pulsosEsq, 1);
      iniciarPulsos(pulsosDir, 1);
      falar("SIGA EM FRENTE", true);
      break;
    case 'H':
      iniciarPulsos(pulsosEsq, 3);
      iniciarPulsos(pulsosDir, 3);
      falar("VOCE CHEGOU", true);
      break;
    case 'T':
      falar("FAIXA DE PEDESTRE A FRENTE", true);
      break;
    case 'A':
    case 'C':
    case 'B':
      receberClassificacao(c);
      break;
    case 'P':
      soPessoasAte = millis() + VALIDADE_PESSOAS;
      Serial.println("[CAMERA] Apenas pessoas a frente: aviso de ATENCAO silenciado por 2 s");
      break;
    case 'S':
      soPessoasAte = 0;
      classificacaoPendente = 0;
      break;
    default:
      Serial.print("[AVISO] Comando desconhecido: ");
      Serial.println(c);
      break;
  }
}

void lerSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;

    if (c == '?') {
      imprimirAjuda();
    } else if (c == 'W' || c == 'w') {
      alternarWiFi();
    } else {
      processarComando(c);
    }
  }
}

// =====================================================
// BOTAO E BATERIA
// =====================================================

void repetirInstrucao() {
  if (ultimaInstrucao.length() > 0) {
    falar(ultimaInstrucao, false);
  } else {
    falar("NENHUMA INSTRUCAO AINDA", false);
  }
}

void pedirAjuda() {
  falar("AJUDA SOLICITADA", false);
  enviarEvento("ajuda", "botao segurado por 3 s");
  // No produto, a API avisa o contato de confianca com a localizacao do GPS.
}

void verificarBotao(unsigned long agora) {
  bool leitura = digitalRead(BOTAO_PIN);

  if (leitura != botaoUltimaLeitura) {
    botaoUltimaLeitura = leitura;
    botaoMudouEm = agora;
  }

  // Filtro de ruido do botao (debounce): aceita a mudanca apos 30 ms estavel
  if (agora - botaoMudouEm > 30 && leitura != botaoEstavel) {
    botaoEstavel = leitura;
    if (botaoEstavel == LOW) {
      botaoPressionadoEm = agora;      // pressionado
      ajudaEnviada = false;
    } else if (!ajudaEnviada) {
      repetirInstrucao();              // solto antes de 3 s: toque simples
    }
  }

  if (botaoEstavel == LOW && !ajudaEnviada && agora - botaoPressionadoEm >= TEMPO_AJUDA) {
    ajudaEnviada = true;
    pedirAjuda();
  }
}

void verificarBateria(unsigned long agora) {
  bateria = map(analogRead(BATERIA_PIN), 0, 4095, 0, 100);

  if (bateria < BATERIA_FRACA && (ultimoAvisoBateria == 0 || agora - ultimoAvisoBateria > 60000)) {
    ultimoAvisoBateria = agora;
    falar("BATERIA FRACA", false);
    enviarEvento("bateria", "abaixo de 15%");
  }
}

// =====================================================
// REDE: WI-FI, COMANDOS E EVENTOS
// =====================================================

void conectarWiFi() {
  if (!wifiHabilitado) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
  Serial.print("[REDE] Conectando ao Wi-Fi");

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[REDE] Conectado. IP: ");
    Serial.println(WiFi.localIP());
    wifiConectadoAntes = true;
  } else {
    Serial.println("[REDE] Sem conexao. Os alertas locais continuam funcionando.");
    wifiConectadoAntes = false;
  }
  proximaReconexao = millis() + INTERVALO_RECONEXAO;
}

void alternarWiFi() {
  wifiHabilitado = !wifiHabilitado;

  if (wifiHabilitado) {
    Serial.println("[TESTE] Wi-Fi religado.");
    WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
    proximaReconexao = millis() + INTERVALO_RECONEXAO;
  } else {
    Serial.println("[TESTE] Wi-Fi desligado: os alertas locais devem continuar.");
    WiFi.disconnect(true);
  }
}

void verificarRede(unsigned long agora) {
  bool conectado = (WiFi.status() == WL_CONNECTED);

  if (conectado != wifiConectadoAntes) {
    wifiConectadoAntes = conectado;
    if (conectado) {
      Serial.println("[REDE] Conexao restabelecida.");
      falhasSeguidas = 0;
    } else {
      Serial.println("[REDE] Conexao perdida. Alertas locais continuam ativos.");
      falar("MODO OFFLINE", false);
    }
  }

  // Tenta reconectar a cada 30 s, sem travar o programa
  if (wifiHabilitado && !conectado && (long)(agora - proximaReconexao) >= 0) {
    proximaReconexao = agora + INTERVALO_RECONEXAO;
    Serial.println("[REDE] Tentando reconectar...");
    WiFi.begin(WIFI_SSID, WIFI_SENHA, WIFI_CANAL);
  }
}

// Busca no servidor o proximo comando (uma ou mais letras do protocolo)
void consultarServidor(unsigned long agora) {
  if (!servidorConfigurado() || WiFi.status() != WL_CONNECTED) return;
  if ((long)(agora - proximaConsulta) < 0) return;

  HTTPClient http;
  http.setConnectTimeout(300);   // tempos curtos: o alerta local nao pode esperar a rede
  http.setTimeout(300);

  String url = String(SERVIDOR_URL) + "/dispositivos/" + DISPOSITIVO_ID + "/comando";
  int codigo = -1;

  if (http.begin(url)) {
    http.addHeader("Authorization", String("Bearer ") + DISPOSITIVO_TOKEN);
    codigo = http.GET();
    if (codigo == 200) {
      String corpo = http.getString();
      for (unsigned int i = 0; i < corpo.length(); i++) {
        if (isAlpha(corpo[i])) processarComando(corpo[i]);
      }
    }
    http.end();
  }

  if (codigo == 200 || codigo == 204) {
    falhasSeguidas = 0;
    proximaConsulta = millis() + INTERVALO_CONSULTA;
  } else {
    falhasSeguidas++;
    Serial.print("[REDE] Falha ao consultar o servidor (codigo ");
    Serial.print(codigo);
    Serial.println(").");
    proximaConsulta = millis() + (falhasSeguidas >= 3 ? ESPERA_APOS_FALHAS : INTERVALO_CONSULTA);
  }
}

// Envia um evento para o mapa de barreiras.
// Somente dados do dispositivo: nenhum nome, telefone ou outro dado pessoal (LGPD).
void enviarEvento(const char* tipo, const String &detalhe) {
  String json = "{";
  json += "\"dispositivo\":\"" + String(DISPOSITIVO_ID) + "\",";
  json += "\"tipo\":\"" + String(tipo) + "\",";
  json += "\"detalhe\":\"" + detalhe + "\",";
  json += "\"estado\":\"" + String(NOMES_ESTADO[estado]) + "\",";
  json += "\"distancia_cm\":" + String(distanciaAtual < 0 ? -1 : (int)(distanciaAtual + 0.5)) + ",";
  json += "\"bateria\":" + String(bateria) + ",";
  json += "\"t_ms\":" + String(millis());
  json += "}";

  Serial.print("[EVENTO] ");
  Serial.println(json);

  if (!servidorConfigurado() || WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(300);
  if (http.begin(String(SERVIDOR_URL) + "/eventos")) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + DISPOSITIVO_TOKEN);
    int codigo = http.POST(json);
    if (codigo < 200 || codigo >= 300) {
      Serial.print("[REDE] Evento nao enviado (codigo ");
      Serial.print(codigo);
      Serial.println(").");
    }
    http.end();
  }
}

// =====================================================
// DISPLAY OLED
// =====================================================

// Escreve o texto quebrando as linhas entre palavras (21 caracteres por linha)
void imprimirComQuebra(const String &texto, int y) {
  String linha = "";
  String palavra = "";
  int linhaY = y;

  for (unsigned int i = 0; i <= texto.length(); i++) {
    char c = (i < texto.length()) ? texto[i] : ' ';
    if (c == ' ') {
      int tamanho = linha.length() + palavra.length() + (linha.length() > 0 ? 1 : 0);
      if (tamanho > 21) {
        display.setCursor(0, linhaY);
        display.print(linha);
        linhaY += 10;
        linha = palavra;
      } else {
        if (linha.length() > 0) linha += " ";
        linha += palavra;
      }
      palavra = "";
    } else {
      palavra += c;
    }
  }

  if (linha.length() > 0) {
    display.setCursor(0, linhaY);
    display.print(linha);
  }
}

void atualizarDisplay() {
  if (!displayOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("GUIDING HAND");
  display.setCursor(98, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "W:ON" : "W:--");

  display.setCursor(0, 10);
  display.print("Dist: ");
  if (distanciaAtual < 0) {
    display.print("--");
  } else {
    display.print((int)(distanciaAtual + 0.5));
    display.print(" cm");
  }
  display.setCursor(92, 10);
  display.print("B:");
  display.print(bateria);
  display.print("%");

  display.setTextSize(2);
  display.setCursor(0, 22);
  display.print(NOMES_ESTADO[estado]);

  display.setTextSize(1);
  if (aindaValido(fraseVisivelAte)) {
    imprimirComQuebra(fraseAtual, 44);
  } else if (aindaValido(soPessoasAte)) {
    imprimirComQuebra("SO PESSOAS A FRENTE", 44);
  }

  display.display();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(VIB_ESQ_PIN, OUTPUT);
  pinMode(VIB_DIR_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BOTAO_PIN, INPUT_PULLUP);
  analogWrite(VIB_ESQ_PIN, 0);
  analogWrite(VIB_DIR_PIN, 0);

  displayOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!displayOk) {
    Serial.println("[ERRO] OLED nao encontrado. O sistema continua sem display.");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("GUIDING HAND");
    display.setCursor(0, 20);
    display.println("Conectando...");
    display.display();
  }

  Serial.println("==============================");
  Serial.println("        GUIDING HAND");
  Serial.println("==============================");
  imprimirAjuda();

  conectarWiFi();
  if (!servidorConfigurado()) {
    Serial.println("[REDE] SERVIDOR_URL vazio: modo simulacao (comandos pelo Monitor Serial).");
  }
  falar("GUIDING HAND PRONTO", false);
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  unsigned long agora = millis();

  lerSerial();

  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    atualizarDistancia();
  }

  atualizarVibracao(agora);
  verificarBotao(agora);

  if (agora - ultimaBateria >= INTERVALO_BATERIA) {
    ultimaBateria = agora;
    verificarBateria(agora);
  }

  verificarRede(agora);
  consultarServidor(agora);

  if (agora - ultimoDisplay >= INTERVALO_DISPLAY) {
    ultimoDisplay = agora;
    atualizarDisplay();
  }
}
