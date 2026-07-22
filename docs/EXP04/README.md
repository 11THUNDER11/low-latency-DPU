# EXP04 — Industrial example

```
DPU-2-os (Exchange)     DPU-3-os (Trading Engine)
p1: 192.168.58.201 ──► p1: 03:00.1
                         DOCA Flow + DPI ARM
```

## Architettura delle pipe

```
         p1 (ingresso cavo fisico)
                  │
     ┌────────────▼────────────┐
     │      TRADING_PIPE       │  CONTROL, root=true
     │                         │
     │  prio=1: flow#0 TCP     │──► counter HW ──► RSS coda 0
     │  prio=2: flow#1 UDP     │──► counter HW ──► RSS coda 0
     │  prio=3: flow#2 UDP     │──► counter HW ──► RSS coda 0
     │  prio=N+1: catch-all    │──► DROP
     └─────────────────────────┘
                  │
             RSS coda 0
                  │
     ┌────────────▼────────────┐
     │    CPU ARM — DPI Worker  │
     │                         │
     │  Tag 35 → MsgType       │
     │  Tag 55 → Ticker        │
     │  Tag 54 → Side (B/S)    │
     │  Tag 38 → Qty           │
     │  Tag 44 → Price         │
     └─────────────────────────┘
```

## Configurazione flussi (`flows.conf`)

```
# src_ip dst_ip src_port dst_port proto
192.168.58.201 192.168.58.200 1234 5678 tcp
192.168.58.201 192.168.58.200 1234 5678 udp
192.168.58.201 192.168.58.200 1234 9999 udp
```

## File sorgenti

| File                | Ruolo                                               |
|---------------------|-----------------------------------------------------|
| `trading_main.c`    | Init DPDK (1 porta), parsing `--config`, cleanup   |
| `trading_sample.c`  | Crea TRADING_PIPE da `flows.conf`, avvia worker    |
| `trading_worker.c`  | DPI FIX Protocol: `decode_fix()`, `identify_flow()`|
| `trading_worker.h`  | Header worker                                      |
| `trading_config.c`  | Parser `flows.conf` → `flow_config_t`              |
| `trading_config.h`  | Struct `flow_key_t`, `flow_config_t`               |
| `send_fix_traffic.py` | Generatore traffico FIX su DPU-2-os             |

## Lancio

```bash
# DPU-3-os
cd ~/doca_projects/flow_trading
meson setup build && ninja -C build
sudo ./build/doca_trading -a 0000:03:00.1,dv_flow_en=2 --config flows.conf

# DPU-2-os — genera traffico FIX
sudo python3 send_fix_traffic.py
```

## Output atteso

```
[DPI][flow#0] FIX NEW_ORDER        | TSLA   | SELL | qty=484  | px=56.56
[DPI][flow#1] FIX MARKET_DATA_SNAPSHOT | NVDA | ?   | qty=?   | px=?
```