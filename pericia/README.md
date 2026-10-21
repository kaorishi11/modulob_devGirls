# Análise de Tráfego de Rede - Incidente VaxGuard Connect

[![Wireshark](https://img.shields.io/badge/Tool-Wireshark-blue.svg)](https://www.wireshark.org/)
[![Protocol-MQTT](https://img.shields.io/badge/Protocol-MQTT-green.svg)](https://mqtt.org/)
[![Protocol-HTTP](https://img.shields.io/badge/Protocol-HTTP-orange.svg)](https://developer.mozilla.org/en-US/docs/Web/HTTP)
[![Team](https://img.shields.io/badge/Team-DevGirls-purple.svg)](#)

## Contexto da Investigação

Durante a operação do sistema **VaxGuard Connect**, a câmara frigorífica **CAM03** registrou uma excursão térmica inesperada, colocando em risco insumos sensíveis (como vacinas). A equipe de redes da **BioFrio** coletou a captura de tráfego (`captura_biofrio_2026.pcap`) no período para investigar a causa raiz do incidente.

A análise confirmou que a elevação da temperatura não decorreu de uma falha mecânica, mas sim de uma **intervenção maliciosa via rede**, na qual um host não autorizado injetou comandos de desligamento no sistema de refrigeração.

---

## Detalhes do Ambiente de Rede

- **Sub-rede Interna:** `192.168.20.0/24`
- **Servidor Central (API Web + Broker MQTT):** `192.168.20.10`
- **Dispositivo da Câmara CAM03 (ESP32):** `192.168.20.31`
- **Atacante / Host Não Autorizado:** `192.168.20.66`

---

## Resolução das Questões

### QUESTÃO 1 — Reconhecimento do Dispositivo

* **A)** **IP do Dispositivo (Cliente / CAM03):** `192.168.20.31` | **Versão do IP:** IPv4
* **B)** **IP do Servidor / Broker MQTT:** `192.168.20.10` | **Versão do IP:** IPv4
* **C)** **Protocolo Utilizado na Telemetria:** MQTT (Frame 13 em diante)
* **D)** **Porta de Destino do Broker:** `1883` (Porta padrão MQTT sem TLS)
* **E)** **Payload da Telemetria Inicial (Frame 13):**
  ```json
  {
    "device": "VG-CAM03",
    "placa": "ESP32",
    "seq": 100,
    "temp": 4.8,
    "umid": 58,
    "porta": "FECHADA",
    "estado": "NORMAL"
  }
  ```
* **F)** **Tópico MQTT da Telemetria:** `biofrio/cam03/telemetria`

---

### QUESTÃO 2 — Análise de Autenticação e Credenciais

* **A)** **Usuário do Painel HTTP:** `tecnico.cam03`
* **B)** **Senha Capturada no Painel:** `Vac@BioFrio2026` *(Nota: No payload via HTTP POST o caractere `@` estava codificado em URL como `%40`)*
* **C)** **Cookie de Sessão HTTP:** `session=c6a1f09b7d2e4418`
* **D)** **Credenciais de Acesso ao Broker MQTT:**
  * **User Name:** `vaxguard` (Tamanho: 8 bytes)
  * **Password:** `Fr10#Cam03` (Tamanho: 10 bytes)

---

### QUESTÃO 3 — Análise da Injeção de Comando e Impacto

* **A)** **Porta de Origem do Atacante:** `44120`
* **B) Injeção do Comando Malicioso (Frame 24):**
  * **Horário:** `2026-10-20 14:14:30.000000`
  * **Origem:** `192.168.20.66` (Host não autorizado / IP de atacante)
  * **Destino:** `192.168.20.10` (Broker MQTT)
  * **Payload:** 
    ```json
    {"cmd": "VENTILACAO_OFF", "cmd_id": "x90"}
    ```
  > **Nota de Autorização:** O IP `192.168.20.66` não tinha autorização legitima para emitir comandos. O cliente legítimo da câmara frigorífica opera no IP final `.31`.

* **C)** **Encaminhamento pelo Broker (Frame 25):**
  * O broker MQTT repassou a mensagem do tópico `biofrio/cam03/cmd` para os clientes inscritos (`192.168.20.31`), pois não havia controle de acesso (ACL) limitando quem podia publicar no tópico.

* **D) Impacto Operacional:**
  * O comando `VENTILACAO_OFF` desligou o sistema de circulação de ar/refrigeração da câmara fria CAM03.
  * Ocorreu uma excursão térmica acentuada: a temperatura subiu rapidamente de **4.8°C** para **13.5°C** (atingindo o estado **CRÍTICO**), comprometendo a integridade das vacinas armazenadas.

* **E) Controles Ausentes:** Autenticação rigorosa de publicação e criptografia de mensagens no canal IoT.

---

### QUESTÃO 4 — Reconstrução da Linha do Tempo e Recomendações

#### A) Cronologia do Incidente (20/10/2026)

| Horário | Origem | Destino | Evento / Ação |
| :--- | :--- | :--- | :--- |
| **14:10:00** | Client | `192.168.20.10` | Requisição `POST /painel/login` envia usuário `tecnico.cam03` e senha em texto claro. |
| **14:10:00** | `192.168.20.10` | Client | Resposta HTTP `302 Found` fornecendo o cookie de sessão `session=c6a1f09b7d2e4418`. |
| **14:10:01** | `192.168.20.31` | `192.168.20.10` | Dispositivo CAM03 (ESP32) conecta ao broker MQTT e publica telemetrias normais ($4.8^\circ\text{C}$). |
| **14:14:30** *(F24)* | `192.168.20.66` | `192.168.20.10` | **Host Não Autorizado** injeta comando `VENTILACAO_OFF` no tópico `biofrio/cam03/cmd`. |
| **14:14:30** *(F25)* | `192.168.20.10` | `192.168.20.31` | Broker repassa o comando `VENTILACAO_OFF` para a câmara CAM03. |
| **14:15:05** *(F26)* | `192.168.20.31` | `192.168.20.10` | Telemetria Seq 108: Temperatura atinge **$10.4^\circ\text{C}$** (Estado: **CRÍTICO**). |
| **14:15:35** *(F27)* | `192.168.20.31` | `192.168.20.10` | Telemetria Seq 109: Temperatura atinge **$11.9^\circ\text{C}$** (Estado: **CRÍTICO**). |
| **14:16:05** *(F28)* | `192.168.20.31` | `192.168.20.10` | Telemetria Seq 110: Temperatura atinge **$13.5^\circ\text{C}$** (Estado: **CRÍTICO**). |

#### B) Conclusão do Incidente
Trata-se de um **ataque malicioso de injeção de comando**. O host `192.168.20.66` explorou a falta de controles de acesso no broker MQTT para emitir o comando de desligamento do sistema de refrigeração.

#### C) Recomendações de Mitigação e Segurança

1. **Criptografia de Transporte (TLS/HTTPS e MQTTS):**
   * Substituir o tráfego Web HTTP sem criptografia por **HTTPS** para mitigar a captura de credenciais em texto claro.
   * Habilitar **MQTTS** (porta 8883) com suporte a TLS para impedir interceptação e *tampering* dos dados de telemetria e comando.
2. **Autenticação Forte e Mútua (mTLS):**
   * Exigir certificados digitais para autenticação mútua entre o dispositivo IoT (ESP32) e o broker.
3. **Listas de Controle de Acesso no Broker (ACLs):**
   * Configurar regras estritas de ACL no broker MQTT para que **apenas** o painel de gerenciamento oficial tenha permissão de publicação (`Publish`) no tópico de comandos (`biofrio/cam03/cmd`). O dispositivo ESP32 deve ter permissão de apenas leitura (`Subscribe`) nesse tópico.
