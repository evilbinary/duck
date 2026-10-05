/* netif.c / netdb.c 会 include <stdlib.h>。同样不要用工具链那份。 */
#ifndef LWIP_PORT_STDLIB_H
#define LWIP_PORT_STDLIB_H

int atoi(const char* s);

#endif
