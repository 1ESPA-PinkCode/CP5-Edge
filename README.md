# Vinheria Agnello – Edge Computing & IoT com FIWARE

Projeto acadêmico desenvolvido para monitoramento inteligente de vinherias, integrando IoT, ESP32, sensores físicos e FIWARE via MQTT, além de um dashboard web em Python para análise de dados históricos.

A proposta é criar uma solução completa que conecta o ambiente físico ao digital, permitindo monitoramento em tempo real das condições ideais para armazenamento de vinhos.

---

## Descrição do Projeto

O Vinheria Agnello IoT é um sistema de monitoramento ambiental voltado para garantir a qualidade na conservação de vinhos.

Nesta etapa, o sistema utiliza ESP32 para coletar dados de sensores e enviar essas informações para a plataforma FIWARE.

O dispositivo físico realiza leituras contínuas do ambiente e responde com alertas visuais e sonoros quando necessário.

O projeto integra:

- dispositivo físico com ESP32
- sensores de temperatura, umidade e luminosidade
- buzzer para alertas sonoros
- LED para alertas visuais
- comunicação MQTT
- plataforma FIWARE
- armazenamento de dados históricos (STH-Comet)
- dashboard web em Python

---

## Objetivo

O objetivo é garantir o controle automatizado das condições ambientais de uma vinheria.

O sistema monitora os dados e permite:

- acompanhamento em tempo real
- análise de dados históricos
- geração de alertas automáticos
- controle remoto de alertas
- visualização via dashboard web

--- 

## Conceito da Solução

A solução funciona como um sistema inteligente de monitoramento ambiental.

O ESP32 coleta dados dos sensores e avalia se estão dentro dos limites ideais da vinheria.

Quando há alguma anomalia, o sistema responde automaticamente com alertas e envia os dados para a nuvem.

Exemplos de funcionamento:

- temperatura fora do ideal ativa alerta
- umidade inadequada gera aviso sonoro
- luminosidade alta dispara alerta contínuo
- dashboard exibe histórico dos dados

---

## Visão Geral do Projeto
### Foto da Simulação
<img width="373" height="403" alt="image" src="https://github.com/user-attachments/assets/7fc59c5c-cd18-45e9-8e1f-49c38eba0b1b" />
Link da simulação: https://wokwi.com/projects/462016964811682817

---

### Arquitetura do Projeto
<img width="1290" height="1422" alt="WhatsApp Image 2026-05-04 at 08 50 16" src="https://github.com/user-attachments/assets/84bb1408-aed6-4a8f-914a-f838269d0d4f" />


---

## Funcionalidades
- Comunicação com ESP32
- Integração com FIWARE
- Comunicação via MQTT
- Monitoramento de temperatura, umidade e luminosidade
- Alertas visuais com LED
- Alertas sonoros com buzzer
- Controle remoto de alertas
- Armazenamento de dados históricos
- Dashboard web em Python
- Análise de dados em tempo real

---

## Componentes Utilizados
- ESP32
- Sensor DHT-11 (temperatura e umidade)
- Sensor LDR (luminosidade)
- Buzzer
- LED externo
- Resistores
- Jumpers
- Protoboard virtual
- Broker MQTT
- FIWARE

---

## Componentes da Simulação
### Sensor DHT-22

Responsável por medir:
- temperatura (°C)
- umidade (%)

---

### Sensor LDR

Responsável por medir:

- luminosidade do ambiente (0–100%)

---

### Buzzer

O buzzer é utilizado para:

- alerta de temperatura fora do ideal (bipes curtos)
- alerta de umidade fora do ideal (bipes longos)
- alerta de luminosidade alta (som contínuo)

---

### LED

O LED indica o estado do sistema:

- piscando: ambiente em alerta
- desligado: ambiente normal

---

## Integração com FIWARE

O sistema envia e recebe dados utilizando MQTT integrado ao FIWARE.

Exemplos de dados transmitidos:

- temperatura
- umidade
- luminosidade
- status do ambiente
- nível de conforto
- tipo de alerta
- estado do LED
- estado do buzzer

---

## Tópicos MQTT
```text
Subscribe: /TEF/vinheriaAgnello001/cmd
Publish: /TEF/vinheriaAgnello001/attrs
CmdExe: /TEF/vinheriaAgnello001/cmdexe
```

---

## Fluxo do Sistema
```text
Sensores coletam dados
        ↓
ESP32 processa informações
        ↓
Verifica limites (triggers)
        ↓
Ativa LED e buzzer
        ↓
Publica dados via MQTT
        ↓
FIWARE recebe
        ↓
STH-Comet armazena histórico
        ↓
Dashboard consome API
        ↓
Usuário visualiza os dados
```

---

## Como Reproduzir o Projeto
### 1. Monte o circuito

Monte conforme a simulação do Wokwi ou imagem do projeto.

### 2. Abra o código

Abra o projeto na Arduino IDE ou no Wokwi.

### 3. Instale as bibliotecas
``` text
WiFi.h
PubSubClient.h
DHT.h
```

### 4. Faça upload para o ESP32

Envie o código para a placa.

### 5. Configure Wi-Fi e MQTT

Defina no código:

- SSID
- broker
- porta
- tópicos MQTT

### 6. Execute

Acompanhe os dados sendo enviados ao FIWARE e visualize no dashboard.

---

## Dashboard Vinheria Agnello

Painel web em Flask que consome dados do FIWARE (Orion :1026 + STH-Comet :8666) para monitorar a sala de guarda da Vinheria Agnello em tempo real, e envia comandos remotos ao ESP32 via Orion → IoT Agent → MQTT.

Roda como serviço systemd na porta 5000 da própria VM do FIWARE.

---

## Estrutura
```bash
dashboard-vinheria/
├── app.py
├── requirements.txt
├── dashboard-vinheria.service
├── templates/
│   └── index.html
└── static/
    ├── style.css
    └── app.js
```

---

## Pré-requisitos
- FIWARE rodando (Orion :1026, STH-Comet :8666, IoT Agent :4041)
- Entidade provisionada
- Subscription ativa no STH-Comet
- Python 3.9+
- Porta 5000 liberada

---

## Instalação
```text
sudo mkdir -p /opt/dashboard-vinheria
sudo cp -r * /opt/dashboard-vinheria/
cd /opt/dashboard-vinheria

sudo apt-get update
sudo apt-get install -y python3-venv python3-pip

sudo python3 -m venv /opt/dashboard-vinheria/.venv
sudo /opt/dashboard-vinheria/.venv/bin/pip install --upgrade pip
sudo /opt/dashboard-vinheria/.venv/bin/pip install -r requirements.txt
```
---

## Executar Manualmente 

```text
sudo /opt/dashboard-vinheria/.venv/bin/python /opt/dashboard-vinheria/app.py
```
---
## Serviço systemd
```text
sudo cp /opt/dashboard-vinheria/dashboard-vinheria.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable dashboard-vinheria
sudo systemctl start dashboard-vinheria
```

---

## Acesso
```text
http:104.197.156.52:5000
```

---

## Endpoints da API
| Método | Rota                    |
| ------ | ----------------------- |
| GET    | /                       |
| GET    | /api/current            |
| GET    | /api/history            |
| POST   | /api/cmd/alert_on       |
| POST   | /api/cmd/alert_off      |
| POST   | /api/cmd/silence_buzzer |
| GET    | /api/health             |


---

## Tecnologias Utilizadas
- ESP32
- C++ / Arduino
- MQTT
- FIWARE
- STH-Comet
- Python
- Wokwi
- IoT
- Edge Computing

---

## Equipe de Desenvolvimento
| RM | Nome |
|----|------|
| RM 567947 | Lara Mofid Essa Alssabak |
| RM 567355 | Maria Luisa Boucinhas Franco |
| RM 568459 | Maria Luiza Kochnoff da Matta |
| RM 567825 | Roberta Moreira dos Santos |

--- 

## Objetivo Acadêmico

Este projeto foi desenvolvido com foco em:

- Edge Computing
- IoT
- comunicação entre hardware e software
- integração com FIWARE
- MQTT
- sistemas embarcados
- monitoramento inteligente
