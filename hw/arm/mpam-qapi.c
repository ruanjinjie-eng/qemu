/*
 * QEMU MPAM QMP commands
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/mpam.h"
#include "qom/object.h"
#include "qapi/qapi-commands-mpam.h"

typedef struct MPAMQueryState {
    MpamCacheInfoList **head;
} MPAMQueryState;

static int mpam_query_cache(Object *obj, void *opaque)
{
    MPAMQueryState *state = opaque;
    MpamCacheInfoList *infolist;
    MpamCacheInfo *info;

    if (!object_dynamic_cast(obj, TYPE_MPAM_MSC_CACHE)) {
        return 0;
    }

    infolist = g_malloc0(sizeof(*infolist));
    info = g_malloc0(sizeof(*info));

    mpam_cache_fill_info(obj, info);

    infolist->value = info;

    *state->head = infolist;
    state->head = &infolist->next;

    return 0;
}

MpamCacheInfoList *qmp_query_mpam_cache(Error **errp)
{
    MpamCacheInfoList *head = NULL;
    MPAMQueryState state = {
        .head = &head,
    };

    object_child_foreach_recursive(object_get_root(), mpam_query_cache, &state);

    return head;
}
