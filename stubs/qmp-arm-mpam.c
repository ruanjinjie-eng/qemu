/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-mpam.h"


MpamCacheInfoList *qmp_query_mpam_cache(Error **errp)
{
    error_setg(errp, "MPAM is not available on this target");
    return NULL;
}
