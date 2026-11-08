# Guia de hardware

Montagem do **Receptor (ESP32 DevKit V1 / ESP-WROOM-32)** e do **Emissor (ESP-01)**.

## Lista de materiais

| Qtd | Item | Observação |
|---|---|---|
| 1 | ESP32 DevKit V1 (ESP-WROOM-32, 30 ou 38 pinos) | Receptor |
| 1 | ESP-01 ou ESP-01S (1MB) | Emissor |
| 1 | Receptor IR 38 kHz: VS1838B, TSOP38238 ou módulo KY-022 | Lê o controle original |
| 1 a 2 | LED infravermelho 940 nm, 5 mm (ex.: TSAL6200, IR333) | Use 2 para mais alcance |
| 2 | Botão táctil (push button) | GRAVAR e ENVIAR |
| 1 | Transistor NPN: 2N2222, BC337 ou S8050 (ou PNP BC557, ver abaixo) | Driver do LED IR |
| 1 | Resistor 1 kΩ | Base do transistor |
| 1 | Resistor 10 kΩ | Base → GND (mantém o LED desligado no boot) |
| 1 | Resistor do LED IR: 22 Ω, 33 Ω ou 39 Ω (ver tabela) | 1/4 W |
| 1 | Resistor 100 Ω | Filtro do receptor IR (opcional, recomendado) |
| 1 | Capacitor 10 µF (eletrolítico) + 1 de 100 nF (cerâmico) | Filtro do receptor IR |
| 1 | Regulador 3,3 V (AMS1117-3.3) ou adaptador ESP-01 com regulador | Alimentação do ESP-01 |
| 2 | Capacitor 10 µF + 1 de 100 nF + 1 de 470 µF | Regulador e ESP-01 |
| 2 a 4 | Resistor 10 kΩ | Pull-ups do ESP-01 (dispensáveis no ESP-01S ou em adaptador) |
| 1 | Conversor USB-serial 3,3 V (CH340/FT232/CP2102) ou gravador ESP-01 | Gravar o ESP-01 |

---

## 1. Receptor: ESP32 DevKit V1

### Ligações

| Componente | Pino do componente | Pino do ESP32 |
|---|---|---|
| Receptor IR | OUT (sinal) | **GPIO27** (D27) |
| Receptor IR | VCC | **3V3** (via 100 Ω, ver filtro) |
| Receptor IR | GND | **GND** |
| Botão GRAVAR | um terminal | **GPIO25** (D25) |
| Botão GRAVAR | outro terminal | **GND** |
| Botão ENVIAR | um terminal | **GPIO26** (D26) |
| Botão ENVIAR | outro terminal | **GND** |
| LED de status | já vem na placa (LED azul) | GPIO2 |
| Alimentação | cabo USB | conector micro-USB / USB-C |

```
                ESP32 DevKit V1
             ┌──────────────────┐
             │ 3V3          VIN │
             │ GND          GND ├──────────────┬───────────┬──────────┐
             │ D15          D13 │              │           │          │
             │ D2 (LED)     D12 │           [GRAVAR]   [ENVIAR]       │
             │ D4           D14 │              │           │          │
             │ ...          D27 ├───────┐      │           │          │
             │              D26 ├───────┼──────┼───────────┘          │
             │              D25 ├───────┼──────┘                      │
             └──────────────────┘       │                             │
                                        │ OUT                         │
   3V3 ──[100 Ω]──┬──────────── VCC  ┌──┴──┐                          │
                  │                  │ IR  │  VS1838B                 │
               10µF + 100nF          │ RX  │                          │
                  │                  └──┬──┘                          │
   GND ───────────┴──────────── GND ────┴─────────────────────────────┘
```

### Resistores e capacitores

- **Botões: não precisam de resistor.** O firmware liga o pull-up interno (`INPUT_PULLUP`). O botão só fecha o pino com o GND.
  - Opcional: um capacitor de 100 nF em paralelo com cada botão reduz o *bounce*. O software já faz debounce de 30 ms.
- **Receptor IR: ligue no 3V3, nunca no 5V.** A saída do VS1838B vai até o VCC, e 5 V na entrada do ESP32 pode danificá-lo.
  - Filtro recomendado pelo fabricante: **100 Ω em série no VCC**, mais **10 µF + 100 nF** entre o VCC do sensor e o GND, bem perto do sensor. Ele evita leituras falsas causadas pelo ruído do Wi-Fi/BLE. Sem o filtro também funciona, só fica um pouco menos imune a ruído.
  - O pino OUT vai **direto** no GPIO27, sem resistor.
- **LED de status:** use o LED azul da placa (GPIO2). Se quiser um LED externo no painel: GPIO2 → **330 Ω** → LED (anodo) → catodo → GND.

### Pinagem do receptor IR

| Modelo | Olhando de frente (lado da "bolinha") |
|---|---|
| VS1838B / TSOP38238 | 1 = **OUT**, 2 = **GND**, 3 = **VCC** |
| Módulo KY-022 | siga a serigrafia: **S** = OUT, **+** = VCC, **−** = GND (o "+" geralmente fica no meio) |

> Confira o datasheet do seu sensor. Inverter VCC e GND queima o sensor.

### Pinos que NÃO devem ser usados no DevKit V1

| Pinos | Motivo |
|---|---|
| GPIO6 a GPIO11 | ligados à memória flash interna |
| GPIO0, GPIO2, GPIO12, GPIO15 | *strapping pins*: alteram o modo de boot (o **GPIO12** em nível alto no boot impede a placa de iniciar) |
| GPIO1, GPIO3 | Serial/USB (monitor serial) |
| GPIO34 a GPIO39 | só entrada e **sem pull-up interno**: os botões não funcionariam sem resistor externo |

Para trocar de pino, edite `build_flags` do ambiente `esp32-devkitv1` em `Receptor/platformio.ini`.

---

## 2. Emissor: ESP-01

### Alimentação: ponto mais importante

- **O ESP-01 funciona apenas com 3,3 V. 5 V queima o módulo.**
- Em picos de Wi-Fi ele puxa **até ~300 mA**. O pino 3,3 V de conversores USB-serial baratos e de Arduinos não aguenta essa corrente, e o resultado são reinícios aleatórios.
- Solução: fonte 5 V (carregador USB) → **AMS1117-3.3** → ESP-01.
  - 10 µF na entrada e 10 µF + 100 nF na saída do regulador.
  - **470 µF** entre 3,3 V e GND, bem perto do ESP-01.
  - Ou use uma placa adaptadora "ESP-01 adapter", que já tem regulador.

### Pinagem do ESP-01 (vista de cima, antena para cima)

```
        ┌───────────────┐
        │  ~~ antena ~~ │
        │               │
  GND  ─┤ 1           2 ├─ GPIO1 (TX)
  GPIO2─┤ 3           4 ├─ EN (CH_PD)
  GPIO0─┤ 5           6 ├─ RST
  RX/GPIO3─┤ 7        8 ├─ VCC 3,3 V
        └───────────────┘
```
(A ordem pode variar conforme o fabricante. Confira a serigrafia do seu módulo.)

### Ligações de boot (pull-ups)

| Pino | Ligação | Por quê |
|---|---|---|
| VCC | 3,3 V | alimentação |
| GND | GND | — |
| EN (CH_PD) | **10 kΩ → 3,3 V** | habilita o chip (sem isso ele fica desligado) |
| RST | **10 kΩ → 3,3 V** | evita resets |
| GPIO0 | **10 kΩ → 3,3 V** | boot normal. **Ligue no GND só para gravar** |
| GPIO2 | **10 kΩ → 3,3 V** | precisa estar em nível alto no boot |
| GPIO3 (RX) | circuito do LED IR | saída do sinal IR |

> **ESP-01S** e placas adaptadoras normalmente já têm esses pull-ups. Nesse caso basta ligar VCC, GND e GPIO3.

### Circuito do LED IR com transistor (recomendado)

Ligado direto no pino, o ESP-01 fornece ~12 mA, e o LED alcança só 1 a 2 m. Com o transistor, o LED recebe ~80–100 mA e alcança 6–8 m.

```
   VLED (3,3 V ou 5 V)
        │
       [R_LED]   ← ver tabela
        │
       ─┴─  LED IR (anodo em cima; catodo = perna curta / lado chato)
       \ /
       ─┬─
        │
        C (coletor)
 GPIO3 ──[1 kΩ]──┬── B (base)    2N2222 / BC337 / S8050
                 │   E (emissor)
               [10 kΩ]  │
                 │      │
 GND ────────────┴──────┴──────── GND comum
```

- **1 kΩ na base:** limita a corrente do GPIO (~2,6 mA) e já satura o transistor para até ~250 mA.
- **10 kΩ base→GND:** garante o LED **apagado** enquanto o ESP-01 inicia ou é gravado (o RX fica flutuando nesse momento).
- **Atenção à pinagem do transistor:** 2N2222 (TO-92, face chata para você) = E-B-C. **BC337** = C-B-E (invertido!). S8050 = E-B-C. Confira o datasheet.
- **BC547 / BC548 / BC549 também servem** (pinagem C-B-E). Eles aguentam só 100 mA, então use um resistor do LED maior que o da tabela: **33 Ω no 3,3 V** (~55 mA), **47 Ω no 5 V com 1 LED** (~75 mA) ou **33 Ω no 5 V com 2 LEDs em série** (~65 mA).

### Valor do resistor do LED (R_LED)

Conta: `R = (VLED − Vf_LED − Vce) / I`, com Vf ≈ 1,35 V (LED IR 940 nm), Vce ≈ 0,2 V e I ≈ 100 mA.

| Alimentação do LED | LEDs | Conta | **Resistor** | Corrente |
|---|---|---|---|---|
| 3,3 V | 1 | (3,3 − 1,35 − 0,2) / 0,1 = 17,5 Ω | **22 Ω** | ~80 mA |
| 5 V | 1 | (5 − 1,35 − 0,2) / 0,1 = 34,5 Ω | **33 Ω** (ou 39 Ω) | ~90–100 mA |
| 5 V | 2 em série | (5 − 2,7 − 0,2) / 0,1 = 21 Ω | **22 Ω** | ~95 mA, mais alcance |

- 1/4 W basta: o LED só pulsa (portadora de 38 kHz com ~33% de ciclo) durante o envio.
- Ao alimentar o LED com 5 V, o **GND do 5 V e o GND do ESP-01 precisam estar ligados**. O GPIO3 continua em 3,3 V, só o coletor fica no 5 V.
- **Se 5 V estiver disponível, prefira a opção de 5 V com 2 LEDs:** é a de maior alcance.

### Alternativa com transistor PNP (BC557, BC327, 2N3906)

O PNP fica do lado positivo e acende o LED com o pino em **0**. Por isso a lógica do firmware é invertida.

```
 3V3 ───────────┬──────────┐
                │          E (emissor)
             [10 kΩ]       │
                │     B ───┤  BC557
 GPIO3 ──[1 kΩ]─┴──────┘   │
                           C (coletor)
                           │
                       [R_LED 33 Ω]
                           │
                          ─┴─  LED IR (anodo em cima)
                          \ /
                          ─┬─
                           │
 GND ──────────────────────┴──── GND
```

- **No firmware**, em `Emissor/platformio.ini`, troque para `-DIR_LED_INVERTED=1`. Sem isso o LED fica aceso o tempo todo e o sinal sai invertido.
- **O emissor do BC557 vai no 3,3 V, nunca no 5 V.** O GPIO só chega a 3,3 V. Com o emissor no 5 V, o transistor nunca desliga e o LED fica sempre aceso.
- **10 kΩ base→emissor (3V3):** mantém o LED **apagado** enquanto o ESP-01 inicia.
- **R_LED = 33 Ω (~55 mA):** o BC557 aguenta só 100 mA. Não use 22 Ω com ele.
- **Pinagem do BC557** (TO-92, face chata para você): **C-B-E**.
- O alcance fica um pouco menor que o do NPN no 5 V, mas basta para um cômodo (~4–6 m).

### Alternativa sem transistor (só para teste)

```
GPIO3 ──[100 Ω]── LED IR (anodo → catodo) ── GND
```
Corrente ~15 mA, alcance de 1 a 2 m. O LED precisa ficar bem perto do ar-condicionado.

### Posicionamento

- Aponte o LED **diretamente** para o receptor do ar (normalmente atrás de uma janelinha escura no painel).
- O receptor IR do ESP32 pode ficar perto do emissor: o firmware ignora o próprio sinal por 1,5 s depois de cada envio.

---

## 3. Gravando o ESP-01

1. Use um **conversor USB-serial em 3,3 V** (jumper em 3,3 V) ou um gravador ESP-01 (com chave PROG/UART).
2. Ligações: adaptador **TX → RX (GPIO3)** do ESP-01, **RX → TX (GPIO1)**, GND → GND.
3. Coloque **GPIO0 no GND** e ligue (ou dê reset) no ESP-01: ele entra em modo de gravação.
4. `cd Emissor && pio run -t upload`
5. Solte o GPIO0 (volta para o pull-up de 10 kΩ) e reinicie.

> Como o LED usa o pino RX, se a gravação falhar, desconecte o resistor de 1 kΩ do GPIO3 durante a gravação.
> O monitor serial do emissor só funciona na direção de saída (TX). Ele mostra as mensagens "Emissor pronto", "AC enviado" etc.

---

## 4. Ordem de teste sugerida

1. Grave o Receptor e abra o monitor serial (`pio device monitor`). Deve aparecer `BLE anunciando como "ArCondicionado"`.
2. Aperte o botão GRAVAR e aponte um controle qualquer: o LED azul pisca rápido e o monitor mostra `Sinal "rapido" gravado`.
3. Grave e ligue o Emissor: no monitor do Receptor aparece `Emissor pareado: XX:XX:...`, e o LED azul fica aceso fixo.
4. Aperte ENVIAR: o LED IR deve piscar. Para ver, use a câmera do celular, que mostra a luz IR como um brilho roxo.
5. Abra a página no celular, conecte por BLE e teste pela aba CONTROLE.

Significado do LED azul (GPIO2):

| LED | Significado |
|---|---|
| aceso fixo | tudo ok, emissor online |
| piscada curta a cada 1 s | emissor offline / não pareado |
| piscando rápido | gravando (aguardando o controle) |
| aceso por 0,3 s | comando enviado com sucesso |
| piscando muito rápido | erro |
