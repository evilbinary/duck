#include "ewok.h"


sys_info_t _sys_info;
uint32_t _allocatable_phy_mem_top = 0;
uint32_t _allocatable_phy_mem_base = 0;
uint32_t _core_base_offset = 0;

void ewok_sys_info_init(void) {
  memset(&_sys_info, 0, sizeof(sys_info_t));

#ifdef PI4
      strcpy(_sys_info.machine, "raspi4B1G");
  _sys_info.phy_mem_size = 1024 * MB;
  _sys_info.mmio.phy_base = 0xfe000000;
  _core_base_offset = 0x01800000;
#else
  _core_base_offset = 0x01000000;

  strcpy(_sys_info.machine, "raspi2B");
  _sys_info.phy_mem_size = 512 * MB;
  _sys_info.mmio.phy_base = 0x3f000000;

#endif

  _sys_info.phy_offset = 0;
  _sys_info.kernel_base = KERNEL_BASE;
  _sys_info.mmio.v_base = MMIO_BASE;
  _sys_info.mmio.size = 16 * MB;

  _sys_info.dma.size = DMA_SIZE;
  _allocatable_phy_mem_base = V2P(ALLOCATABLE_MEMORY_START);
  _allocatable_phy_mem_top = _sys_info.phy_offset + _sys_info.phy_mem_size -
                             FB_SIZE - _sys_info.dma.size;
  _sys_info.dma.phy_base = _allocatable_phy_mem_top;
}