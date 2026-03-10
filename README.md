# 🚀 Low-Latency DPU Trading Lab (DPU-3 Focus)

A comprehensive development environment for **NVIDIA BlueField-3 (DPU-3)**, optimized for high-frequency trading (HFT) and hardware-accelerated networking via DOCA.

## 🎯 Project Vision

This lab focuses on offloading the network stack from the Host to the DPU-3 hardware. By using **DOCA Flow** and **Switchdev Mode**, we aim to achieve nanosecond-level determinism for market data processing.

---

## 🔐 Access & Connectivity

### Remote Access (Jump Host)

To connect to the management environment, use the following SSH command:

```bash
ssh acatellani@<IP_DEL_SERVER>

```

### DPU Internal Access (ARM Cores)

Once inside the host, you can access the DPU-3 ARM processors directly:

```bash
ssh -i DPUKeys ubuntu@192.168.100.2

```

---

## 🛠️ DOCA Installation & Setup

To develop and run acceleration programs, the DOCA SDK must be synchronized between the Host and the DPU.

### 1. Repository Setup (Host & DPU)

Download the local repository installer from the NVIDIA Networking portal:

```bash
# Download the DOCA repo packet for Ubuntu 24.04
wget https://developer.download.nvidia.com/networking/bundle/doca/2.9.0/doca-repo-ubuntu2404_2.9.0-1_amd64.deb

# Install the repository
sudo dpkg -i doca-repo-ubuntu2404_2.9.0-1_amd64.deb
sudo apt update

```

### 2. SDK Installation

Install the complete DOCA stack to enable all development libraries:

```bash
sudo apt install doca-all

```

### 3. Verification

Verify that the DOCA drivers and tools are correctly linked:

```bash
doca_version
# Should return: DOCA_VERSION: 3.2.1-044000 (or similar)

```

---

## 🖥️ DPU Current State & Datapath

The DPU-3 is currently configured in **Switchdev Mode** with a dual-bridge architecture, ideal for separating Market Data (Feed) from Order Entry (Execution).

### OVS Mapping (Live Status)

Current logical topology identified via `ovs-vsctl show`:

| Bridge | Port | Interface | Role |
| --- | --- | --- | --- |
| **ovsbr1** | `p0` | Uplink 0 | **Market Data Ingress** (External) |
|  | `pf0hpf` | Representor | **Host Path** (To x86 CPU) |
|  | `en3f0...` | SF | **DPU Local Path** (To ARM Cores) |
| **ovsbr2** | `p1` | Uplink 1 | **Order Execution** (To Broker) |
|  | `pf1hpf` | Representor | **Host Path** (To x86 CPU) |

### Kernel Details

* **Architecture:** x86_64 (Host) / ARM64 (DPU)
* **Kernel:** 6.8.0-90-generic (**PREEMPT_DYNAMIC** enabled for low latency)

---

## 📉 Trading Strategy Roadmap

1. **EXP01: Datapath Init** - Port mapping and DOCA Flow environment boot.
2. **EXP02: Packet Steering** - Filtering UDP Multicast ticks in hardware.
3. **EXP03: Zero-Copy Delivery** - Moving data to app memory via `doca_mmap`.

---

## 📚 Resources

* [DOCA Flow Guide](https://docs.nvidia.com/doca/sdk/doca-flow/index.html)
* [DOCA Core Guide](https://docs.nvidia.com/doca/sdk/doca-core/index.html)

**Project Status:** Active - Infrastructure Validated ✅

**Last Updated:** March 2026
