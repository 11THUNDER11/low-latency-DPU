# EXP06: Biridectional sample

## Architecture & Flow Diagrams

### 1. High-Level System Architecture
This diagram illustrates the separation between the **Hardware Fast Path** (NVIDIA ConnectX eSwitch) and the **Software Slow Path** running on the ARM CPU cores.

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



```mermaid
flowchart TB
    subgraph DPU_ARM ["1. DPU ARM Subsystem (dpu-3-os User Space)"]
        APP["flow_count Application<br/><i>(DPDK / DOCA Control Plane)</i>"]
        
        subgraph DPDK_PORTS ["DPDK Ethdev Layer"]
            DPDK_P0["DPDK Port 0<br/><code>0000:03:00.0</code><br/><i>(Primary PF / Switch Master)</i>"]
            DPDK_REP0["DPDK Port 1<br/><code>0000:03:00.0_representor_0</code><br/><i>(Representor 0)</i>"]
            DPDK_REP1["DPDK Port 2<br/><code>0000:03:00.0_representor_1</code><br/><i>(Representor 1)</i>"]
        end
    end

    subgraph PCIE ["2. PCIe Subsystem"]
        PCI_DEV["PCI Endpoint<br/><code>0000:03:00.0</code><br/><i>(NVIDIA ConnectX PF0)</i>"]
    end

    subgraph ESWITCH ["3. Hardware eSwitch Engine (ConnectX Silicon)"]
        direction TB
        FLOW_ENGINE["eSwitch Flow Steering Engine<br/><i>(DOCA Flow Hardware Tables)</i>"]
        
        subgraph VPORTS ["eSwitch Representor Endpoints"]
            REP0_HW["vport / Rep 0<br/><i>(Uplink 0 Representor)</i>"]
            REP1_HW["vport / Rep 1<br/><i>(Host PF / Uplink 1 Representor)</i>"]
        end
        
        subgraph PHYSICAL ["Physical & Host Interfaces"]
            PHY_PORT0["Physical Port 0 (p0)<br/><i>(Wire / Network)</i>"]
            PHY_PORT1["Host PF / Physical Port 1 (p1)<br/><i>(Host / Wire)</i>"]
        end
    end

    %% DPDK App Connections
    APP <--> DPDK_P0
    APP <--> DPDK_REP0
    APP <--> DPDK_REP1

    %% DPDK to PCI Device Binding
    DPDK_P0 <--> PCI_DEV
    DPDK_REP0 <--> PCI_DEV
    DPDK_REP1 <--> PCI_DEV

    %% PCI to Hardware eSwitch
    PCI_DEV <--> FLOW_ENGINE

    %% eSwitch internal paths
    FLOW_ENGINE <--> REP0_HW
    FLOW_ENGINE <--> REP1_HW

    REP0_HW <--> PHY_PORT0
    REP1_HW <--> PHY_PORT1

    %% Fast Path Bypass
    PHY_PORT0 <== "Hardware Fast Path Offload (Zero CPU)" ==> FLOW_ENGINE
    FLOW_ENGINE <== "Hardware Fast Path Offload (Zero CPU)" ==> PHY_PORT1

```