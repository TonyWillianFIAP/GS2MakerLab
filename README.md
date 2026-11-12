# Guiding Hand

![Capa da apresentação do Guiding Hand](docs/apresentacao/slides/01.png)

Óculos inteligentes que avisam pessoas cegas ou com baixa visão sobre obstáculos que a bengala não alcança e as guiam pela cidade por vibração e voz, com o celular guardado no bolso. Projeto do Project-Based Maker Lab, Global Solution 2026 CitizenTech, Engenharia de Software 4º ano da FIAP.

## Integrantes

João Rodrigo Solano Nogueira | 551319

Julia Amorim Bezerra | 99609

Lana Giulia Auada Leite | 551143

Tony Willian da Silva Segalin | 550667

## Vídeo demonstrativo

▶ **[Assista no YouTube](https://youtu.be/pfPCI3VZzHk)** (o aúdio está baixo)

Simulação pronta para abrir no navegador: **(https://wokwi.com/projects/479899778643629057)**

## O problema

- No Brasil, 7,9 milhões de pessoas têm deficiência visual (IBGE, Censo 2022).
- A bengala percebe o chão e o que está perto dele. Placas, orelhões, lixeiras e caçambas, na altura do tronco e da cabeça, passam despercebidos.
- Para se orientar, a pessoa precisa segurar o celular na rua. Na capital paulista foram 154.058 roubos e furtos de celular em 2025 (SSP-SP).

## A solução

O Guiding Hand evolui o robô do Maker Lab para um dispositivo vestível. O sensor ultrassônico do Smart Parking detecta obstáculos, o controle por PWM aciona a vibração das hastes, e o protocolo de comandos do Vision Wheel recebe o que a visão computacional identifica.

| No robô do Maker Lab | No Guiding Hand |
| --- | --- |
| Webcam no computador + Python e MediaPipe | Câmera do ESP32-CAM nos óculos + YOLO no servidor |
| Comandos D, E e S pela porta serial | Comandos de rota e de obstáculo pelo Wi-Fi |
| Ponte H + motor DC + roda | Ponte H dupla + dois motores de vibração, um em cada haste |
| Velocidade do motor por PWM | Intensidade da vibração por PWM, conforme a distância |
| HC-SR04 com quatro zonas (Smart Parking) | HC-SR04 na armação, com zonas ajustadas à caminhada |
| LEDs, buzzer e OLED | Vibração nas hastes e frases de voz perto do ouvido |

A regra principal: **o alerta de proximidade é decidido no próprio ESP32 e nunca depende da internet.** A câmera e o servidor apenas acrescentam o tipo do obstáculo e as instruções de rota.

## Arquivos

| Arquivo | Conteúdo |
| --- | --- |
| `sketch.ino` | Programa do ESP32 |
| `diagram.json` | Circuito do Wokwi |
| `config.h` | Wi-Fi, endereço do servidor e identificação do dispositivo |
| `libraries.txt` | Bibliotecas do OLED |
| `testes/` | Testes automáticos da lógica, com hardware simulado |
| `docs/` | Apresentação e roteiro do vídeo |
| `.gitignore` | Deixa fora do repositório os arquivos gerados pelos testes |

## Componentes

| Componente | Qtd. | Onde fica | Função |
| --- | --- | --- | --- |
| ESP32 DevKit V1 | 1 | Módulo de cintura | Lê os sensores, decide os alertas e conecta ao Wi-Fi |
| ESP32-CAM (câmera OV2640) | 1 | Ponte dos óculos | Fotografa a cena à frente e envia ao servidor |
| Sensor ultrassônico HC-SR04 | 1 | Frente da armação | Mede a distância na altura do tronco e da cabeça |
| Ponte H dupla MX1508 | 1 | Módulo de cintura | Aciona os motores de vibração por PWM |
| Motor de vibração tipo moeda, 3 V | 2 | Hastes esquerda e direita | Alerta tátil e indicação de direção |
| DFPlayer Mini + microSD + alto-falante | 1 | Módulo de cintura e haste | Frases de voz gravadas |
| Módulo GPS NEO-6M | 1 | Módulo de cintura | Posição para a navegação |
| Botão tátil | 1 | Haste direita | Repetir instrução e pedir ajuda |
| Power bank 5 V | 1 | Módulo de cintura | Alimentação |
| Armação e caixa impressas em 3D | 1 | — | Suporte e acabamento |

Na simulação do Wokwi, LEDs, buzzer, OLED e potenciômetro representam a vibração, a voz e a bateria.

## Ligações

| Componente | Pino | ESP32 |
| --- | --- | --- |
| HC-SR04 | VCC / GND | 5V / GND |
| HC-SR04 | TRIG / ECHO | GPIO 5 / GPIO 18 |
| OLED SSD1306 | VCC / GND | 3V3 / GND |
| OLED SSD1306 | SDA / SCL | GPIO 21 / GPIO 22 |
| Vibração esquerda (LED no Wokwi) | Sinal PWM | GPIO 25 |
| Vibração direita (LED no Wokwi) | Sinal PWM | GPIO 27 |
| Buzzer | + / − | GPIO 14 / GND |
| Botão | Terminais | GPIO 13 (INPUT_PULLUP) / GND |
| Potenciômetro (bateria) | SIG / VCC / GND | GPIO 34 / 3V3 / GND |

## Integração entre sensores e IA

1. **Medir:** o HC-SR04 mede a distância a cada 60 ms. A distância usada é a mediana das 3 últimas leituras.
2. **Decidir no ESP32:** a distância define o estado e a vibração, sem esperar a rede.
3. **Fotografar:** o ESP32-CAM envia a foto da cena ao servidor.
4. **Classificar:** o modelo YOLO treinado identifica placa, galho, poste, carro na calçada, buraco, degrau, faixa de pedestre ou pessoa.
5. **Responder:** o servidor devolve uma letra do protocolo de comandos.
6. **Agir:** o ESP32 combina a letra com a distância e decide se fala, vibra ou silencia.

| Distância | Estado | Vibração | Som e fala |
| --- | --- | --- | --- |
| Acima de 200 cm | LIVRE | Desligada | — |
| 120 a 200 cm | ATENÇÃO | Pulsos lentos (150 ms a cada 800 ms) | Tipo do obstáculo, se a câmera identificou |
| 60 a 119 cm | CUIDADO | Pulsos rápidos (150 ms a cada 300 ms) | Tipo do obstáculo |
| Abaixo de 60 cm | PERIGO | Contínua e máxima | Alerta contínuo e "PARE" |

Regras de decisão autônoma:

- **Câmera + sensor:** o tipo do obstáculo só é falado se o sensor confirmar a aproximação em até 3 s.
- **Só pessoas:** pedestres andando silenciam o estado ATENÇÃO por 2 s. CUIDADO e PERIGO continuam valendo.
- **Sem repetição:** o mesmo obstáculo não é anunciado de novo em menos de 5 s.
- **Prioridade:** em PERIGO, a vibração contínua se sobrepõe a qualquer instrução de rota.

### Protocolo de comandos

O servidor envia as letras pelo Wi-Fi. Na simulação, elas são digitadas no Monitor Serial, como o Python fazia no Vision Wheel.

| Comando | Significado | Ação |
| --- | --- | --- |
| E | Vire à esquerda | Dois pulsos na haste esquerda + fala |
| D | Vire à direita | Dois pulsos na haste direita + fala |
| F | Siga em frente | Um pulso nas duas hastes + fala |
| H | Destino alcançado | Três pulsos nas duas hastes + fala |
| A | Obstáculo na altura da cabeça | Fala, confirmada pelo sensor |
| C | Carro na calçada | Fala, confirmada pelo sensor |
| B | Buraco ou degrau | Fala, confirmada pelo sensor |
| T | Faixa de pedestre à frente | Fala |
| P | Apenas pessoas à frente | Silencia ATENÇÃO por 2 s |
| S | Nada relevante | Limpa as classificações pendentes |
| W | Teste: liga ou desliga o Wi-Fi | Só no Monitor Serial |
| ? | Ajuda | Lista os comandos |
