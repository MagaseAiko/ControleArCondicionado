# AR-CTRL 2000: controle de ar-condicionado

Projeto com três partes:

| Parte | Hardware | Função |
|---|---|---|
| **Receptor** (`Receptor/`) | ESP32 DevKit V1 (ESP-WROOM-32) | Concentra a lógica: lê o controle original, grava sinais, guarda o estado do ar e conversa com o celular (BLE) e com o emissor |
| **Emissor** (`Emissor/`) | ESP-01 (1MB) + LED IR | Recebe comandos por ESP-NOW e dispara o LED infravermelho |
| **Página** (`docs/index.html`) | Celular | Controle remoto completo + gravação de sinais (controle universal) |

```
 Celular ──BLE──► Receptor (ESP32) ──ESP-NOW──► Emissor (ESP-01) ──IR──► Ar-condicionado
                               ▲
                     controle original (IR)
```

## Placas suportadas

| Ambiente (`Receptor/platformio.ini`) | Placa | Conexão com o celular |
|---|---|---|
| `esp32-devkitv1` (**padrão**) | ESP32 DevKit V1 / ESP-WROOM-32 | **BLE** com a página HTTPS (GitHub Pages) |
| `esp32-s3-ble` | ESP32-S3 DevKitC | BLE |
| `esp32-c3-ble` | ESP32-C3 DevKitM | BLE |
| `esp32-s2-saola-1` | ESP32-S2 (não tem Bluetooth) | Wi-Fi: o receptor cria a rede `ArCondicionado-XXXX` (senha `arcondicionado`) e serve a página em `http://192.168.4.1` |

## Ligações

**O guia completo de montagem, com pinos, resistores, alimentação e gravação do ESP-01, está em [HARDWARE.md](HARDWARE.md).** Resumo:

| Receptor (DevKit V1) | Pino |
|---|---|
| Receptor IR (VS1838B), OUT | GPIO27 |
| Receptor IR, VCC / GND | 3V3 / GND |
| Botão GRAVAR → GND | GPIO25 |
| Botão ENVIAR → GND | GPIO26 |
| LED de status (azul, na placa) | GPIO2 |

| Emissor (ESP-01) | Pino |
|---|---|
| LED IR via transistor NPN (1 kΩ na base) | GPIO3 (RX) |

Botões físicos: **GRAVAR** grava um sinal no espaço `rapido` (aperte de novo para cancelar). **ENVIAR** envia esse sinal.

## Gravar o firmware

Com o PlatformIO (VS Code), abra cada pasta como projeto ou use:

```bash
cd Emissor  && pio run -t upload
cd Receptor && pio run -t upload                       # ESP32 DevKit V1
cd Receptor && pio run -e esp32-s2-saola-1 -t upload   # outra placa
```

No modo Wi-Fi (só ESP32-S2), o build executa `tools/embed_web.py`, que compacta `docs/index.html` dentro do firmware.

O Emissor pareia sozinho: ao ligar, ele se anuncia por broadcast e o Receptor guarda o primeiro emissor que ouvir. Para trocar de emissor, use CONFIG → Esquecer.

Os dois usam o **canal Wi-Fi 1** (`LINK_CHANNEL` em `common/link_protocol.h`).

## Publicar a página (HTTPS para o Web Bluetooth)

O Web Bluetooth só funciona em páginas HTTPS. A página fica em `docs/` para o **GitHub Pages**:

1. Envie o repositório para o GitHub (`git push`).
2. No repositório: **Settings → Pages → Build and deployment → Deploy from a branch → `main` / `/docs`**.
3. Endereço: `https://magaseaiko.github.io/ControleArCondicionado/`

Navegadores: **Chrome no Android**, ou **Bluefy** no iPhone (o Safari não tem Web Bluetooth).
Só no ESP32-S2 (modo Wi-Fi) não precisa de HTTPS: conecte o celular na rede `ArCondicionado-XXXX` e abra `http://192.168.4.1`. Se o Android avisar "rede sem internet", escolha manter a conexão.

A aba **CONFIG → Modo demo** simula o receptor, para testar a página sem hardware.

## Como usar

### Modo PROTOCOLO (recomendado)

1. Aba **GRAVAR** → GRAVAR → aperte qualquer botão do controle original do ar.
2. Se o protocolo for reconhecido (Samsung, LG, Gree, Midea, Daikin, Fujitsu e mais de 70 outros), ele é configurado automaticamente.
3. A aba **CONTROLE** passa a controlar tudo: liga/desliga, modo, temperatura, ventilação, aletas, turbo, eco etc. O receptor gera o sinal completo, e o emissor envia com o `IRac`.
4. Se você usar o controle original, o receptor lê o sinal e atualiza o estado na página.

Se a detecção não funcionar, escolha o protocolo em **CONFIG → Protocolo do ar**.

### Modo APRENDIDO (qualquer ar, inclusive protocolo desconhecido)

Cada botão da página procura um sinal gravado com um nome padrão. Grave pela aba **GRAVAR → Mapa de funções**:

| Ação na página | Sinais procurados (em ordem) |
|---|---|
| Desligar | `power_off`, `power` |
| Ligar | `<modo>_<temp>` (ex.: `cool_23`), `power_on`, `power` |
| Mudar modo/temperatura | `<modo>_<temp>`, depois `mode_<modo>` / `mode`, `temp_<t>` ou `temp_up`/`temp_down` repetido |
| Ventilação | `fan_<auto/min/low/medium/high/max>`, `fan` |
| Aleta vertical | `swing_off` / `swing_<posição>`, `swing_on`, `swing` |
| Aleta horizontal | `swingh_off` / `swingh_<posição>`, `swingh_on`, `swingh` |
| Turbo, eco, silencioso, luz, limpar, filtro, bipe, sono | `<função>_on` / `<função>_off`, `<função>` |

Como o controle de ar envia o estado completo a cada toque, o jeito mais confiável é gravar `cool_16` … `cool_30` (e `heat_*` se usar aquecimento) mais `power_off`.

### Controle universal

Na aba GRAVAR, escolha "sinal livre", dê um nome (ex.: `tv_liga`) e grave. Os sinais ficam na lista **Sinais salvos**, onde podem ser enviados, renomeados ou apagados.

## Estrutura

```
common/            protocolo ESP-NOW compartilhado (link_protocol.h, link_ac.h)
Emissor/src/       main.cpp: ESP-NOW → IRsend / IRac
Receptor/src/
  main.cpp         lógica: gravação, fila de envios, modos, botões, LED
  link.*           ESP-NOW (pareamento, envio com confirmação, sinais em pedaços)
  signals.*        sinais gravados no LittleFS
  ac_json.*        estado do ar <-> JSON
  transport_ble.*  Web Bluetooth (NimBLE)
  transport_wifi.* rede Wi-Fi própria + servidor da página
Receptor/tools/    embed_web.py (embute a página no firmware)
docs/index.html    página do celular
```

### Protocolo com o celular

Mensagens JSON, uma por linha. Pelo BLE, a página escreve na característica `…0002` e recebe notificações em `…0003`. Pelo Wi-Fi, a página faz `POST /api`.

- Página → receptor: `{"c":"hello"}`, `{"c":"ac","ac":{"power":true,"mode":"cool","temp":23,...}}`, `{"c":"learn","key":"cool_23"}`, `{"c":"send","key":"..."}`, `{"c":"del"|"rename"|"setProto"|"setMode"|"ping"|"unpair"|"protocols"|"resend", ...}`
- Receptor → página: `{"t":"status"|"signals"|"protocols"|"learned"|"learnTimeout"|"sent"|"missing"|"remote"|"error", ...}`
