# 📄 EXP01: Datapath Initialization & Hardware Filtering

## 🎯 Experiment Objectives

The goal of this experiment is to initialize the **DOCA Flow** environment and validate the hardware datapath on the **BlueField-3 (DPU-3)**. This "bootstrap" phase is mandatory; it establishes the zero-latency silicon path required to intercept or steer financial market data.

1. **Hardware Offloading**: Program the ConnectX-7 eSwitch to identify and destroy irrelevant packets at wire speed (100Gbps+) before they consume CPU cycles.
2. **Jitter Elimination**: Move packet processing entirely from the ARM CPU to the NIC's **TCAM (Ternary Content-Addressable Memory)**.
3. **Bump-in-the-Wire Verification**: Ensure that market data "ticks" travel through a dedicated wire-to-wire silicon path, bypassing the Operating System and host memory entirely.

---

## 🧠 Application Anatomy

The application is split into two logical domains to maximize performance:

* **Control Plane (`flow_drop_main.c`)**: Runs on the ARM CPU. Handles the "slow path" (initialization, hugepage memory allocation, and hardware probing).
* **Data Plane (`flow_drop_sample.c`)**: Programs the hardware. Once rules are pushed, the software sleeps, and the silicon handles 100% of the traffic logic with **0% CPU usage**.

---

## 📂 Component Analysis

### 1. `flow_drop_main.c` (The Infrastructure)

This file serves as the **Environment Bootstrap**. It prepares the DPU’s memory and PCIe handles to manage the hardware.

| Function | Action | Significance |
| --- | --- | --- |
| `doca_argp_start()` | Triggers DPDK EAL initialization. | Reserves **Hugepages** and maps the DPU silicon to the app address space. |
| `dpdk_queues_and_ports_init()` | Establishes RX/TX software queues. | Opens the physical "Link" and readies the hardware rings. |
| `flow_drop()` | Transitions to datapath logic. | Hands over the initialized hardware handles to the steering engine. |

> **Note on Hugepages**: Hugepages (2MB/1GB) are essential for DPDK. They prevent "TLB misses" by providing large, continuous memory blocks, allowing the NIC to write data directly into RAM via DMA. *Note: In this specific hairpin experiment, packets bypass hugepages entirely, but the memory is still required to initialize the DPDK environment.*

---

### 2. `flow_drop_sample.c` (The Steering Brain)

This file contains the **Hardware Rules**. It creates a two-tier hardware pipeline to manage traffic flow automatically.

#### **A. What is a Pipe?**

In DOCA, a "Pipe" is a hardware template. It defines *what* fields the silicon should look at (the Match) and *what* it should do (the Action), but it doesn't do anything until you add an "Entry" (the specific rule) and "Process" it (flash it to the silicon).

#### **B. The Pipeline Architecture (Creation vs. Execution)**

A critical concept in DOCA is that **creation order is backwards from execution order**. We must build the destination before we build the front door.

1. **The Hairpin Pipe (Created 1st, Executed 2nd)**:
* **The Match**: Configured as a Wildcard (`memset` to 0). It matches *everything*.
* **The Action**: `port_id ^ 1`. It instantly forwards the packet out the *other* physical port (Wire-to-Wire). It acts as a "Catch-All" safety net for legitimate traffic.


2. **The Drop Pipe (Created 2nd, Executed 1st)**:
* **The Match**: The primary filter. It is configured as the **Root** table, meaning packets hit this first.
* **The Action**: If it matches specific "noise" (e.g., unwanted IPs), it destroys the packet. If it misses, it forwards the packet to the Hairpin Pipe.



#### **C. The Lifecycle of a Rule**

| Step | API Command | What it does |
| --- | --- | --- |
| **1. Template** | `doca_flow_pipe_create()` | Tells the silicon: *"Prepare a table to match these types of fields."* |
| **2. Rule** | `doca_flow_pipe_add_entry()` | Queues the specific target (e.g., Drop IP `8.8.8.8`). |
| **3. Commit** | `doca_flow_entries_process()` | Flushes the software queue and permanently flashes the rules into the physical eSwitch. |

---

## 📉 Telemetry & Validation

Because packets are moving Wire-to-Wire and never touching the Operating System, standard Linux tools like `tcpdump` or `Wireshark` will see nothing.

To monitor performance, we use **Hardware Counters**:

* **`doca_flow_query_entry()`**: The CPU wakes up and queries the silicon directly for `total_bytes` and `total_pkts`.
* **Effect**: You can monitor your market data drop rates with **zero impact** on the packet's wire speed.

---

## 🛠️ Execution Quickstart

```bash
# 1. Allocate Hugepages
sudo bash -c "echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"

# 2. Build the experiment
meson setup build && ninja -C build

# 3. Run the Hardware Firewall
# -a flags specify the PCIe addresses and enable Hardware Steering (dv_flow_en=2)
sudo -E ./build/doca_flow_drop -a 0000:03:00.0,dv_flow_en=2 -a 0000:03:00.1,dv_flow_en=2 --

```