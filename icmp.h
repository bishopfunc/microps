#ifndef ICMP_H
#define ICMP_H
#define ICMP_TYPE_ECHO  8


extern int
icmp_output(uint8_t type, uint8_t code, uint32_t values, const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst);

extern int
icmp_init(void);

#endif