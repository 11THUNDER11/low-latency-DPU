#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "trading_config.h"

int load_flow_config(const char *path, flow_config_t *cfg)
{
    FILE *f;
    char line[256];
    char src_ip_s[64], dst_ip_s[64], proto_s[8];
    int  src_port, dst_port;
    struct in_addr addr;

    if (!path || !cfg) return -1;

    cfg->count = 0;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[CONFIG] Impossibile aprire %s\n", path);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        /* Salta commenti e righe vuote */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if (cfg->count >= MAX_FLOWS) {
            fprintf(stderr, "[CONFIG] Limite MAX_FLOWS=%d raggiunto\n", MAX_FLOWS);
            break;
        }

        /* Parsing: src_ip dst_ip src_port dst_port proto */
        if (sscanf(line, "%63s %63s %d %d %7s",
                   src_ip_s, dst_ip_s,
                   &src_port, &dst_port,
                   proto_s) != 5) {
            fprintf(stderr, "[CONFIG] Riga non valida: %s", line);
            continue;
        }

        flow_key_t *k = &cfg->flows[cfg->count];

        /* Converte IP stringa → uint32 in network byte order */
        if (inet_aton(src_ip_s, &addr) == 0) {
            fprintf(stderr, "[CONFIG] IP sorgente non valido: %s\n", src_ip_s);
            continue;
        }
        k->src_ip = addr.s_addr;

        if (inet_aton(dst_ip_s, &addr) == 0) {
            fprintf(stderr, "[CONFIG] IP destinazione non valido: %s\n", dst_ip_s);
            continue;
        }
        k->dst_ip = addr.s_addr;

        /* Porte in network byte order */
        k->src_port = htons((uint16_t)src_port);
        k->dst_port = htons((uint16_t)dst_port);

        /* Protocollo */
        if (strcasecmp(proto_s, "tcp") == 0)
            k->proto = IPPROTO_TCP;
        else if (strcasecmp(proto_s, "udp") == 0)
            k->proto = IPPROTO_UDP;
        else {
            fprintf(stderr, "[CONFIG] Protocollo non valido: %s\n", proto_s);
            continue;
        }

        cfg->count++;
    }

    fclose(f);
    return (cfg->count > 0) ? 0 : -1;
}

void print_flow_config(const flow_config_t *cfg)
{
    char src_s[INET_ADDRSTRLEN], dst_s[INET_ADDRSTRLEN];
    struct in_addr a;

    printf("[CONFIG] %d flussi caricati:\n", cfg->count);
    for (int i = 0; i < cfg->count; i++) {
        const flow_key_t *k = &cfg->flows[i];
        a.s_addr = k->src_ip; inet_ntop(AF_INET, &a, src_s, sizeof(src_s));
        a.s_addr = k->dst_ip; inet_ntop(AF_INET, &a, dst_s, sizeof(dst_s));
        printf("  [%d] %s:%u -> %s:%u  proto=%s\n",
               i,
               src_s, ntohs(k->src_port),
               dst_s, ntohs(k->dst_port),
               (k->proto == IPPROTO_TCP) ? "TCP" : "UDP");
    }
}
