#include "lwip/opt.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include "kernel/device.h"

/* 直连 Mac USB 网卡时的地址。改这里即可，不必动驱动。 */
#define LWIP_IP_A 192
#define LWIP_IP_B 168
#define LWIP_IP_C 1
#define LWIP_IP_D 250
#define LWIP_GW_D 1

#define ETH_FRAME_MAX 1514
#define ETH_RX_BURST 8

static struct netif lwip_netif;
static int lwip_netif_ready;
static volatile int pumping;

static device_t* nic(void) { return device_find(DEVICE_NET); }

static err_t low_level_output(struct netif* netif, struct pbuf* p) {
  u8_t frame[ETH_FRAME_MAX];
  u16_t n;
  device_t* dev;
  size_t w;
  (void)netif;
  if (p == NULL || p->tot_len == 0 || p->tot_len > ETH_FRAME_MAX) return ERR_BUF;
  n = pbuf_copy_partial(p, frame, p->tot_len, 0);
  if (n != p->tot_len) return ERR_BUF;
  dev = nic();
  if (dev == NULL || dev->write == NULL) return ERR_IF;
  w = dev->write(dev, frame, n);
  if (w == 0) return ERR_IF;
  return ERR_OK;
}

err_t ethernetif_init(struct netif* netif) {
  device_t* dev;
  if (netif == NULL) return ERR_ARG;
  netif->hwaddr_len = ETH_HWADDR_LEN;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;
  dev = nic();
  if (dev != NULL && dev->ioctl != NULL) {
    dev->ioctl(dev, 0x01, netif->hwaddr);
  }
  return ERR_OK;
}

static void poll_link(struct netif* netif) {
  device_t* dev;
  u32 up = 0;
  static u32_t last;
  u32_t now = sys_now();
  if ((now - last) < 500 && last != 0) return;
  last = now;
  dev = nic();
  if (dev == NULL || dev->ioctl == NULL) return;
  dev->ioctl(dev, 0x02, &up);
  if (up) {
    if (!netif_is_link_up(netif)) netif_set_link_up(netif);
  } else if (netif_is_link_up(netif)) {
    netif_set_link_down(netif);
  }
}

static void drain(struct netif* netif) {
  device_t* dev = nic();
  int i;
  if (dev == NULL || dev->read == NULL) return;
  for (i = 0; i < ETH_RX_BURST; i++) {
    u8_t frame[1600];
    size_t n = dev->read(dev, frame, sizeof(frame));
    struct pbuf* p;
    if (n < 14) break;
    if (n > 1600) n = 1600;
    p = pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);
    if (p == NULL) break;
    if (pbuf_take(p, frame, (u16_t)n) != ERR_OK) {
      pbuf_free(p);
      break;
    }
    if (ethernet_input(p, netif) != ERR_OK) pbuf_free(p);
  }
}

void ethernetif_pump(void) {
  extern int gem_pf_trace;
  if (!lwip_netif_ready) return;
  if (__sync_lock_test_and_set(&pumping, 1)) return;
  LOCK_TCPIP_CORE();
  poll_link(&lwip_netif);
  if (gem_pf_trace == 2) kprintf("pf6 link\n");
  drain(&lwip_netif);
  if (gem_pf_trace == 2) kprintf("pf7 drain\n");
  sys_check_timeouts();
  if (gem_pf_trace == 2) {
    kprintf("pf8 tmo\n");
    gem_pf_trace = 3;
  }
  UNLOCK_TCPIP_CORE();
  __sync_lock_release(&pumping);
}

static void lwip_netif_bringup(void* arg) {
  ip4_addr_t ip, mask, gw;
  (void)arg;
  IP4_ADDR(&ip, LWIP_IP_A, LWIP_IP_B, LWIP_IP_C, LWIP_IP_D);
  IP4_ADDR(&mask, 255, 255, 255, 0);
  IP4_ADDR(&gw, LWIP_IP_A, LWIP_IP_B, LWIP_IP_C, LWIP_GW_D);
  netif_add(&lwip_netif, &ip, &mask, &gw, NULL, ethernetif_init, ethernet_input);
  netif_set_default(&lwip_netif);
  netif_set_up(&lwip_netif);
  lwip_netif_ready = 1;
  kprintf("lwip: %u.%u.%u.%u/24 gw %u.%u.%u.%u\n", LWIP_IP_A, LWIP_IP_B, LWIP_IP_C,
          LWIP_IP_D, LWIP_IP_A, LWIP_IP_B, LWIP_IP_C, LWIP_GW_D);
}

void lwip_netif_start(void) { tcpip_init(lwip_netif_bringup, NULL); }
