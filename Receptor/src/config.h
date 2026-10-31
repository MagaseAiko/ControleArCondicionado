#pragma once
// Configurações do Receptor. Os pinos podem ser trocados no platformio.ini.
// Padrões: ESP32 DevKit V1 (ESP-WROOM-32).

#ifndef PIN_IR_RX
#define PIN_IR_RX 27       // saída do receptor IR (VS1838B / TSOP38238)
#endif
#ifndef PIN_BTN_REC
#define PIN_BTN_REC 25     // botão GRAVAR (para o GND)
#endif
#ifndef PIN_BTN_SEND
#define PIN_BTN_SEND 26    // botão ENVIAR (para o GND)
#endif
#ifndef PIN_RGB
#define PIN_RGB -1         // LED RGB endereçável (-1 = não tem)
#endif
#ifndef PIN_LED
#define PIN_LED -1         // LED simples de status, usado quando não há RGB (DevKit V1: GPIO2)
#endif

#define FW_VERSION "1.0.0"
#define DEVICE_NAME "ArCondicionado"
#define AP_PASSWORD "arcondicionado"   // senha da rede Wi-Fi (modo Wi-Fi), mínimo 8 caracteres

#define QUICK_SLOT "rapido"            // sinal usado pelos botões físicos
#define LEARN_TIMEOUT_MS 15000
#define EMITTER_TIMEOUT_MS 12000       // sem HELLO por esse tempo = emissor offline
#define RESULT_TIMEOUT_MS 3000
#define JOB_GAP_MS 350                 // intervalo entre comandos IR seguidos
#define MAX_JOBS 24

#define IR_CAPTURE_BUFFER 1024
#define IR_TIMEOUT_MS 50               // controles de ar mandam mensagens longas
#define IR_MIN_UNKNOWN_LEN 12          // abaixo disso é ruído
