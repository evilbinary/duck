/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
/* /dev/cpu 的通用后端：没有温度源的平台。
 *
 * 节点仍然由抽象层（cpu.c）挂上，只是读不到值 —— read 返回 0 字节，
 * 用户态 fand（app/cmd/fand.c）据此跳过调档，不会每 2 秒刷 open 失败。
 * 要接新平台的温度源，在 ya.py 的 plat_source 里加一个 <平台>.c
 * 实现 cpu_temp_read() 即可，抽象层不用动。 */
#include "cpu.h"

int cpu_temp_read(int* milli_celsius) {
  (void)milli_celsius;
  return -1;
}
