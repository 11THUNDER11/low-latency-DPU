# 🚀 Low-Latency DPU Lab

A comprehensive development environment for **NVIDIA BlueField-3**, optimized for hardware-accelerated networking via DOCA.

## 🎯 Project Vision

This lab focuses on offloading the network stack from the Host to the DPU-3 hardware. By using **DOCA Flow**, we aim to achieve nanosecond-level determinism for market data processing.

---

## 🔐 Access & Connectivity

### Remote Access (Jump Host)

To connect to the management environment, use the following SSH command:

```bash
ssh acatellani@<IP_DEL_SERVER>

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

### Setting up the Network

Communication between the **Host (x86)** and the **DPU (ARM)** is established via a virtual network interface tunneled through the PCIe bus and managed by the `rshim` driver. 

It exposes the DPU’s internal registers as system files and enables the `tmfifo_net0` virtual network interface.

```bash
# Check if the rshim service is installed and active
sudo systemctl status rshim

# If not installed, proceed with the installation
sudo apt install rshim -y

# Enable and start the service at boot
sudo systemctl enable --now rshim

# Verify that the DPU is correctly detected (it should appear as /dev/rshim0)
ls /dev/rshim*
```

Once `rshim` is active, the Host will detect a new network interface (typically named `tmfifo_net0` or similar). A static IP must be assigned to this interface to communicate with the DPU's internal management bridge.

```bash
# Identify the exact name of the tmfifo interface
ip link show | grep tmfifo

# Assign the IP to the interface (Host side)
# Note: The DPU default internal IP is 192.168.100.2, so we use .1 for the Host
sudo ip addr add 192.168.100.1/24 dev tmfifo_net0
sudo ip link set tmfifo_net0 up

# Verify connectivity to the ARM Cores
ping -c 3 192.168.100.2
```

If the ping works, you can access the DPU-3 ARM processors directly:

```bash
ssh -i DPUKeys ubuntu@192.168.100.2

```
---

### Kernel Details

* **Architecture:** x86_64 (Host) / ARM64 (DPU)
* **Kernel:** 6.8.0-90-generic (**PREEMPT_DYNAMIC** enabled for low latency)

---

## 📉 Experiments list

0. **EXP00: Exploration** - Commenting an explainig `flow_drop` sample  compilation and linking.
1. **EXP01: Datapath Init** - Commenting the code of `flow_drop` sample.
2. **EXP02: Packet Steering** - Creating `flow_count`for filtering UDP and TCP packets in hardware.

---

## 📚 Resources

* [DOCA Flow Guide](https://docs.nvidia.com/doca/sdk/doca-flow/index.html)
* [DOCA Core Guide](https://docs.nvidia.com/doca/sdk/doca-core/index.html)

**Project Status:** Active - Infrastructure Validated ✅

**Last Updated:** March 2026
