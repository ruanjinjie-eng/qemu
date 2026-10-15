/*
 * QEMU MPAM emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _MPAM_H_
#define _MPAM_H_

#include "qom/object.h"

#define TYPE_MPAM_MSC_MEM "mpam-msc-mem"
#define TYPE_MPAM_MSC_CACHE "mpam-msc-cache"

/*
 * MMIO window of one MSC: IHI 0099 places the register block in a 16 KiB
 * region (0x4000).  The virt board reserves a 1 MiB slot for up to 256 MSCs.
 */
#define MPAM_SIZE 0x4000

#endif /* _MPAM_H_ */
