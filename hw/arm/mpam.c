/*
 * ARM MPAM emulation
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
#include "hw/arm/mpam.h"

REG64(MPAMF_IDR, 0)
    FIELD(MPAMF_IDR, PART_ID_MAX, 0, 16)
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
    FIELD(MPAMF_IDR, HAS_ERR_MS, 40, 1)
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

REG32(MPAMF_MSMON_IDR, 0x080)
    FIELD(MPAMF_MSMON_IDR, MSMON_CSU, 16, 1)
    FIELD(MPAMF_MSMON_IDR, MSMON_MBWU, 17, 1)
    FIELD(MPAMF_MSMON_IDR, HAS_OFLOW_SR, 28, 1)
    FIELD(MPAMF_MSMON_IDR, HAS_OFLW_MS, 29, 1)
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

REG32(MSMON_CFG_MON_SEL, 0x0800)
    FIELD(MSMON_CFG_MON_SEL, MON_SEL, 0, 8)
    FIELD(MSMON_CFG_MON_SEL, RIS, 24, 4)

REG32(MSMON_CFG_MBWU_FLT, 0x0820)
    FIELD(MSMON_CFG_MBWU_FLT, PARTID, 0, 16)
    FIELD(MSMON_CFG_MBWU_FLT, PMG, 16, 8)
    FIELD(MSMON_CFG_MBWU_FLT, RWBW, 30, 2)

REG32(MSMON_CFG_MBWU_CTL, 0x0828)
    FIELD(MSMON_CFG_MBWU_CTL, TYPE, 0, 8)
    FIELD(MSMON_CFG_MBWU_CTL, MATCH_PARTID, 16, 1)
    FIELD(MSMON_CFG_MBWU_CTL, MATCH_PMG, 17, 1)
    FIELD(MSMON_CFG_MBWU_CTL, SCLEN, 19, 1)
    FIELD(MSMON_CFG_MBWU_CTL, SUBTYPE, 20, 4)
    FIELD(MSMON_CFG_MBWU_CTL, OFLOW_FRZ, 24, 1)
    FIELD(MSMON_CFG_MBWU_CTL, OFLOW_INTR, 25, 1)
    FIELD(MSMON_CFG_MBWU_CTL, OFLOW_STATUS, 26, 1)
    FIELD(MSMON_CFG_MBWU_CTL, CAPT_RESET, 27, 1)
    FIELD(MSMON_CFG_MBWU_CTL, CAPT_EVNT, 28, 3)
    FIELD(MSMON_CFG_MBWU_CTL, EN, 31, 1)

REG32(MSMON_MBWU, 0x0860)           /* 31-bit counter, bit31 NRDY */
REG32(MSMON_MBWU_L, 0x0880)         /* 64-bit counter */
REG32(MSMON_MBWU_CAPTURE, 0x0868)
REG32(MSMON_MBWU_CAPTURE_L, 0x0890)
REG32(MSMON_CAPT_EVNT, 0x0808)
REG32(MSMON_CFG_CSU_FLT, 0x0810)
REG32(MSMON_CFG_CSU_CTL, 0x0818)

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

REG32(MPAMF_CFG_PART_SEL, 0x0100)
    FIELD(MPAMF_CFG_PART_SEL, PARTID_SEL, 0, 16)
    FIELD(MPAMF_CFG_PART_SEL, INTERNAL, 16, 1)
    FIELD(MPAMF_CFG_PART_SEL, RIS, 24, 4)
#define MPAMF_CFG_PART_SEL_WRITE_MASK ( \
    R_MPAMF_CFG_PART_SEL_PARTID_SEL_MASK | \
    R_MPAMF_CFG_PART_SEL_INTERNAL_MASK | \
    R_MPAMF_CFG_PART_SEL_RIS_MASK)

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
    R_MPAMF_MPAMCFG_WINWD_US_FRAC | \
    R_MPAMF_MPAMCFG_WINWD_US_INT)

REG32(MPAMF_MPAMCFG_EN, 0x0300)
    FIELD(MPAMF_MPAMCFG_EN, PARTID, 0, 16)
#define MPAMF_MPAMCFG_EN_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_EN_PARTID_MASK)

REG32(MPAMF_MPAMCFG_DIS, 0x0310) /* WHat is this for? */
    FIELD(MPAMF_MPAMCFG_DIS, PARTID, 0, 16)
    FIELD(MPAMF_MPAMCFG_DIS, NFU, 31, 1)
#define MPAMF_MPAMCFG_DIS_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_DIS_PARTID_MASK | \
    R_MPAMF_MPAMCFG_DIS_NFU)

REG32(MPAMF_MPAMCFG_EN_FLAGS, 0x320)

REG32(MPAMF_MPAMCFG_PRI, 0x400)
    FIELD(MPAMF_MPAMCFG_PRI, INTPRI, 0, 16)
    FIELD(MPAMF_MPAMCFG_PRI, DSPRI, 16, 16)
#define MPAMF_MPAMCFG_PRI_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_PRI_INTPRI_MASK | \
    R_MPAMF_MPAMCFG_PRI_DSPRI_MASK)

REG32(MPAMF_MPAMCFG_MBW_PROP, 0x500)
    FIELD(MPAMF_MPAMCFG_MBW_PROP, STRIDEM1, 0, 16)
    FIELD(MPAMF_MPAMCFG_MBW_PROP, EN, 31, 1)
#define MPAMF_MPAMCFG_MBW_PROP_WRITE_MASK ( \
    R_MPAMF_MPAMCFG_MBW_PROP_STRIDEM1_MASK | \
    R_MPAMF_MPAMCFG_MBW_PROP_EN_MASK)

REG32(MPAMF_MPAMCFG_INTPARTID, 0x600)
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
/* MPAMF_IIDR.IMPLEMENTER: ARM JEP106 code for Huawei (0x736). */
#define MPAM_IIDR_IMPLEMENTER 0x736

typedef struct MpamfPerNrwId {
        uint32_t cfg_cpbm[(MPAM_CACHE_PART + 31) / 32];
        uint32_t cfg_mbw_pbm[(MPAM_MBW_PART + 31) / 32];
        uint32_t cfg_pri;
        uint32_t cfg_cmax;
        uint32_t cfg_mbw_min;
        uint32_t cfg_mbw_max;
        uint32_t cfg_mbw_prop;
} MpamfPerNrwId;

/*
 * One MBWU monitor instance.  The counter is lazily advanced on
 * access: value = base + rate x (now - t0), where rate is the
 * nominal bandwidth scaled by the MBW_MAX fraction of the filtered
 * PARTID.  Monitoring therefore reads back values that track the
 * configured bandwidth control.
 */
typedef struct MpamMbwuMon {
    uint32_t flt;
    uint32_t ctl;
    uint64_t base;
    int64_t t0;
    bool oflow;
} MpamMbwuMon;

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
    uint32_t *cfg_intpartid;

    MpamfPerNrwId *per_nrw_id;

    MpamMbwuMon *mbwu_mons;

} Mpamf;

typedef struct MPAMMSCState {
    SysBusDevice parent_obj;

    Mpamf *mpamf;

    uint8_t ris;
    uint16_t part_sel; /* Technically per ris, but in same reg */
    bool internal_part_sel;
    uint32_t mon_sel;
    struct MemoryRegion mr;
    uint32_t num_partid;
    uint32_t num_int_partid;
    uint8_t num_ris;
    uint32_t num_mbwu_mon;
    uint64_t bw_nominal_bps;

    /*
     * Wired-interrupt GSIVs the board assigned (INTID = SPI + 32).  Zero
     * means "no wired interrupt"; the ACPI MPAM table reports them as-is.
     */
    uint32_t err_gsiv;
    uint32_t oflow_gsiv;

    /*
     * Memory-MSC locator: the NUMA node this instance serves.  UINT16_MAX
     * means unset (single-MSC mode covers every memory node instead).
     */
    uint16_t node;

} MPAMMSCState;

/*
 * ID narrowing may be in effect.  If it is there is an indirection
 * table per RIS mapping from part_sel to the internal ID. To make things
 * more complex, the Partition selection register can directly address
 * internal IDs. That works for everything other than the ID map itself.
 * This function pulls the right internal ID out of this complexity
 * for use in accessing the per_nrw_id structures.
 */
static uint32_t mpam_get_nrw_id(MPAMMSCState *s)
{
    Mpamf *mpamf = &s->mpamf[s->ris];
    uint32_t intpartid;

    if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW)) {
        return s->part_sel;
    }
    if (s->internal_part_sel) {
        intpartid = s->part_sel;
    } else {
        intpartid = mpamf->cfg_intpartid[s->part_sel];
    }

    return intpartid % s->num_int_partid;
}

/*
 * Resolve a request PARTID from a monitor filter onto the internal
 * PARTID whose configuration governs it.
 */
static uint32_t mpam_nrw_id_for(MPAMMSCState *s, Mpamf *mf, uint32_t partid)
{
    uint32_t intpartid;

    if (!FIELD_EX32(mf->idr, MPAMF_IDR, HAS_PARTID_NRW)) {
        return partid;
    }

    intpartid = mf->cfg_intpartid[partid % s->num_partid];
    return intpartid % s->num_int_partid;
}

static Mpamf *mpam_mon_ris(MPAMMSCState *s)
{
    unsigned ris = FIELD_EX32(s->mon_sel, MSMON_CFG_MON_SEL, RIS);

    if (ris >= s->num_ris) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPAM: MON_SEL RIS %d out of range (num-ris %d)\n",
                      ris, s->num_ris);
        ris = 0;
    }
    return &s->mpamf[ris];
}

static MpamMbwuMon *mpam_mbwu_mon(MPAMMSCState *s, Mpamf *mf)
{
    unsigned mon = FIELD_EX32(s->mon_sel, MSMON_CFG_MON_SEL, MON_SEL);

    if (mon >= s->num_mbwu_mon) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPAM: MON_SEL %d out of range (num-mbwu-mon %d)\n",
                      mon, s->num_mbwu_mon);
        mon = 0;
    }
    return &mf->mbwu_mons[mon];
}

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

DECLARE_INSTANCE_CHECKER(MPAMMSCMemState, MPAM_MSC_MEM_DEVICE,
                         TYPE_MPAM_MSC_MEM);

DECLARE_INSTANCE_CHECKER(MPAMMSCCacheState, MPAM_MSC_CACHE_DEVICE,
                         TYPE_MPAM_MSC_CACHE);

static uint64_t mpam_msc_read_reg(void *opaque, hwaddr offset,
                                  unsigned size)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(opaque);
    Mpamf *mpamf = &s->mpamf[s->ris];
    uint32_t nrw_part_sel = mpam_get_nrw_id(s);

    switch (offset) {
    case A_MPAMF_IDR:
        switch (size) {
        case 4:
            return mpamf->idr & 0xffffffff;
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
                "MPAM: Accessing IMPL_IDR which isn't suported\n");
            return 0;
        }
        return mpamf->impl_idr;
    case A_MPAMF_CPOR_IDR:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CPOR_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CPOR_IDR with no CPOR support\n");
            return 0;
        }
        return mpamf->cpor_idr;
    case A_MPAMF_CCAP_IDR:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CCAP_IDR with no CCAP support\n");
            return 0;
        }
        return mpamf->ccap_idr;
    case A_MPAMF_MBW_IDR:
        return mpamf->mbw_idr;
    case A_MPAMF_PRI_IDR:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PRI_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of PRI_IDR with no PRI PART support\n");
            return 0;
        }
        return mpamf->pri_idr;
    case A_MPAMF_PARTID_NRW_IDR: {
        return mpamf->partid_nrw_idr;
    }
    case A_MPAMF_MSMON_IDR:
        return mpamf->msmon_idr;
    case A_MPAMF_CSUMON_IDR:
        return mpamf->csumon_idr;
    case A_MPAMF_MBWUMON_IDR:
        return mpamf->mbwumon_idr;
    case A_MSMON_CFG_MON_SEL:
        return s->mon_sel;
    case A_MSMON_CFG_MBWU_FLT:
        return mpam_mbwu_mon(s, mpam_mon_ris(s))->flt;
    case A_MSMON_CFG_MBWU_CTL:
    {
        MpamMbwuMon *m = mpam_mbwu_mon(s, mpam_mon_ris(s));
        uint32_t ctl = m->ctl;

        if (m->oflow) {
            ctl |= R_MSMON_CFG_MBWU_CTL_OFLOW_STATUS_MASK;
        }
        return ctl;
    }
    case A_MPAMF_ERR_MSI_MPAM:
        return mpamf->err_msi_mpam;
    case A_MPAMF_ERR_MSI_ADDR_L:
        return mpamf->err_msi_addr_l;
    case A_MPAMF_ERR_MSI_ADDR_H:
        return mpamf->err_msi_addr_h;
    case A_MPAMF_ERR_MSI_DATA:
        return mpamf->err_msi_data;
    case A_MPAMF_ERR_MSI_ATTR:
        return mpamf->err_msi_attr;
    case A_MPAMF_ECR:
        return mpamf->ecr;
    case A_MPAMF_ESR:
        return mpamf->esr;
    case A_MPAMF_CFG_PART_SEL:
        return mpamf->cfg_part_sel;
    case A_MPAMF_MPAMCFG_INTPARTID: {
        /*
         * The map is only meaningful for resources that implement
         * narrowing; others read as zero.
         */
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW)) {
            return 0;
        }

        return mpamf->cfg_intpartid[s->part_sel];
    }
    case A_MPAMF_MPAMCFG_CMAX:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CMAX with no CCAP support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_cmax;
    case A_MPAMF_MPAMCFG_MBW_MIN:
        return mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_min;
    case A_MPAMF_MPAMCFG_MBW_MAX:
        return mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_max;
    case A_MPAMF_MPAMCFG_PRI:
        return mpamf->per_nrw_id[nrw_part_sel].cfg_pri;
    case A_MPAMF_MPAMCFG_MBW_PROP:
        return mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_prop;
    case A_MPAMF_MPAMCFG_CPBM0...
        (A_MPAMF_MPAMCFG_CPBM0 + ((MPAM_CACHE_PART + 31) / 32 - 1) * 4):
    {
        uint32_t array_offset;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CPOR_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CPBM with no CPOR support\n");
            return 0;
        }
        array_offset = (offset - A_MPAMF_MPAMCFG_CPBM0) / sizeof(uint32_t);
        return mpamf->per_nrw_id[nrw_part_sel].cfg_cpbm[array_offset];
    }
    case A_MPAMF_MPAMCFG_MBW_PBM0...
            (A_MPAMF_MPAMCFG_MBW_PBM0 + ((MPAM_MBW_PART + 31) / 32 - 1) * 4):
    {
        uint32_t array_offset;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBW_PBM with no MBW_PART support\n");
            return 0;
        }
        array_offset = (offset - A_MPAMF_MPAMCFG_MBW_PBM0) / sizeof(uint32_t);
        return mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_pbm[array_offset];
    }
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
    /* Update in the ris setting path */
    Mpamf *mpamf = &s->mpamf[s->ris];
    /* Update if cfg_intpartid being touched */
    uint32_t nrw_part_sel = mpam_get_nrw_id(s);

    switch (offset) {
    case A_MPAMF_CFG_PART_SEL:
        if (value & ~MPAMF_CFG_PART_SEL_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CFG_PART_SEL Mask=%x Value=%lx\n",
                          MPAMF_CFG_PART_SEL_WRITE_MASK, value);
        }
        /* Field matches for all RIS */
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_RIS) &&
            FIELD_EX32(value, MPAMF_CFG_PART_SEL, RIS) != 0) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write of non 0 RIS on MSC with !HAS_RIS\n");
            return;
        }
        if (FIELD_EX32(value, MPAMF_CFG_PART_SEL, RIS) >= s->num_ris) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: RIS %d out of range (num-ris %d)\n",
                          FIELD_EX32(value, MPAMF_CFG_PART_SEL, RIS),
                          s->num_ris);
            return;
        }
        {
            /*
             * IHI 0099 7.2.1: the MSC does not check PARTID_SEL on write
             * (the first IMPLEMENTATION DEFINED behaviour); the selector
             * wraps into the supported PARTID space instead.
             */
            uint32_t limit = FIELD_EX32(value, MPAMF_CFG_PART_SEL, INTERNAL) ?
                             s->num_int_partid : s->num_partid;

            s->part_sel = FIELD_EX32(value, MPAMF_CFG_PART_SEL,
                                     PARTID_SEL) % limit;
        }
        s->ris = FIELD_EX32(value, MPAMF_CFG_PART_SEL, RIS);
        s->internal_part_sel = FIELD_EX32(value, MPAMF_CFG_PART_SEL, INTERNAL);
        mpamf = &s->mpamf[s->ris];
        mpamf->cfg_part_sel = value;
        return;
    case A_MPAMF_MPAMCFG_INTPARTID:
        if (value & ~MPAMF_MPAMCFG_INTPARTID_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to INTPARTID Mask=%x Value=%lx\n",
                          MPAMF_MPAMCFG_INTPARTID_WRITE_MASK, value);
        }
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to INTPARTID when !HAS_PARTID_NRW\n");
            return;
        }

        mpamf->cfg_intpartid[s->part_sel] =
            FIELD_EX32(value, MPAMF_MPAMCFG_INTPARTID, INTPARTID);
        return;
    case A_MPAMF_MPAMCFG_CMAX:
        if (value & ~MPAMF_MPAMCFG_CMAX_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CMAX Mask=%x Value=%lx\n",
                          MPAMF_MPAMCFG_CMAX_WRITE_MASK, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_cmax = value;
        return;
    case A_MPAMF_MPAMCFG_MBW_MIN:
        if (value & ~MPAMF_MPAMCFG_CMIN_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CMAX Mask=%x Value=%lx\n",
                          MPAMF_MPAMCFG_CMIN_WRITE_MASK, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_min = value;
        return;
    case A_MPAMF_MPAMCFG_MBW_MAX:
        if (value & ~MPAMF_MPAMCFG_MBW_MAX_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_MAX Mask=%x Value=%lx\n",
                          MPAMF_MPAMCFG_MBW_MAX_WRITE_MASK, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_max = value;
        return;
    case A_MPAMF_MPAMCFG_PRI:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PRI_PART)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CFG_PRI when !HAS_PRI_PART\n");
        } else {
            if (!FIELD_EX32(mpamf->pri_idr, MPAMF_PRI_IDR, HAS_DSPRI) &&
                FIELD_EX32(value, MPAMF_MPAMCFG_PRI, DSPRI)) {
                qemu_log_mask(LOG_UNIMP,
                              "MPAM: Unexpected write to CFGP_PRI DSPRI when !HAS_DSPRI\n");
            }
            if (!FIELD_EX32(mpamf->pri_idr, MPAMF_PRI_IDR, HAS_INTPRI) &&
                FIELD_EX32(value, MPAMF_MPAMCFG_PRI, INTPRI)) {
                qemu_log_mask(LOG_UNIMP,
                              "MPAM: Unexpected write to CFGP_PRI INTPRI when !HAS_INTPRI\n");
            }
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_pri = value;
        return;
    case A_MPAMF_MPAMCFG_MBW_PROP:
        if (value & ~MPAMF_MPAMCFG_MBW_PROP_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_PROP Mask=%x Value=%lx\n",
                          MPAMF_MPAMCFG_MBW_MAX_WRITE_MASK, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_prop = value;
        return;
    case A_MPAMF_MPAMCFG_CPBM0...
        (A_MPAMF_MPAMCFG_CPBM0 + ((MPAM_CACHE_PART + 31) / 32 - 1) * 4):
    {
        uint32_t array_offset;

        /* TODO: Figure out write mask to check this stays in write bits */
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CPOR_PART)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CPMB when !HAS_CPORT_PART\n");
            return;
        }
        array_offset = (offset - A_MPAMF_MPAMCFG_CPBM0) / sizeof(uint32_t);
        mpamf->per_nrw_id[nrw_part_sel].cfg_cpbm[array_offset] = value;
        return;
    }
    case A_MPAMF_MPAMCFG_MBW_PBM0...
        (A_MPAMF_MPAMCFG_MBW_PBM0 + ((MPAM_MBW_PART + 31) / 32 - 1) * 4):
    {
        uint32_t array_offset;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_PBM when !HAS_MBW_PART\n");
            return;
        }
        /* TODO: Figure out write mask to check this stays in write bits */
        array_offset = (offset - A_MPAMF_MPAMCFG_MBW_PBM0) / sizeof(uint32_t);

        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_pbm[array_offset] = value;
        return;
    }
    case A_MSMON_CFG_MON_SEL:
        s->mon_sel = value & (R_MSMON_CFG_MON_SEL_MON_SEL_MASK |
                              R_MSMON_CFG_MON_SEL_RIS_MASK);
        return;
    case A_MSMON_CFG_MBWU_FLT:
        mpam_mbwu_mon(s, mpam_mon_ris(s))->flt = value;
        return;
    case A_MSMON_CFG_MBWU_CTL:
    {
        MpamMbwuMon *m = mpam_mbwu_mon(s, mpam_mon_ris(s));

        m->ctl = value;
        if (value & R_MSMON_CFG_MBWU_CTL_OFLOW_STATUS_MASK) {
            m->oflow = false;               /* write-one-to-clear */
        }
        return;
    }
    case A_MSMON_CAPT_EVNT:
    case A_MSMON_CFG_CSU_FLT:
    case A_MSMON_CFG_CSU_CTL:
        qemu_log_mask(LOG_UNIMP,
                      "MPAM: cache-usage monitoring not implemented (write %lx)\n",
                      offset);
        return;
    case A_MPAMF_ECR:
        if (value & ~MPAMF_ECR_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to ECR Mask=%x Value=%lx\n",
                          MPAMF_ECR_WRITE_MASK, value);
        }
        mpamf->ecr = value;
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
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void mpam_msc_realize(DeviceState *dev, Error **errp)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);
    int i;

    /* IHI 0099: MPAMF_CFG_PART_SEL.RIS is 4 bits, so at most 16 RIS. */
    if (s->num_ris > 16) {
        error_setg(errp, "num-ris must be <= 16");
        return;
    }
    if (s->num_partid == 0) {
        error_setg(errp, "num-ris must be <= 16");
        return;
    }
    if (s->num_int_partid == 0) {
        s->num_int_partid = s->num_partid;
    }

    s->mpamf = g_new0(Mpamf, s->num_ris);
    for (i = 0; i < s->num_ris; i++) {
        int p;

        s->mpamf[i].per_nrw_id = g_new0(MpamfPerNrwId, s->num_int_partid);
        s->mpamf[i].cfg_intpartid = g_new0(uint32_t, s->num_partid);
        s->mpamf[i].mbwu_mons = g_new0(MpamMbwuMon, s->num_mbwu_mon);
        /*
         * Until resctrl programs the map, wrap each request PARTID onto an
         * internal PARTID so the narrowing indirection is always in range.
         */
        for (p = 0; p < s->num_partid; p++) {
            s->mpamf[i].cfg_intpartid[p] = p % s->num_int_partid;
        }
        /*
         * per_nrw_id is indexed by INTERNAL PARTID: it has num_int_partid
         * entries, not num_partid.
         */
        for (p = 0; p < s->num_int_partid; p++) {
            /* MBW_MAX resets to full bandwidth on real hardware */
            s->mpamf[i].per_nrw_id[p].cfg_mbw_max = 0xffff;
        }
    }

    memory_region_init_io(&s->mr, OBJECT(s), &mpam_msc_ops, s, "mpam_msc",
                          MPAM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
}

static void mpam_msc_mem_realize(DeviceState *dev, Error **errp)
{    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);
    int i;

    mpam_msc_realize(dev, errp);

    for (i = 0; i < s->num_ris; i++) {
        Mpamf *mpamf = &s->mpamf[i];

        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PART_ID_MAX,
                                s->num_partid - 1);
        /* No PCMG for now */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PMG_MAX, 0);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, EXT, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_RIS, s->num_ris > 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, RIS_MAX,
                                  s->num_ris > 1 ? s->num_ris - 1 : 0);
         /* Optional - test with and without */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_ESR, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_EXTD_ESR, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW,
                                s->num_int_partid < s->num_partid);

        /* We won't implement any implementation specific stuff */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_MSMON, 1);
        /* Memory specific bit */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_MBW_PART, 1);

        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, IMPLEMENTER,
                                 MPAM_IIDR_IMPLEMENTER);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, REVISION, 0);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, VARIANT, 0);
        /* FIXME get allocation for this emulation */
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, PRODUCT_ID, 42);

        mpamf->aidr = FIELD_DP64(mpamf->aidr, MPAMF_AIDR, ARCH_MINOR_REV, 1);
        mpamf->aidr = FIELD_DP64(mpamf->aidr, MPAMF_AIDR, ARCH_MAJOR_REV, 1);

        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR, BWA_WD, 16);
        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_MIN, 1);
        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_MAX, 1);
        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_PBM, 1);
        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_PROP, 1);

        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR, WINDWR, 0);
        mpamf->mbw_idr = FIELD_DP32(mpamf->mbw_idr, MPAMF_MBW_IDR,
                                    BWPBM_WD, MPAM_MBW_PART);

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
    int i;

    mpam_msc_realize(dev, errp);

    for (i = 0; i < s->num_ris; i++) {
        Mpamf *mpamf = &s->mpamf[i];

        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PART_ID_MAX,
                                s->num_partid - 1);
        /* No PCMG for now */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, PMG_MAX, 0);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, EXT, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_RIS, s->num_ris > 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, RIS_MAX,
                                  s->num_ris > 1 ? s->num_ris - 1 : 0);

        /* Optional - test with and without */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_ESR, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_EXTD_ESR,
                                s->num_ris > 1);

        /* We won't implement any implementation specific stuff */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, NO_IMPL_MSMON, 1);

        /* Need to implement for RME */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, SP4, 0);

        /* Cache specific bit */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_CPOR_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_PRI_PART, 1);
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_PARTID_NRW,
                                s->num_int_partid < s->num_partid);

        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, IMPLEMENTER,
                                 MPAM_IIDR_IMPLEMENTER);
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, REVISION, 0);
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, VARIANT, 0);
        /* FIXME get allocation for this emulation */
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, PRODUCT_ID, 42);

        mpamf->aidr = FIELD_DP32(mpamf->aidr, MPAMF_AIDR, ARCH_MINOR_REV, 1);
        mpamf->aidr = FIELD_DP32(mpamf->aidr, MPAMF_AIDR, ARCH_MAJOR_REV, 1);

        /* Portion */
        mpamf->cpor_idr = FIELD_DP32(mpamf->cpor_idr, MPAMF_CPOR_IDR, CPBM_WD,
                                     MPAM_CACHE_PART);

        /* Priority */
        mpamf->pri_idr = FIELD_DP32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                    HAS_INTPRI, 1);
        mpamf->pri_idr = FIELD_DP32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                    INTPRI_0_IS_LOW, 1);
        mpamf->pri_idr = FIELD_DP32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                    INTPRI_WD, 2);

        /* Capacity Partitioning */
        mpamf->ccap_idr = FIELD_DP32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                                     HAS_CMAX_SOFTLIM, 1);
        mpamf->ccap_idr = FIELD_DP32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                                     NO_CMAX, 0);
        mpamf->ccap_idr = FIELD_DP32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                                     HAS_CMIN, 1);
        mpamf->ccap_idr = FIELD_DP32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                                     HAS_CASSOC, 1);
        mpamf->ccap_idr = FIELD_DP32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                                     CASSOC_WD, 4); /* Not much flex on this */
        mpamf->ccap_idr = FIELD_DP32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                                     CMAX_WD, 4);

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
    DEFINE_PROP_UINT32("num-mbwu-mon", MPAMMSCState, num_mbwu_mon, 8),
    DEFINE_PROP_UINT64("bw-nominal-bps", MPAMMSCState, bw_nominal_bps,
                       10000000000ULL),  /* 10 GB/s default bandwidth */
    DEFINE_PROP_UINT32("err-gsiv", MPAMMSCState, err_gsiv, 0),
    DEFINE_PROP_UINT32("oflow-gsiv", MPAMMSCState, oflow_gsiv, 0),
    DEFINE_PROP_UINT16("node", MPAMMSCState, node, UINT16_MAX),
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
