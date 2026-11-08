# Controle universal IR (ESP32 + ESP-01)

Projeto com três partes:

| Parte | Hardware | Função |
|---|---|---|
| **Receptor** (`Receptor/`) | ESP32 DevKit V1 (ESP-WROOM-32) | Grava os sinais do controle original, guarda os perfis e os sinais e conversa com o celular (BLE) e com o emissor |
| **Emissor** (`Emissor/`) | ESP-01 (1MB) + LED IR | Recebe comandos por ESP-NOW e dispara o LED infravermelho |
| **Página** (`docs/index.html`) | Celular | Perfis de controle com tela editável + gravação de sinais |

```
 Celular ──BLE──► Receptor (ESP32) ──ESP-NOW──► Emissor (ESP-01) ──IR──► Ar, TV, ventilador...
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

O Emissor pareia sozinho: ao ligar, ele se anuncia por broadcast e o Receptor guarda o primeiro emissor que ouvir. Para trocar de emissor, use **Ajustes → Esquecer emissor**.

Os dois usam o **canal Wi-Fi 1** (`LINK_CHANNEL` em `common/link_protocol.h`).

## Publicar a página (HTTPS para o Web Bluetooth)

O Web Bluetooth só funciona em páginas HTTPS. A página fica em `docs/` para o **GitHub Pages**:

1. Envie o repositório para o GitHub (`git push`).
2. No repositório: **Settings → Pages → Build and deployment → Deploy from a branch → `main` / `/docs`**.
3. Endereço: `https://magaseaiko.github.io/ControleArCondicionado/`

Navegadores: **Chrome no Android**, ou **Bluefy** no iPhone (o Safari não tem Web Bluetooth).
Só no ESP32-S2 (modo Wi-Fi) não precisa de HTTPS: conecte o celular na rede `ArCondicionado-XXXX` e abra `http://192.168.4.1`. Se o Android avisar "rede sem internet", escolha manter a conexão.

**Ajustes → Modo demonstração** simula o receptor, para conhecer a página sem hardware.

## Como usar

Não há protocolos prontos: cada controle é um **perfil**, e você grava os botões do controle original.

1. **Perfis → Novo controle.** Dê um nome e escolha o tipo: Ar-condicionado, TV, Ventilador, Som ou Personalizado. O tipo traz uma lista de **comandos base** e uma tela inicial. O Personalizado começa vazio.
2. **Comandos → Gravar pendentes.** A página pede um comando de cada vez. Aponte o controle original para o receptor e aperte o botão correspondente. Cada comando pode ser testado, regravado, renomeado ou apagado, e você pode criar os seus (ex.: "Frio 23°", "Netflix").
3. **Controle.** Toque nos botões para enviar. Um ponto vermelho marca o que ainda não foi gravado. Tocar nele abre a gravação.
4. **Controle → Editar** muda a tela daquele perfil:
   - adicionar, mover, apagar e renomear **seções**;
   - adicionar **botões** e escolher o comando, o texto, o ícone, a cor e a largura (1 a 4 colunas);
   - usar um **par + / −** (volume, temperatura, canal) ou um **direcional** (setas + OK).

Os perfis e os sinais ficam guardados no **receptor**, então aparecem em qualquer celular que se conectar a ele. O celular também guarda uma cópia para mostrar a tela sem conexão.

**Se o aparelho não responder a um comando gravado:** em **Perfis → editar**, ative **Enviar como sinal bruto**. O emissor passa a repetir os tempos gravados em vez de recriar o código do protocolo. **Ajustes → Último sinal recebido** mostra o que o receptor leu de um controle.

Os botões físicos do receptor continuam funcionando: **GRAVAR** guarda um sinal no espaço rápido (`rapido`) e **ENVIAR** repete esse sinal.

## Estrutura

```
common/            protocolo ESP-NOW compartilhado (link_protocol.h)
Emissor/src/       main.cpp: ESP-NOW → IRsend (código decodificado ou sinal bruto)
Receptor/src/
  main.cpp         lógica: gravação, fila de envios, botões, LED
  link.*           ESP-NOW (pareamento, envio com confirmação, sinais em pedaços)
  signals.*        sinais gravados no LittleFS
  profiles.*       perfis (JSON montado pela página) no LittleFS
  transport_ble.*  Web Bluetooth (NimBLE)
  transport_wifi.* rede Wi-Fi própria + servidor da página
Receptor/tools/    embed_web.py (embute a página no firmware)
docs/index.html    página do celular
```

### Protocolo com o celular

Mensagens JSON, uma por linha. Pelo BLE, a página escreve na característica `…0002` e recebe notificações em `…0003`. Pelo Wi-Fi, a página faz `POST /api`.

- Página → receptor: `{"c":"hello"}`, `{"c":"learn","key":"pab12-power"}`, `{"c":"send","key":"pab12-power","raw":false}`, `{"c":"saveProfile","id":"pab12","data":{...}}`, `{"c":"delProfile","id":"pab12"}`, `{"c":"del"|"rename"|"ping"|"unpair"|"profiles"|"list"|"learnCancel", ...}`
- Receptor → página: `{"t":"status"|"signals"|"profile"|"profilesDone"|"profileSaved"|"profileDeleted"|"learned"|"learnTimeout"|"sent"|"ir"|"error", ...}`
- Os sinais de um perfil usam a chave `<id do perfil>-<comando>`. Apagar o perfil apaga esses sinais.
