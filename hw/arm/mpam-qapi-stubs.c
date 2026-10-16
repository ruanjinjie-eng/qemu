/*
 * QEMU MPAM QMP command stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-mpam.h"

MpamCacheInfoList *qmp_query_mpam_cache(bool has_level, int64_t level,
                                        Error **errp)
{
    return NULL;
}
