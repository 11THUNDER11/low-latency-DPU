from scapy.all import *

# Constants
dpu_3_mac = "c4:70:bd:86:b8:55"
target_ip = "192.168.1.1"
interface = "p1"

def send_perfect_packets(count):
    # 1. Build the packets
    # We use / and then call show2() or similar later to ensure fields are set
    udp_pkt = Ether(dst=dpu_3_mac)/IP(dst=target_ip)/UDP(sport=1234, dport=5678)
    tcp_pkt = Ether(dst=dpu_3_mac)/IP(dst=target_ip)/TCP(sport=1234, dport=5678, flags="S")

    # 2. Force Scapy to handle the checksums and lengths correctly for HWS
    # By using the 'raw' conversion and re-parsing, Scapy fills in all defaults
    udp_pkt = Ether(raw(udp_pkt))
    tcp_pkt = Ether(raw(tcp_pkt))

    print(f"Sending {count} UDP and {count} TCP packets...")

    # 3. Use 'realtime' sending to ensure the DPU doesn't drop due to burst
    sendp(udp_pkt, count=count, iface=interface, verbose=False)
    sendp(tcp_pkt, count=count, iface=interface, verbose=False)

if __name__ == "__main__":
    send_perfect_packets(500)
    print("Done.")