# EXP02 — Packet Counting via DOCA Flow (BlueField-3)

This sample demonstrates how to leverage NVIDIA DOCA Flow on a BlueField-3 DPU to count TCP, UDP, and other protocol packets directly in hardware using the `vnf,hws` (Hardware Steering) mode.

---

## Physical Setup

```mermaid
graph LR
    DPU2["DPU-2 (Traffic Gen)\n192.168.58.201"]
    DPU3["DPU-3 (Monitor)\n192.168.58.200\n← Sample runs here"]
    DPU2 -->|"p1 ↔ p1\nDirect 100G Cable"| DPU3
```

**Crucial Note:** Traffic must enter through the **physical cable on `p1`**, not via PCIe from the host. If traffic originates from the host, OVS or the Linux kernel might intercept it before it reaches the DOCA Flow hardware pipeline.

---

## How it Works

```mermaid
graph TD
    P["p1 — Packet Ingress"]
    CP["CONTROL_PIPE\nroot=true"]
    T["Priority 1\nIPv4 next_proto=6"]
    U["Priority 2\nIPv4 next_proto=17"]
    O["Priority 3\nCatch-all"]

    P --> CP --> T
    T -->|no match| U
    U -->|no match| O

    T --> CT["Hardware Counter: TCP"] --> D1[DROP]
    U --> CU["Hardware Counter: UDP"] --> D2[DROP]
    O --> CO["Hardware Counter: OTHER"] --> D3[DROP]
```

Every packet is compared against entries in order of priority. Upon the first match: the hardware counter increments, and the packet is dropped. The entire process occurs in silicon, involving zero CPU cycles for forwarding.

### Packet Walkthrough (TCP Example)

```mermaid
sequenceDiagram
    participant DPU2 as Scapy on DPU-2
    participant Cable as Physical Cable
    participant HW as ConnectX-7 HW
    participant App as ARM CPU (every 5s)

    DPU2->>Cable: Ether/IP/TCP
    Cable->>HW: Frame arrives on p1
    HW->>HW: match next_proto=6 → counter++
    HW->>HW: Action: DROP
    App->>HW: doca_flow_query_entry()
    HW-->>App: total_pkts (Hardware stats)
```

---

## Build and Execution

### 1. Compilation
On **DPU-3**, use `meson` and `ninja` to build the application:
```bash
cd /opt/mellanox/doca/samples/doca_flow/flow_count
meson setup build
ninja -C build
```

### 2. Execution (DPU-3 Monitor)
Stop OVS to prevent resource conflicts and launch the sample. The `-a` flag specifies the PCI address for `p1`, and `dv_flow_en=2` enables the Hardware Steering engine.
```bash
sudo systemctl stop openvswitch-switch
sudo ./build/doca_flow_count -a 0000:03:00.1,dv_flow_en=2
```

### 3. Traffic Generation (DPU-2)
Generate traffic from **DPU-2** directly through the `p1` interface:
```python
from scapy.all import *

# Destination MAC must be p1 of DPU-3
dst_mac = "c4:70:bd:86:b8:55"

# Send 500 TCP and 2000 UDP packets
sendp([Ether(dst=dst_mac)/IP(dst="192.168.58.200")/TCP(dport=80)]*500, iface="p1", verbose=False)
sendp([Ether(dst=dst_mac)/IP(dst="192.168.58.200")/UDP(dport=5000)]*2000, iface="p1", verbose=False)
```

**Expected Output:**
```text
[11:05:10][DOCA][INF] Successfully initialized DOCA Flow with HWS
[11:05:15][DOCA][INF] [STATS] TCP: 0    | UDP: 0    | OTHER: 0
[11:05:20][DOCA][INF] [STATS] TCP: 500  | UDP: 2000 | OTHER: 1
[11:05:25][DOCA][INF] [STATS] TCP: 500  | UDP: 2000 | OTHER: 3
```

---

## Project Structure

| File | Responsibility |
|---|---|
| `flow_count_main.c` | DPDK initialization, argument parsing, and resource cleanup. |
| `flow_count_sample.c` | Core logic: Pipe creation, entry installation, and the counter query loop. |
| `flow_common.c/h` | Shared DOCA Flow utilities for port and resource initialization. |
| `meson.build` | Meson build system configuration. |