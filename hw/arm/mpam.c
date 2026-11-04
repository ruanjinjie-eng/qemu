/*
 * MPAM emulation
 *
 * Copyright (c) 2023 Huawei
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/registerfields.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/arm/mpam.h"

/*
 * WRITE_MASK macros describe the implemented bits.  Some fields are
 * conditional on other identifier registers (MON_SEL.RIS on HAS_RIS,
 * MBWU_FLT.RWBW on HAS_RWBW, CPBM/PBM widths on CPBM_WD/BWPBM_WD);
 * the write handlers enforce those conditions at runtime.
 */
REG64(MPAMF_IDR, 0)
    FIELD(MPAMF_IDR, PARTID_MAX, 0, 16)
    FIELD(MPAMF_IDR, PMG_MAX, 16, 8)
    FIELD(MPAMF_IDR, HAS_CCAP_PART, 24, 1)
    FIELD(MPAMF_IDR, HAS_CPOR_PART, 25, 1)
    FIELD(MPAMF_IDR, HAS_MBW_PART, 26, 1)
    FIELD(MPAMF_IDR, HAS_PRI_PART, 27, 1)
    FIELD(MPAMF_IDR, EXT, 28, 1)
    FIELD(MPAMF_IDR, HAS_IMPL_IDR, 29, 1)
    FIELD(MPAMF_IDR, HAS_MSMON, 30, 1)
    FIELD(MPAMF_IDR, HAS_PARTID_NRW, 31, 1)
    FIELD(MPAMF_IDR, HAS_RIS, 32, 1)
    FIELD(MPAMF_IDR, NO_IMPL_PART, 36, 1)
    FIELD(MPAMF_IDR, NO_IMPL_MSMON, 37, 1)
    FIELD(MPAMF_IDR, HAS_EXTD_ESR, 38, 1)
    FIELD(MPAMF_IDR, HAS_ESR, 39, 1)
    FIELD(MPAMF_IDR, HAS_ERR_MSI, 40, 1)
    FIELD(MPAMF_IDR, SP4, 41, 1)
    FIELD(MPAMF_IDR, HAS_ENDIS, 42, 1)
    FIELD(MPAMF_IDR, HAS_NFU, 43, 1)
    FIELD(MPAMF_IDR, RIS_MAX, 56, 4)

REG32(MPAMF_IIDR, 0x0018)
    FIELD(MPAMF_IIDR, IMPLEMENTER, 0, 12)
    FIELD(MPAMF_IIDR, REVISION, 12, 4)
    FIELD(MPAMF_IIDR, VARIANT, 16, 4)
    FIELD(MPAMF_IIDR, PRODUCT_ID, 20, 12)

REG32(MPAMF_AIDR, 0x0020)
    FIELD(MPAMF_AIDR, ARCH_MINOR_REV, 0, 4)
    FIELD(MPAMF_AIDR, ARCH_MAJOR_REV, 4, 4)

REG32(MPAMF_IMPL_IDR, 0x0028)
REG32(MPAMF_CPOR_IDR, 0x0030)
    FIELD(MPAMF_CPOR_IDR, CPBM_WD, 0, 16)

REG32(MPAMF_CCAP_IDR, 0x0038)
    FIELD(MPAMF_CCAP_IDR, CMAX_WD, 0, 6)
    FIELD(MPAMF_CCAP_IDR, CASSOC_WD, 8, 5)
    FIELD(MPAMF_CCAP_IDR, HAS_CASSOC, 28, 1)
    FIELD(MPAMF_CCAP_IDR, HAS_CMIN, 29, 1)
    FIELD(MPAMF_CCAP_IDR, NO_CMAX, 30, 1)
    FIELD(MPAMF_CCAP_IDR, HAS_CMAX_SOFTLIM, 31, 1)

REG32(MPAMF_MBW_IDR, 0x0040)
    FIELD(MPAMF_MBW_IDR, BWA_WD, 0, 6)
    FIELD(MPAMF_MBW_IDR, HAS_MIN, 10, 1)
    FIELD(MPAMF_MBW_IDR, HAS_MAX, 11, 1)
    FIELD(MPAMF_MBW_IDR, HAS_PBM, 12, 1)
    FIELD(MPAMF_MBW_IDR, HAS_PROP, 13, 1)
    FIELD(MPAMF_MBW_IDR, WINDWR, 14, 1)
    FIELD(MPAMF_MBW_IDR, BWPBM_WD, 16, 13)

REG32(MPAMF_PRI_IDR, 0x0048)
    FIELD(MPAMF_PRI_IDR, HAS_INTPRI, 0, 1)
    FIELD(MPAMF_PRI_IDR, INTPRI_0_IS_LOW, 1, 1)
    FIELD(MPAMF_PRI_IDR, INTPRI_WD, 4, 6)
    FIELD(MPAMF_PRI_IDR, HAS_DSPRI, 16, 1)
    FIELD(MPAMF_PRI_IDR, DSPRI_0_IS_LOW, 17, 1)
    FIELD(MPAMF_PRI_IDR, DSPRI_WD, 20, 6)

REG32(MPAMF_PARTID_NRW_IDR, 0x0050)
    FIELD(MPAMF_PARTID_NRW_IDR, INTPARTID_MAX, 0, 16)

REG32(MPAMF_MSMON_IDR, 0x0080)
    FIELD(MPAMF_MSMON_IDR, MSMON_CSU, 16, 1)
    FIELD(MPAMF_MSMON_IDR, MSMON_MBWU, 17, 1)
    FIELD(MPAMF_MSMON_IDR, HAS_OFLOW_SR, 28, 1)
    FIELD(MPAMF_MSMON_IDR, HAS_OFLW_MSI, 29, 1)
    FIELD(MPAMF_MSMON_IDR, NO_OFLW_INTR, 30, 1)
    FIELD(MPAMF_MSMON_IDR, HAS_LOCAL_CAPT_EVNT, 31, 1)

REG32(MPAMF_CSUMON_IDR, 0x0088)
    FIELD(MPAMF_CSUMON_IDR, NUM_MON, 0, 16)
    FIELD(MPAMF_CSUMON_IDR, HAS_OFLOW_CAPT, 24, 1)
    FIELD(MPAMF_CSUMON_IDR, HAS_CEVNT_OFLW, 25, 1)
    FIELD(MPAMF_CSUMON_IDR, HAS_OFSR, 26, 1)
    FIELD(MPAMF_CSUMON_IDR, HAS_OFLOW_LNKG, 27, 1)
    FIELD(MPAMF_CSUMON_IDR, HAS_XCL, 29, 1)
    FIELD(MPAMF_CSUMON_IDR, CSU_RO, 30, 1)
    FIELD(MPAMF_CSUMON_IDR, HAS_CAPTURE, 31, 1)

REG32(MPAMF_MBWUMON_IDR, 0x0090)
    FIELD(MPAMF_MBWUMON_IDR, NUM_MON, 0, 16)
    FIELD(MPAMF_MBWUMON_IDR, SCALE, 16, 5)
    FIELD(MPAMF_MBWUMON_IDR, HAS_OFLOW_CAPT, 24, 1)
    FIELD(MPAMF_MBWUMON_IDR, HAS_CEVNT_OFLW, 25, 1)
    FIELD(MPAMF_MBWUMON_IDR, HAS_OFSR, 26, 1)
    FIELD(MPAMF_MBWUMON_IDR, HAS_OFLOW_LNKG, 27, 1)
    FIELD(MPAMF_MBWUMON_IDR, HAS_RWBW, 28, 1)
    FIELD(MPAMF_MBWUMON_IDR, LWD, 29, 1)
    FIELD(MPAMF_MBWUMON_IDR, HAS_LONG, 30, 1)
    FIELD(MPAMF_MBWUMON_IDR, HAS_CAPTURE, 31, 1)

REG32(MPAMF_ERR_MSI_MPAM, 0x00dc)
REG32(MPAMF_ERR_MSI_ADDR_L, 0x00e0)
REG32(MPAMF_ERR_MSI_ADDR_H, 0x00e4)
REG32(MPAMF_ERR_MSI_DATA, 0x00e8)
REG32(MPAMF_ERR_MSI_ATTR, 0x00ec)

REG32(MPAMF_ECR, 0x00f0)
    FIELD(MPAMF_ECR, INTEN, 0, 1)
#define MPAMF_ECR_WRITE_MASK ( \
    R_MPAMF_ECR_INTEN_MASK)

REG64(MPAMF_ESR, 0x00f8)
    FIELD(MPAMF_ESR, PARID_MON, 0, 16)
    FIELD(MPAMF_ESR, PMG, 16, 8)
    FIELD(MPAMF_ESR, ERR_CODE, 24, 4)
    FIELD(MPAMF_ESR, OVRWR, 31, 1)
    FIELD(MPAMF_ESR, RIS, 32, 4)

REG32(MPAMCFG_PART_SEL, 0x0100)
    FIELD(MPAMCFG_PART_SEL, PARTID_SEL, 0, 16)
    FIELD(MPAMCFG_PART_SEL, INTERNAL, 16, 1)
    FIELD(MPAMCFG_PART_SEL, RIS, 24, 4)
#define MPAMCFG_PART_SEL_WRITE_MASK ( \
    R_MPAMCFG_PART_SEL_PARTID_SEL_MASK | \
    R_MPAMCFG_PART_SEL_INTERNAL_MASK | \
    R_MPAMCFG_PART_SEL_RIS_MASK)

REG32(MPAMF_MPAMCFG_CMAX, 0x0108)
    FIELD(MPAMF_MPAMCFG_CMAX, CMAX, 0, 16)
    FIELD(MPAMF_MPAMCFG_CMAX, SOFTLIM, 31, 1)
#define MPAMF_MPAMCFG_CMAX_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_CMAX_CMAX_MASK | \
    R_MPAMF_MPAMCFG_CMAX_SOFTLIM_MASK)

REG32(MPAMF_MPAMCFG_CMIN, 0x0110)
    FIELD(MPAMF_MPAMCFG_CMIN, CMIN, 0, 16)
#define MPAMF_MPAMCFG_CMIN_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_CMIN_CMIN_MASK)

REG32(MPAMF_MPAMCFG_CASSOC, 0x0118)
    FIELD(MPAMF_MPAMCFG_CASSOC, CASSOC, 0, 16)
#define MPAMF_MPAMCFG_CASSOC_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_CASSOC_CASSOC_MASK)

REG32(MPAMF_MPAMCFG_MBW_MIN, 0x0200)
    FIELD(MPAMF_MPAMCFG_MBW_MIN, MIN, 0, 16)
#define MPAMF_MPAMCFG_MBW_MIN_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_MBW_MIN_MIN_MASK)

REG32(MPAMF_MPAMCFG_MBW_MAX, 0x0208)
    FIELD(MPAMF_MPAMCFG_MBW_MAX, MAX, 0, 16)
    FIELD(MPAMF_MPAMCFG_MBW_MAX, HARDLIM, 31, 1)
#define MPAMF_MPAMCFG_MBW_MAX_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_MBW_MAX_MAX_MASK | \
    R_MPAMF_MPAMCFG_MBW_MAX_HARDLIM_MASK)

REG32(MPAMF_MPAMCFG_WINWD, 0x0220)
    FIELD(MPAMF_MPAMCFG_WINWD, US_FRAC, 0, 8)
    FIELD(MPAMF_MPAMCFG_WINWD, US_INT, 8, 16)
#define MPAMF_MPAMCFG_WINWD_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_WINWD_US_FRAC_MASK | \
    R_MPAMF_MPAMCFG_WINWD_US_INT_MASK)

REG32(MPAMF_MPAMCFG_EN, 0x0300)
    FIELD(MPAMF_MPAMCFG_EN, PARTID, 0, 16)
#define MPAMF_MPAMCFG_EN_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_EN_PARTID_MASK)

REG32(MPAMF_MPAMCFG_DIS, 0x0310)
    FIELD(MPAMF_MPAMCFG_DIS, PARTID, 0, 16)
    FIELD(MPAMF_MPAMCFG_DIS, NFU, 31, 1)
#define MPAMF_MPAMCFG_DIS_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_DIS_PARTID_MASK | \
    R_MPAMF_MPAMCFG_DIS_NFU_MASK)

REG32(MPAMF_MPAMCFG_EN_FLAGS, 0x0320)

REG32(MPAMF_MPAMCFG_PRI, 0x0400)
    FIELD(MPAMF_MPAMCFG_PRI, INTPRI, 0, 16)
    FIELD(MPAMF_MPAMCFG_PRI, DSPRI, 16, 16)
#define MPAMF_MPAMCFG_PRI_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_PRI_INTPRI_MASK | \
    R_MPAMF_MPAMCFG_PRI_DSPRI_MASK)

REG32(MPAMF_MPAMCFG_MBW_PROP, 0x0500)
    FIELD(MPAMF_MPAMCFG_MBW_PROP, STRIDEM1, 0, 16)
    FIELD(MPAMF_MPAMCFG_MBW_PROP, EN, 31, 1)
#define MPAMF_MPAMCFG_MBW_PROP_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_MBW_PROP_STRIDEM1_MASK | \
    R_MPAMF_MPAMCFG_MBW_PROP_EN_MASK)

REG32(MPAMF_MPAMCFG_INTPARTID, 0x0600)
    FIELD(MPAMF_MPAMCFG_INTPARTID, INTPARTID, 0, 16)
    FIELD(MPAMF_MPAMCFG_INTPARTID, INTERNAL, 16, 1)
#define MPAMF_MPAMCFG_INTPARTID_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_INTPARTID_INTPARTID_MASK | \
    R_MPAMF_MPAMCFG_INTPARTID_INTERNAL_MASK)

REG32(MPAMF_MPAMCFG_CPBM0, 0x1000)

REG32(MPAMF_MPAMCFG_MBW_PBM0, 0x2000)

#define TYPE_MPAM_MSC "mpam-msc"
/*
 * Max number of portions this model exposes for the cache-portion (CPOR)
 * bitmap and the memory-bandwidth-portion (MBW_PBM) bitmap.  IHI 0099
 * dimensions CPBM_WD/BWPBM_WD by these widths (see MPAMF_CPOR/MBW_IDR).
 */
#define MPAM_MBW_PART 4
#define MPAM_CACHE_PART 32
/* Implementer 0 is reserved for software; product ID 'Q'. */
#define MPAM_IIDR_IMPLEMENTER 0

typedef struct Mpamf {
    uint64_t idr;
    uint32_t iidr;
    uint32_t aidr;
    uint32_t impl_idr;
    uint32_t cpor_idr;
    uint32_t ccap_idr;
    uint32_t mbw_idr;
    uint32_t pri_idr;
    uint32_t partid_nrw_idr;
    uint32_t msmon_idr;
    uint32_t csumon_idr;
    uint32_t mbwumon_idr;
    uint32_t err_msi_mpam;
    uint32_t err_msi_addr_l;
    uint32_t err_msi_addr_h;
    uint32_t err_msi_data;
    uint32_t err_msi_attr;
    uint32_t ecr;
    uint32_t esr;
    uint32_t cfg_part_sel;
    /* Array sizes for migration (derived from the MSC properties). */
    uint32_t num_partid_vm;
    uint32_t num_int_partid_vm;
    uint32_t num_mbwu_mon_vm;
    uint32_t num_csu_mon_vm;
} Mpamf;

typedef struct MPAMMSCState {
    SysBusDevice parent_obj;

    Mpamf *mpamf;

    uint8_t ris;
    uint16_t part_sel; /* Technically per ris, but in same reg */
    bool internal_part_sel;
    struct MemoryRegion mr;
    uint32_t num_partid;
    uint32_t num_int_partid;
    uint8_t num_ris;
    /* Array size for migration (derived from num_ris). */
    uint32_t num_ris_vm;

} MPAMMSCState;

typedef struct MPAMMSCMemState {
    MPAMMSCState parent;
} MPAMMSCMemState;

typedef struct MPAMMSCCacheState {
    MPAMMSCState parent;
    uint8_t cache_level;
    uint8_t cache_type;
    uint16_t cpu;

} MPAMMSCCacheState;

DECLARE_INSTANCE_CHECKER(MPAMMSCState, MPAM_MSC_DEVICE, TYPE_MPAM_MSC);

static bool mpam_has_csu_mon(Mpamf *mf)
{
    return FIELD_EX32(mf->idr, MPAMF_IDR, HAS_MSMON) &&
           FIELD_EX32(mf->msmon_idr, MPAMF_MSMON_IDR, MSMON_CSU);
}

static bool mpam_has_mbwu_mon(Mpamf *mf)
{
    return FIELD_EX32(mf->idr, MPAMF_IDR, HAS_MSMON) &&
           FIELD_EX32(mf->msmon_idr, MPAMF_MSMON_IDR, MSMON_MBWU);
}

static const VMStateDescription vmstate_mpamf = {
    .name = "mpam/mpamf",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(idr, Mpamf),
        VMSTATE_UINT32(iidr, Mpamf),
        VMSTATE_UINT32(aidr, Mpamf),
        VMSTATE_UINT32(impl_idr, Mpamf),
        VMSTATE_UINT32(cpor_idr, Mpamf),
        VMSTATE_UINT32(ccap_idr, Mpamf),
        VMSTATE_UINT32(mbw_idr, Mpamf),
        VMSTATE_UINT32(pri_idr, Mpamf),
        VMSTATE_UINT32(partid_nrw_idr, Mpamf),
        VMSTATE_UINT32(msmon_idr, Mpamf),
        VMSTATE_UINT32(csumon_idr, Mpamf),
        VMSTATE_UINT32(mbwumon_idr, Mpamf),
        VMSTATE_UINT32(err_msi_mpam, Mpamf),
        VMSTATE_UINT32(err_msi_addr_l, Mpamf),
        VMSTATE_UINT32(err_msi_addr_h, Mpamf),
        VMSTATE_UINT32(err_msi_data, Mpamf),
        VMSTATE_UINT32(err_msi_attr, Mpamf),
        VMSTATE_UINT32(ecr, Mpamf),
        VMSTATE_UINT32(esr, Mpamf),
        VMSTATE_UINT32(cfg_part_sel, Mpamf),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_mpam_msc = {
    .name = "mpam/msc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(ris, MPAMMSCState),
        VMSTATE_UINT16(part_sel, MPAMMSCState),
        VMSTATE_BOOL(internal_part_sel, MPAMMSCState),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(mpamf, MPAMMSCState,
                                             num_ris_vm,
                                             vmstate_mpamf, Mpamf),
        VMSTATE_END_OF_LIST()
    },
};
DECLARE_INSTANCE_CHECKER(MPAMMSCMemState, MPAM_MSC_MEM_DEVICE,
                         TYPE_MPAM_MSC_MEM);

DECLARE_INSTANCE_CHECKER(MPAMMSCCacheState, MPAM_MSC_CACHE_DEVICE,
                         TYPE_MPAM_MSC_CACHE);

static uint64_t mpam_msc_read_reg(void *opaque, hwaddr offset,
                                  unsigned size)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(opaque);
    Mpamf *mpamf = &s->mpamf[s->ris];

    switch (offset) {
    case A_MPAMF_IDR:
        switch (size) {
        case 4:
            return mpamf->idr & MAKE_64BIT_MASK(0, 32);
        case 8:
            return mpamf->idr;
        default:
            qemu_log_mask(LOG_UNIMP, "MPAM: Unexpected read size\n");
            return 0;
        }
    case A_MPAMF_IDR + 0x04:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, EXT)) {
            qemu_log_mask(LOG_UNIMP, "MPAM: Unexpected read of top of IDR\n");
            return 0;
        }
        switch (size) {
        case 4:
            return mpamf->idr >> 32;
        default:
            qemu_log_mask(LOG_UNIMP, "MPAM: Unexpected read size\n");
            return 0;
        }
    case A_MPAMF_IIDR:
        return mpamf->iidr;
    case A_MPAMF_AIDR:
        return mpamf->aidr;
    case A_MPAMF_IMPL_IDR:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_IMPL_IDR)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Accessing IMPL_IDR which isn't supported\n");
            return 0;
        }
        return mpamf->impl_idr;
    case A_MPAMF_PARTID_NRW_IDR: {
        return mpamf->partid_nrw_idr;
    }
    case A_MPAMF_MSMON_IDR:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MSMON)) {
            return 0;
        }
        return mpamf->msmon_idr;
    case A_MPAMF_CSUMON_IDR:
        if (!mpam_has_csu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CSUMON_IDR with no CSU monitor\n");
            return 0;
        }
        return mpamf->csumon_idr;
    case A_MPAMF_MBWUMON_IDR:
        if (!mpam_has_mbwu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBWUMON_IDR with no MBWU monitor\n");
            return 0;
        }
        return mpamf->mbwumon_idr;
    case A_MPAMCFG_PART_SEL:
        return mpamf->cfg_part_sel;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "MPAM: Unexpected read of %lx\n", offset);
        return 0x0;
    }
}

static void mpam_msc_write_reg(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(opaque);
    Mpamf *mpamf = &s->mpamf[s->ris];

    switch (offset) {
    case A_MPAMCFG_PART_SEL: {
        uint32_t ris = FIELD_EX32(value, MPAMCFG_PART_SEL, RIS);

        if (value & ~MPAMCFG_PART_SEL_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CFG_PART_SEL Mask=%x Value=%lx\n",
                          MPAMCFG_PART_SEL_WRITE_MASK, value);
        }
        /* Field matches for all RIS */
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_RIS) &&
            FIELD_EX32(value, MPAMCFG_PART_SEL, RIS) != 0) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write of non 0 RIS on MSC with !HAS_RIS\n");
            return;
        }
        if (FIELD_EX32(value, MPAMCFG_PART_SEL, RIS) >= s->num_ris) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: RIS %d out of range (num-ris %d)\n",
                          FIELD_EX32(value, MPAMCFG_PART_SEL, RIS),
                          s->num_ris);
            return;
        }
        s->ris = ris;
        s->part_sel = FIELD_EX32(value, MPAMCFG_PART_SEL, PARTID_SEL);
        s->internal_part_sel = FIELD_EX32(value, MPAMCFG_PART_SEL, INTERNAL);
        mpamf = &s->mpamf[s->ris];
        mpamf->cfg_part_sel = value & MPAMCFG_PART_SEL_WRITE_MASK;
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "MPAM: Write to unexpected register Addr %lx Value=%lx\n",
                      offset, value);
    }
}

static const MemoryRegionOps mpam_msc_ops = {
    .read = mpam_msc_read_reg,
    .write = mpam_msc_write_reg,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

/*
 * Power-on state of the MSC, shared between realize and reset.
 */
static void mpam_msc_state_init(MPAMMSCState *s)
{
    s->ris = 0;
    s->part_sel = 0;
    s->internal_part_sel = false;
}

static void mpam_msc_reset(DeviceState *dev)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);

    mpam_msc_state_init(s);
}

static bool mpam_msc_realize(DeviceState *dev, Error **errp)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);

    /* IHI 0099: MPAMCFG_PART_SEL.RIS is 4 bits, so at most 16 RIS. */
    if (s->num_ris < 1 || s->num_ris > 16) {
        error_setg(errp, "num-ris must be in the range 1..16");
        return false;
    }
    /* PARTID_MAX is a 16-bit field. */
    if (s->num_partid < 1 || s->num_partid > 65536) {
        error_setg(errp, "num-partid must be in the range 1..65536");
        return false;
    }
    if (s->num_int_partid == 0) {
        s->num_int_partid = s->num_partid;
    }
    if (s->num_int_partid > 65536) {
        error_setg(errp, "num-int-partid must be <= 65536");
        return false;
    }

    s->num_ris_vm = s->num_ris;
    s->mpamf = g_new0(Mpamf, s->num_ris);
    mpam_msc_state_init(s);

    memory_region_init_io(&s->mr, OBJECT(s), &mpam_msc_ops, s, "mpam_msc",
                          MPAM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
    return true;
}

static void mpam_msc_mem_realize(DeviceState *dev, Error **errp)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);

    if (!mpam_msc_realize(dev, errp)) {
        return;
    }

    for (int i = 0; i < s->num_ris; i++) {
        Mpamf *mpamf = &s->mpamf[i];

        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PARTID_MAX,
                                s->num_partid - 1);
        /* No PCMG for now */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PMG_MAX, 0);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, EXT, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_RIS, s->num_ris > 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, RIS_MAX,
                                  s->num_ris > 1 ? s->num_ris - 1 : 0);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_ESR, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_EXTD_ESR, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW,
                                s->num_int_partid < s->num_partid);

        /* We won't implement any implementation specific stuff */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_MSMON, 1);

        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, IMPLEMENTER,
                                 MPAM_IIDR_IMPLEMENTER);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, REVISION, 0);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, VARIANT, 0);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, PRODUCT_ID, 'Q');

        mpamf->aidr = FIELD_DP64(mpamf->aidr, MPAMF_AIDR, ARCH_MINOR_REV, 1);
        mpamf->aidr = FIELD_DP64(mpamf->aidr, MPAMF_AIDR, ARCH_MAJOR_REV, 1);

        if (s->num_int_partid < s->num_partid) {
            mpamf->partid_nrw_idr = FIELD_DP32(mpamf->partid_nrw_idr,
                                               MPAMF_PARTID_NRW_IDR,
                                               INTPARTID_MAX,
                                               s->num_int_partid - 1);
        }
    }
}

static void mpam_msc_cache_realize(DeviceState *dev, Error **errp)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);

    if (!mpam_msc_realize(dev, errp)) {
        return;
    }

    for (int i = 0; i < s->num_ris; i++) {
        Mpamf *mpamf = &s->mpamf[i];

        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PARTID_MAX,
                                s->num_partid - 1);
        /* No PCMG for now */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PMG_MAX, 0);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, EXT, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_RIS, s->num_ris > 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, RIS_MAX,
                                  s->num_ris > 1 ? s->num_ris - 1 : 0);

        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_ESR, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_EXTD_ESR,
                                s->num_ris > 1);

        /* We won't implement any implementation specific stuff */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_MSMON, 1);

        /* Need to implement for RME */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, SP4, 0);

        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW,
                                s->num_int_partid < s->num_partid);

        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, IMPLEMENTER,
                                 MPAM_IIDR_IMPLEMENTER);
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, REVISION, 0);
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, VARIANT, 0);
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, PRODUCT_ID, 'Q');

        mpamf->aidr = FIELD_DP32(mpamf->aidr, MPAMF_AIDR, ARCH_MINOR_REV, 1);
        mpamf->aidr = FIELD_DP32(mpamf->aidr, MPAMF_AIDR, ARCH_MAJOR_REV, 1);

        if (s->num_int_partid < s->num_partid) {
            mpamf->partid_nrw_idr = FIELD_DP32(mpamf->partid_nrw_idr,
                                               MPAMF_PARTID_NRW_IDR,
                                               INTPARTID_MAX,
                                               s->num_int_partid - 1);
        }
        /* TODO: Initialize all on as if firmware had done it for us */
    }
}

static const Property mpam_msc_props[] = {
    DEFINE_PROP_UINT8("num-ris", MPAMMSCState, num_ris, 1),
    DEFINE_PROP_UINT32("num-partid", MPAMMSCState, num_partid, 1),
    DEFINE_PROP_UINT32("num-int-partid", MPAMMSCState, num_int_partid, 0),
};

static const Property mpam_msc_cache_props[] = {
    DEFINE_PROP_UINT8("cache-level", MPAMMSCCacheState, cache_level, 1),
    DEFINE_PROP_UINT8("cache-type", MPAMMSCCacheState, cache_type, 1),
    DEFINE_PROP_UINT16("cpu", MPAMMSCCacheState, cpu, 2),
};

static void mpam_msc_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, mpam_msc_props);
    dc->vmsd = &vmstate_mpam_msc;
    /* Board-internal sysbus devices; not creatable from the command line. */
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, mpam_msc_reset);
}

static void mpam_msc_mem_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mpam_msc_mem_realize;
}

static void mpam_msc_cache_init(ObjectClass *klass, const void *data)
{
   DeviceClass *dc = DEVICE_CLASS(klass);

   dc->realize = mpam_msc_cache_realize;
   device_class_set_props(dc, mpam_msc_cache_props);
}

static const TypeInfo mpam_msc_info = {
    .name = TYPE_MPAM_MSC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MPAMMSCState),
    .class_init = mpam_msc_init
};

static const TypeInfo mpam_msc_mem_info = {
    .name = TYPE_MPAM_MSC_MEM,
    .parent = TYPE_MPAM_MSC,
    .instance_size = sizeof(MPAMMSCMemState),
    .class_init = mpam_msc_mem_init
};

static const TypeInfo mpam_msc_cache_info = {
    .name = TYPE_MPAM_MSC_CACHE,
    .parent = TYPE_MPAM_MSC,
    .instance_size = sizeof(MPAMMSCCacheState),
    .class_init = mpam_msc_cache_init
};

static void mpam_register_types(void)
{
    type_register_static(&mpam_msc_info);
    type_register_static(&mpam_msc_mem_info);
    type_register_static(&mpam_msc_cache_info);
}
type_init(mpam_register_types);
