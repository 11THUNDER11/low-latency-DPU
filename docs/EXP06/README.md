# EXP06: Biridectional sample

## Architecture & Flow Diagrams

### 1. High-Level System Architecture
Thi s diagram illustrates the separation between the **Hardware Fast Path** (NVIDIA ConnectX eSwitch) and the **Software Slow Path** running on the ARM CPU cores.

```mermaid
flowchart TB
    subgraph HW ["NVIDIA BlueField DPU (Hardware Fast Path)"]
        P0["Ingress Port (PF0)"]
        P1["Egress Port (PF1)"]
        ESW["NVIDIA ConnectX eSwitch / Hardware Flow Engine"]
    end

    subgraph SW ["ARM Host / CPU (Software Slow Path)"]
        DPDK_RX["DPDK RX Burst (Queue 0)"]
        FLOW_MGMT["Flow Management & Lookup Table"]
        DOCA_API["DOCA Flow API (doca_flow_pipe_add_entry)"]
        DPDK_TX["DPDK TX Burst"]
    end

    P0 --> ESW
    ESW -- "Hit: Dynamic 5-Tuple Rule" --> P1
    ESW -- "Miss: RSS Punt" --> DPDK_RX
    DPDK_RX --> FLOW_MGMT
    FLOW_MGMT --> DOCA_API
    DOCA_API -- "Offload HW Rule" --> ESW
    FLOW_MGMT --> DPDK_TX
    DPDK_TX --> P1

```

---

### 2. DOCA Flow Hardware Pipeline Topology

This flowchart details the structural topology of the DOCA Flow pipes created during initialization (`ROOT_PIPE_P0`, protocol-specific 5-tuple pipes, and the RSS fallback pipe).

```mermaid
flowchart TD
    subgraph INGRESS ["1. Ingress Traffic"]
        PKT["Incoming Packet"]
    end

    subgraph PIPELINE ["2. DOCA Flow Hardware Pipeline"]
        ROOT["ROOT_PIPE_P0<br/>(Control Pipe)<br/><i>Matches L3/L4 Protocol</i>"]
        
        TCP_P["TCP_PIPE_P0<br/>(Basic Pipe + Counters)<br/><i>Matches 5-Tuple TCP</i>"]
        UDP_P["UDP_PIPE_P0<br/>(Basic Pipe + Counters)<br/><i>Matches 5-Tuple UDP</i>"]
        
        RSS_P["RSS_MISS_P0<br/>(Basic Pipe)<br/><i>Catch-All / Miss Handler</i>"]
        
        DROP["Hardware Drop"]
    end

    subgraph DESTINATIONS ["3. Egress & Slow Path"]
        PORT_FWD["Hardware Wire-Speed Forwarding<br/>(DOCA_FLOW_FWD_PORT)"]
        SW_PUNT["CPU RSS Queue 0<br/>(DOCA_FLOW_FWD_RSS)"]
    end

    PKT --> ROOT
    ROOT -- "IPv4 + TCP" --> TCP_P
    ROOT -- "IPv4 + UDP" --> UDP_P
    ROOT -- "Default / Non-IP" --> DROP

    TCP_P -- "5-Tuple Hit" --> PORT_FWD
    UDP_P -- "5-Tuple Hit" --> PORT_FWD

    TCP_P -- "Miss" --> RSS_P
    UDP_P -- "Miss" --> RSS_P

    RSS_P --> SW_PUNT

```

---

### 3. Packet Lifecycle & Offload Sequence

This sequence diagram shows the step-by-step execution path for a flow's **first packet** versus all **subsequent packets**.

```mermaid
sequenceDiagram
    autonumber
    actor Traffic as Traffic Generator / Network
    participant HW as DOCA Flow HW (eSwitch)
    participant SW as CPU / DPDK Slow Path
    participant Egress as Target Egress Port

    Note over Traffic, Egress: Phase 1: First Packet (Slow Path & Rule Offload)
    Traffic->>HW: 1. Send Packet (New 5-Tuple Flow)
    HW->>HW: 2. Lookup in TCP/UDP Pipe (MISS)
    HW->>SW: 3. Forward via RSS_MISS_P0 to CPU Queue 0
    SW->>SW: 4. process_packet() (Record flow in table)
    SW->>HW: 5. doca_flow_pipe_add_entry() (Install 5-Tuple HW Rule)
    SW->>Egress: 6. rte_eth_tx_burst() (Forward 1st Packet)

    Note over Traffic, Egress: Phase 2: Subsequent Packets (Hardware Fast Path)
    Traffic->>HW: 7. Send Next Packets (Same 5-Tuple Flow)
    HW->>HW: 8. Lookup in TCP/UDP Pipe (HIT)
    HW->>Egress: 9. Direct HW Port Forwarding (Bypasses CPU entirely)

```



---

## Pipe Specifications

| Pipe Name | Type | Match Fields | Action on Hit | Action on Miss |
| --- | --- | --- | --- | --- |
| **`ROOT_PIPE_P0`** | `CONTROL` | L3 Protocol (IPv4), L4 Protocol | Jump to `TCP_PIPE_P0` / `UDP_PIPE_P0` | Drop non-IP traffic |
| **`TCP_PIPE_P0`** | `BASIC` | 5-Tuple (Src/Dst IP, Src/Dst Port, Proto) | Forward to Egress Port (`FWD_PORT`) | Forward to `RSS_MISS_P0` |
| **`UDP_PIPE_P0`** | `BASIC` | 5-Tuple (Src/Dst IP, Src/Dst Port, Proto) | Forward to Egress Port (`FWD_PORT`) | Forward to `RSS_MISS_P0` |
| **`RSS_MISS_P0`** | `BASIC` | Empty Match (Catch-All) | Forward to CPU Queue 0 (`FWD_RSS`) | N/A |

---

## Building and Running

### Prerequisites

* NVIDIA DOCA SDK installed on DPU
* DPDK environment configured with hugepages enabled
* BlueField DPU running in Switch Devlink mode

### Build

```bash
meson build
ninja -C build
sudo ./build/doca_flow_count -a 0000:03:00.1,dv_flow_en=2,representor=[65535]
```
