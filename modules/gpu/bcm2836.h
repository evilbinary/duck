#ifndef BCM2836_H
#define BCM2836_H

#include "kernel/kernel.h"
#include "bcm/bcm2835.h"
#include "bcm/mailbox.h"

typedef struct pixel {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} pixel_t;


#endif
