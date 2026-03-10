# EXP0: Application Folder Structure & File Relationships

This document outlines the architecture of a standard DOCA Flow application on the BlueField-3 DPU. Understanding this structure is essential as it dictates how hardware initialization, packet routing logic, and build dependencies are connected.

## 📂 Directory Layout (Baseline: `flow_drop`)

```text
/opt/mellanox/doca/samples/doca_flow/flow_drop
├── meson.build           # Build instructions & dependency mapping (DOCA/DPDK)
├── flow_drop_main.c      # Infrastructure: Hugepages, ARGP, and Hardware Probing
├── flow_drop_sample.c    # The Brain: Defines the hardware "Drop" rules
└── flow_drop_sample.yaml # The Manifest: Documentation on how to run & test

```

---

## 🔗 The Core Components & Their Relationships

### 1. `meson.build` (The Blueprint)

* **Role:** Replaces Makefiles. It defines the project name, version, and links the high-performance hardware engines (`doca-flow`, `doca-common`, and `libdpdk`).
* **Key Fix:** In standalone folders, the `version` must be hardcoded (e.g., `'2.7.0085'`) because it can no longer find the parent `../../../VERSION` file.

### 2. `..._main.c` (The Infrastructure)

* **Role:** This is the entry point. It handles the "heavy lifting" of the OS:
* **DPDK EAL:** Initializes the Environment Abstraction Layer.
* **Hugepages:** Verifies memory is available for zero-copy packet processing.
* **ARGP:** Parses command-line flags (like `-a` for PCIe addresses).


* **Transition:** Once the silicon is probed, it hands off control to the `sample.c` logic.

### 3. `..._sample.c` (The Datapath Logic)

* **Role:** This is where you write your trading filter. It defines:
* **The Match:** (e.g., Destination IP `239.1.1.1` and UDP Port `12345`).
* **The Action:** (e.g., `DOCA_FLOW_ACTION_DROP` to kill noise or `FORWARD` for trade data).


* **Hardware Offload:** Once these rules are called, they are programmed into the ConnectX-7 eSwitch. The CPU is no longer involved in the decision-making for those packets.

### 4. `..._sample.yaml` (The Validation Manifest)

* **Role:** A blueprint for execution. It lists the exact parameters needed to run the sample successfully.
* **Trading Context:** We use this to document which multicast groups our application is designed to filter, ensuring that testers know which traffic should trigger the hardware counters.

---

## 🛠️ General Build & Execution Guide

To move from source code to hardware-accelerated execution, follow these three stages.

### Stage 1: The Environment Setup

You must tell the build system where the Mellanox-specific "maps" (pkg-config files) are located.

```bash
export PKG_CONFIG_PATH=/opt/mellanox/doca/lib/aarch64-linux-gnu/pkgconfig:/opt/mellanox/dpdk/lib/aarch64-linux-gnu/pkgconfig

```

### Stage 2: Compilation

Use `meson` to configure and `ninja` to compile.

```bash
# Clear previous attempts
sudo rm -rf build/

# Setup the build directory
sudo -E meson setup build

# Compile the binary
sudo -E ninja -C build

```

### Stage 3: Execution (The Hardware Run)

Always run with the `dv_flow_en=2` flag to enable the Hardware Steering engine.

```bash
# Allocate Hugepages first
sudo bash -c "echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"

# Execute the application
# Replace 0000:03:00.0 with your actual PCIe address from 'lspci' | grep Mellanox
sudo -E ./build/doca_flow_drop -a 0000:03:00.0,dv_flow_en=2 -a 0000:03:00.1,dv_flow_en=2 --

```
