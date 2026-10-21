#include <DHT.h>

#define DHT_PIN 2
#define DHT_TYPE DHT11

#define HALL_PIN 3
#define BUZZER_PIN 4
#define LED_VERDE 5
#define LED_AMARELO 7
#define LED_VERMELHO 6
#define BUTTON_PIN 8
#define RELAY_PIN 9

const int MAGNET_DETECTED_LEVEL = LOW;
const int RELAY_ACTIVE_LEVEL = HIGH;

const float TEMP_MIN = 2.0;
const float TEMP_MAX = 8.0;
const float MARGEM_ATENCAO = 1.0;

const unsigned long INTERVALO_LEITURA = 5000;
const unsigned long TEMPO_ATENCAO_PORTA = 20000;
const unsigned long TEMPO_CRITICO_PORTA = 60000;
const unsigned long TEMPO_CRITICO_TEMP = 30000;
const unsigned long INTERVALO_BIPE = 10000;
const unsigned long DURACAO_BIPE = 150;

DHT dht(DHT_PIN, DHT_TYPE);

enum Estado {
  NORMAL,
  ATENCAO,
  CRITICO,
  RECONHECIDO,
  FALHA_SENSOR
};

Estado estadoAtual = NORMAL;

unsigned long ultimaLeitura = 0;
unsigned long inicioPortaAberta = 0;
unsigned long inicioTempFora = 0;
unsigned long ultimoBipe = 0;
unsigned long inicioBipe = 0;
unsigned long ultimaAlternancia = 0;
unsigned long inicioBotao = 0;

bool portaAberta = false;
bool temporizadorPortaAtivo = false;
bool temporizadorTempAtivo = false;
bool bipeAtivo = false;
bool ledsFalhaAlternados = false;
bool redReconhecidoLigado = false;
bool botaoEstavel = HIGH;
bool ultimaLeituraBotao = HIGH;
bool alarmeReconhecido = false;

int falhasDHT = 0;
float temperatura = NAN;
float umidade = NAN;

const unsigned long DEBOUNCE_MS = 40;
const unsigned long INTERVALO_PISCAR = 400;

const char* nomeEstado(Estado e) {
  switch (e) {
    case NORMAL: return "NORMAL";
    case ATENCAO: return "ATENCAO";
    case CRITICO: return "CRITICO";
    case RECONHECIDO: return "RECONHECIDO";
    case FALHA_SENSOR: return "FALHA_SENSOR";
    default: return "DESCONHECIDO";
  }
}

void registrarEvento(Estado anterior, Estado novo, const char* motivo) {
  Serial.print("{\"evento\":\"mudanca_estado\",\"anterior\":\"");
  Serial.print(nomeEstado(anterior));
  Serial.print("\",\"novo\":\"");
  Serial.print(nomeEstado(novo));
  Serial.print("\",\"motivo\":\"");
  Serial.print(motivo);
  Serial.println("\"}");
}

void mudarEstado(Estado novo, const char* motivo) {
  if (novo == estadoAtual) return;

  Estado anterior = estadoAtual;
  estadoAtual = novo;
  registrarEvento(anterior, novo, motivo);

  if (novo == CRITICO) {
    alarmeReconhecido = false;
  }

  if (novo == NORMAL) {
    alarmeReconhecido = false;
  }

  if (novo == FALHA_SENSOR) {
    alarmeReconhecido = false;
  }
}

void atualizarPorta(unsigned long agora) {
  bool magnetoDetectado =
    digitalRead(HALL_PIN) == MAGNET_DETECTED_LEVEL;

  portaAberta = !magnetoDetectado;

  if (portaAberta) {
    if (!temporizadorPortaAtivo) {
      inicioPortaAberta = agora;
      temporizadorPortaAtivo = true;
      Serial.println("{\"evento\":\"porta_aberta\"}");
    }
  } else {
    if (temporizadorPortaAtivo) {
      Serial.print("{\"evento\":\"porta_fechada\",\"duracao_ms\":");
      Serial.print(agora - inicioPortaAberta);
      Serial.println("}");
    }

    temporizadorPortaAtivo = false;
  }
}

void lerDHT(unsigned long agora) {
  if (agora - ultimaLeitura < INTERVALO_LEITURA) return;

  ultimaLeitura = agora;

  float novaTemp = dht.readTemperature();
  float novaUmid = dht.readHumidity();

  if (isnan(novaTemp) || isnan(novaUmid)) {
    falhasDHT++;

    Serial.print("{\"evento\":\"falha_leitura_dht\",\"consecutivas\":");
    Serial.print(falhasDHT);
    Serial.println("}");

    if (falhasDHT >= 3) {
      mudarEstado(FALHA_SENSOR, "3 leituras invalidas do DHT11");
    }

    return;
  }

  falhasDHT = 0;
  temperatura = novaTemp;
  umidade = novaUmid;

  Serial.print("{\"temp\":");
  Serial.print(temperatura, 1);
  Serial.print(",\"umid\":");
  Serial.print(umidade, 1);
  Serial.print(",\"porta\":\"");
  Serial.print(portaAberta ? "ABERTA" : "FECHADA");
  Serial.print("\",\"estado\":\"");
  Serial.print(nomeEstado(estadoAtual));
  Serial.println("\"}");
}

void lerBotao(unsigned long agora) {
  bool leitura = digitalRead(BUTTON_PIN);

  if (leitura != ultimaLeituraBotao) {
    inicioBotao = agora;
    ultimaLeituraBotao = leitura;
  }

  if (agora - inicioBotao >= DEBOUNCE_MS &&
      leitura != botaoEstavel) {
    botaoEstavel = leitura;

    if (botaoEstavel == LOW && estadoAtual == CRITICO) {
      alarmeReconhecido = true;
      mudarEstado(RECONHECIDO, "botao pressionado durante critico");
    }
  }
}

void atualizarEstado(unsigned long agora) {
  if (falhasDHT >= 3) {
    mudarEstado(FALHA_SENSOR, "sensor indisponivel");
    return;
  }

  bool tempFora = temperatura < TEMP_MIN || temperatura > TEMP_MAX;

  if (tempFora) {
    if (!temporizadorTempAtivo) {
      inicioTempFora = agora;
      temporizadorTempAtivo = true;
    }
  } else {
    temporizadorTempAtivo = false;
  }

  bool portaCritica = portaAberta &&
    temporizadorPortaAtivo &&
    agora - inicioPortaAberta >= TEMPO_CRITICO_PORTA;

  bool tempCritica = tempFora &&
    temporizadorTempAtivo &&
    agora - inicioTempFora >= TEMPO_CRITICO_TEMP;

  if (portaCritica || tempCritica) {
    if (estadoAtual != RECONHECIDO) {
      const char* motivo = portaCritica
        ? "porta aberta por 60 segundos"
        : "temperatura fora da faixa por 30 segundos";

      mudarEstado(CRITICO, motivo);
    }
    return;
  }

  if (estadoAtual == RECONHECIDO) {
    if (!tempFora && !portaAberta) {
      mudarEstado(NORMAL, "temperatura normalizada e porta fechada");
    }
    return;
  }

  bool pertoLimite =
    temperatura <= TEMP_MIN + MARGEM_ATENCAO ||
    temperatura >= TEMP_MAX - MARGEM_ATENCAO;

  bool portaAtencao = portaAberta &&
    temporizadorPortaAtivo &&
    agora - inicioPortaAberta >= TEMPO_ATENCAO_PORTA;

  if (tempFora || pertoLimite || portaAtencao) {
    const char* motivo = tempFora
      ? "temperatura fora da faixa"
      : (portaAtencao
        ? "porta aberta por mais de 20 segundos"
        : "temperatura proxima do limite");

    mudarEstado(ATENCAO, motivo);
  } else {
    mudarEstado(NORMAL, "temperatura na faixa e porta fechada");
  }
}

void atualizarBuzzer(unsigned long agora) {
  if (estadoAtual == CRITICO) {
    if (!alarmeReconhecido) {
      tone(BUZZER_PIN, 2000);
    } else {
      noTone(BUZZER_PIN);
    }
    bipeAtivo = false;
    return;
  }

  if (estadoAtual == RECONHECIDO ||
      estadoAtual == NORMAL ||
      estadoAtual == FALHA_SENSOR) {
    noTone(BUZZER_PIN);
    bipeAtivo = false;
    return;
  }

  if (estadoAtual == ATENCAO) {
    if (!bipeAtivo && agora - ultimoBipe >= INTERVALO_BIPE) {
      tone(BUZZER_PIN, 1800);
      inicioBipe = agora;
      ultimoBipe = agora;
      bipeAtivo = true;
    }

    if (bipeAtivo && agora - inicioBipe >= DURACAO_BIPE) {
      noTone(BUZZER_PIN);
      bipeAtivo = false;
    }
  }
}

void atualizarSaidas(unsigned long agora) {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARELO, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  switch (estadoAtual) {
    case NORMAL:
      digitalWrite(LED_VERDE, HIGH);
      digitalWrite(RELAY_PIN, !RELAY_ACTIVE_LEVEL);
      break;

    case ATENCAO:
      digitalWrite(LED_AMARELO, HIGH);
      digitalWrite(RELAY_PIN, !RELAY_ACTIVE_LEVEL);
      break;

    case CRITICO:
      digitalWrite(LED_VERMELHO, HIGH);
      digitalWrite(RELAY_PIN, RELAY_ACTIVE_LEVEL);
      break;

    case RECONHECIDO:
      redReconhecidoLigado =
        ((agora / INTERVALO_PISCAR) % 2 == 0);
      digitalWrite(LED_VERMELHO, redReconhecidoLigado);
      digitalWrite(RELAY_PIN, RELAY_ACTIVE_LEVEL);
      break;

    case FALHA_SENSOR:
      if (agora - ultimaAlternancia >= INTERVALO_PISCAR) {
        ultimaAlternancia = agora;
        ledsFalhaAlternados = !ledsFalhaAlternados;
      }

      digitalWrite(LED_AMARELO, ledsFalhaAlternados);
      digitalWrite(LED_VERMELHO, !ledsFalhaAlternados);
      digitalWrite(RELAY_PIN, RELAY_ACTIVE_LEVEL);
      break;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(HALL_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, !RELAY_ACTIVE_LEVEL);
  noTone(BUZZER_PIN);

  dht.begin();

  Serial.println("VaxGuard Connect - teste local iniciado");
  Serial.println("Aguardando primeira leitura do DHT11...");
}

void loop() {
  unsigned long agora = millis();

  atualizarPorta(agora);
  lerDHT(agora);
  lerBotao(agora);
  atualizarEstado(agora);
  atualizarBuzzer(agora);
  atualizarSaidas(agora);
}
