#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "net.h"
#include "ip.h"
#include "platform.h"
#include <sys/types.h>
#include <string.h>

struct ip_hdr {
    uint8_t vhl;
    uint8_t tos;
    uint16_t total;
    uint16_t id;
    uint16_t offset; //flag(3) + flagment offset(13)
    uint8_t ttl;
    uint8_t protocol;
    uint16_t sum;
    ip_addr_t src;
    ip_addr_t dst;
    uint8_t options[];
};

const ip_addr_t IP_ADDR_ANY       = 0x00000000; /* 0.0.0.0 */
const ip_addr_t IP_ADDR_BROADCAST = 0xffffffff; /* 255.255.255.2 */

/* NOTE: if you want to add/delete the entries after net_run(), you need to protect these lists with a mutex. */
static struct ip_iface *ifaces;
// static struct ip_protocol *protocols;
// static struct ip_route *routes;

struct ip_iface *
ip_iface_alloc(const char *unicast, const char *netmask)
{
    struct ip_iface *iface;
    iface = memory_alloc(sizeof(*iface));
    if(!iface){
        errorf("memory_alloc() failure");
        return NULL;
    }
    NET_IFACE(iface)->family = NET_IFACE_FAMILY_IP; //iface->iface.family
    //#define NET_IFACE(x) ((struct net_iface *)(x))

    //Exercise 7-3: IPインタフェースにアドレス情報を設定
    if (ip_addr_pton(unicast, &iface->unicast) == -1) {
        errorf("ip_addr_pton() failure, addr=%s", unicast);
        memory_free(iface);
        return NULL;
    }
    if (ip_addr_pton(netmask, &iface->netmask) == -1) {
        errorf("ip_addr_pton() failure, addr=%s", netmask);
        memory_free(iface);
        return NULL;
    }
    iface->broadcast = (iface->unicast & iface->netmask) | ~iface->netmask;
    //ネットワーク部とnetmaskでAND
    //netmaskをビット反転し、↑とのOR
    return iface;
}

/* NOTE: must not be call after net_run() */
int
ip_iface_register(struct net_device *dev, struct ip_iface *iface)
{
    char addr1[IP_ADDR_STR_LEN]; 
    char addr2[IP_ADDR_STR_LEN]; 
    char addr3[IP_ADDR_STR_LEN];
    
    //Exercise 7-4: IPインタフェースの登録
    if (net_device_add_iface(dev, NET_IFACE(iface)) == -1) {
        errorf("net_device_add_iface() failure");
        return -1;
    }
    iface->next = ifaces;
    ifaces = iface;

    infof("registered: dev=%s, unicast=%s, netmask=%s, broadcast=%s", dev->name, ip_addr_ntop(iface->unicast, addr1, sizeof(addr1)), ip_addr_ntop(iface->netmask, addr2, sizeof(addr2)), ip_addr_ntop(iface->broadcast, addr3, sizeof(addr3)));
    return 0;
}

struct ip_iface *
ip_iface_select(ip_addr_t addr)
{
    // Exercise 7-5: IPインタフェースの検索
}

int 
ip_addr_pton(const char *p, ip_addr_t *n)
{
    char *sp, *ep; //?
    int idx; 
    long ret; //?

    sp = (char *)p;
    for(idx = 0; idx < 4; idx++){
        ret = strtol(sp, &ep, 10);
        // long strtol(const char *s, char **endp, int base);
        // 文字列をlong型に変換
        if(ret < 0 || ret > 255){
            return -1;
        }//0~255
        if(ep == sp){
            return -1;
        }
        if((idx == 3 && *ep != '\0') || (idx != 3 && *ep != '.')){
            return -1;
        }//なぜ3 2^3=8?
        //終端か.なら

        ((uint8_t *)n)[idx] = ret; //これはなに??
        sp = ep + 1; //end+1?必要??
    }
    return 0;
}

char *
ip_addr_ntop(ip_addr_t n, char *p, size_t size)
{
    uint8_t *u8;
    u8 = (u_int8_t *)&n; //cast
    snprintf(p, size, "%d.%d.%d.%d", u8[0], u8[1], u8[2], u8[3]);//pにフォーマットでコピーしてる //u8はどういう?

    return p;
}

static void
ip_dump(const uint8_t *data, size_t len)
{
    struct ip_hdr *hdr;
    uint8_t v, hl, hlen;
    uint16_t total, offset;
    char addr[IP_ADDR_STR_LEN];

    flockfile(stderr);
    // void flockfile(FILE *file);
    // ファイルのロック なぜ?
    hdr = (struct ip_hdr *)data;
    v = (hdr->vhl & 0xf0) >> 4; //0xf0上位ビットを取り出す, 4桁ずらす
    //0xf0 240?
    hl = (hdr->vhl & 0x0f); //下位ビットを取り出す、そのまま
    //0x0f 15?
    hlen = hl << 2; //2桁ずらすして、4倍にして、8bitにする

    fprintf(stderr, "        vhl: 0x%02x [v: %u, hl: %u (%u)]\n", hdr->vhl, v, hl, hlen);
    fprintf(stderr, "        tos: 0x%02x\n", hdr->tos);

    total = ntoh16(hdr->total); //バイトオーダー変換 16b
    fprintf(stderr, "      total: %u (payload: %u)\n", total, total - hlen);
    fprintf(stderr, "         id: %u\n", ntoh16(hdr->id));

    offset = ntoh16(hdr->offset);
    fprintf(stderr, "     offset: 0x%04x [flags=%x, offset=%u]\n", offset, (offset & 0xe000) >> 13, offset & 0x1fff);//offset & 0xe000, offset & 0x1fffがわからん
    fprintf(stderr, "        ttl: %u\n", hdr->ttl);
    fprintf(stderr, "   protocol: %u\n", hdr->protocol);
    fprintf(stderr, "        sum: 0x%04x\n", ntoh16(hdr->sum));
    fprintf(stderr, "        src: %s\n", ip_addr_ntop(hdr->src, addr, sizeof(addr))); //文字列に変換
    fprintf(stderr, "        dst: %s\n", ip_addr_ntop(hdr->dst, addr, sizeof(addr)));

#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif  
    funlockfile(stderr);
}

static void
ip_input(const uint8_t *data, size_t len, struct net_device *dev)
{
    // debugf("dev=%s, len=%zu", dev->name, len);
    // debugdump(data, len);
    struct ip_hdr *hdr;
    uint8_t v;
    u_int16_t hlen, offset, total;
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];

    //最小
    if(len < IP_HDR_SIZE_MIN){
        errorf("too short");
        return;
    }

    hdr = (struct ip_hdr *)data; //IPヘッダ構造体に代入

    //Exercise 6-1: IPデータグラムの検証
    //長さ一致
    v = hdr->vhl >> 4;
    if (v != IP_VERSION_IPV4) {
        errorf("ip version error: v=%u", v);
        return;
    }

    //長さ一致
    hlen = (hdr->vhl & 0x0f) << 2; //0x0f??
    if (len < hlen){
        errorf("header length error: hlen=%u, len=%u", hlen, len);
        return;
    }

    total = ntoh16(hdr->total);//変換
    if (len < total){
        errorf("total length error: total=%u, len=%u", total, len);
        return;
    }

    //チェックサム
    if (cksum16((u_int16_t *)hdr, hlen, 0) != 0){
        errorf("checksum error: sum=0x%04x, verify=0x%04x", ntoh16(hdr->sum), ntoh16(cksum16((uint16_t *)hdr, hlen, -hdr->sum))); 
        //-hdr->sumとは
        return;
    }

    //フラグメントは中断
    offset = ntoh16(hdr->offset);
    if(offset & 0x2000 || offset & 0x1fff){
        errorf("fragments does not support");
        return; 
    } //(offset & 0x2000 || offset & 0x1fff)
    //8192 2^13 0x1fff

    //Exercise 7-6: IPデータグラムのフィルタリング
    debugf("dev=%s, protocol=%u, total=%u", dev->name, hdr->protocol, total);
    ip_dump(data, total);
}

ssize_t
ip_output(uint8_t protocol, const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst)
{
    struct ip_iface *iface;
    char addr[IP_ADDR_STR_LEN];
    uint16_t id;

    if (src == IP_ADDR_ANY) {
        errorf("ip routing does not implement");
        return -1;
    } else {
        //Exercise 8-1: IPインタフェースの検索??cp
        iface = ip_iface_select(src);
        if (!iface) {
            errorf("iface not found, addr=%s", ip_addr_ntop(src, addr, sizeof(addr)));
            return -1;
        } 
        //Exercise 8-2: 宛先へ到達可能か確認??cp
        if ((dst & iface->netmask) != (iface->unicast & iface->netmask) && dst != IP_ADDR_BROADCAST) {
            errorf("not reached, addr=%s", ip_addr_ntop(src, addr, sizeof(addr)));
            return -1;
        }
    }

    if (NET_IFACE(iface)->dev->mtu < IP_HDR_SIZE_MIN + len) {
        errorf("too long, dev=%s, mtu=%u < %zu",
            NET_IFACE(iface)->dev->name, NET_IFACE(iface)->dev->mtu, IP_HDR_SIZE_MIN + len);
            //フラグメンテーションをサポートしないのでMTUを超える場合はエラーを返す
        return -1;
    }
    id = ip_generate_id(); //IPデータグラムのIDを採番
    if (ip_output_core(iface, protocol, data, len, iface->unicast, dst, id, 0) == -1) {
        errorf("ip_output_core() failure");
        return -1;
    }
    return len;
}

static ssize_t
ip_output_core(struct ip_iface *iface, uint8_t protocol, const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst, uint16_t id, uint16_t offset)
{
    uint8_t buf[IP_TOTAL_SIZE_MAX];
    struct ip_hdr *hdr;
    uint16_t hlen, total;
    char addr[IP_ADDR_STR_LEN];

    hdr = (struct ip_hdr *)buf;

    //Exercise 8-3: IPデータグラムの生成
    debugf("dev=%s, dst=%s, protocol=%u, len=%u",
        NET_IFACE(iface)->dev->name, ip_addr_ntop(dst, addr, sizeof(addr)), protocol, total);
    ip_dump(buf, total);
    return ip_output_device(iface, buf, total, dst);
}

static int
ip_output_device(struct ip_iface *iface, const uint8_t *data, size_t len, ip_addr_t dst)
{
    uint8_t hwaddr[NET_DEVICE_ADDR_LEN] = {};

    if (NET_IFACE(iface)->dev->flags & NET_DEVICE_FLAG_NEED_ARP) {
        if (dst == iface->broadcast || dst == IP_ADDR_BROADCAST) {
            memcpy(hwaddr, NET_IFACE(iface)->dev->broadcast, NET_IFACE(iface)->dev->alen);
        } else {
            errorf("arp does not implement");
            return -1;
        }
    }

    //Exercise 8-4: デバイスから送信
}



int
ip_init(void)
{
    if (net_protocol_register(NET_PROTOCOL_TYPE_IP, ip_input) == -1){
      //(uint16_t type, void (*handler)(const uint8_t *data, size_t len, struct net_device *dev))
        errorf("net_protocol_register() failure");
        return -1;
    }
    return 0;
}