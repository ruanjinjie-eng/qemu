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
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "qemu/units.h"
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

REG32(MSMON_CFG_MON_SEL, 0x0800)
    FIELD(MSMON_CFG_MON_SEL, MON_SEL, 0, 16)
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
    FIELD(MSMON_CFG_MBWU_CTL, SUBTYPE, 20, 3)
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
    FIELD(MSMON_CFG_CSU_FLT, PARTID, 0, 16)
    FIELD(MSMON_CFG_CSU_FLT, PMG, 16, 8)
    FIELD(MSMON_CFG_CSU_FLT, XCL, 31, 1)

REG32(MSMON_CFG_CSU_CTL, 0x0818)
    FIELD(MSMON_CFG_CSU_CTL, TYPE, 0, 8)
    FIELD(MSMON_CFG_CSU_CTL, MATCH_PARTID, 16, 1)
    FIELD(MSMON_CFG_CSU_CTL, MATCH_PMG, 17, 1)
    FIELD(MSMON_CFG_CSU_CTL, EN, 31, 1)

REG32(MSMON_CSU, 0x0840)             /* 31-bit value, bit31 NRDY */
REG32(MSMON_CSU_CAPTURE, 0x0848)

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

/* IHI 0099 Table 7-1: MPAMF_ESR.ERR_CODE encodings this model can raise */
#define MPAM_ERRCODE_PARTID_SEL_RANGE    1
#define MPAM_ERRCODE_MSMONCFG_ID_RANGE   3
#define MPAM_ERRCODE_MONITOR_RANGE       5
#define MPAM_ERRCODE_INTPARTID_RANGE     6
#define MPAM_ERRCODE_UNDEF_RIS_PART_SEL  8
#define MPAM_ERRCODE_UNDEF_RIS_MON_SEL  10

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

typedef struct MpamfPerNrwId {
        uint32_t cfg_cpbm[(MPAM_CACHE_PART + 31) / 32];
        uint32_t cfg_mbw_pbm[(MPAM_MBW_PART + 31) / 32];
        uint32_t cfg_pri;
        uint32_t cfg_cmax;
        uint32_t cfg_cmin;
        uint32_t cfg_cassoc;
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

/*
 * One CSU monitor instance.  Occupancy is modelled as a snapshot: the
 * cache capacity scaled by the CPBM portion fraction of the filtered
 * PARTID, so cache-usage readings track the configured CBM.
 */
typedef struct MpamCsuMon {
    uint32_t flt;
    uint32_t ctl;
} MpamCsuMon;

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
    MpamCsuMon *csu_mons;

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
    uint32_t mon_sel;
    struct MemoryRegion mr;
    uint32_t num_partid;
    uint32_t num_int_partid;
    uint8_t num_ris;
    uint32_t num_mbwu_mon;
    uint64_t bw_nominal_bps;
    uint32_t num_csu_mon;
    uint64_t csu_capacity;

    /*
     * Memory-side MSC whose bandwidth control limits the traffic counted
     * by this MSC's MBWU monitors.  Cache MSCs generally implement no
     * bandwidth control of their own, but what leaves the cache for
     * memory is capped by the memory-side MSC.
     */
    DeviceState *bw_source;

    /*
     * MPAMF_ECR / MPAMF_ESR are single per-MSC registers (IHI 0099 4.3),
     * independent of the RIS selection.  The 64-bit ESR records the RIS
     * the error was detected against in its top half.
     */
    uint32_t ecr;
    uint64_t esr;

    /* Wired interrupt outputs: [0] error, [1] overflow (level) */
    qemu_irq err_irq;
    qemu_irq oflow_irq;

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
    /* Array size for migration (derived from num_ris). */
    uint32_t num_ris_vm;

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

/*
 * Keep the per-RIS copies in Mpamf (exported through the QMP/PPTT path)
 * in sync with the MSC-level ECR/ESR view the MMIO handlers use.
 */
static void mpam_msc_sync_esr_ecr(MPAMMSCState *s)
{
    for (int i = 0; i < s->num_ris; i++) {
        s->mpamf[i].ecr = s->ecr;
        s->mpamf[i].esr = (uint32_t)s->esr;
    }
}

/*
 * IHI 0099 8.2: the MPAM error interrupt is level-sensitive; it is active
 * while MPAMF_ESR.ERR_CODE is non-zero and MPAMF_ECR.INTEN is set, and is
 * deactivated by software writing ERRCODE to zero.
 */
static void mpam_msc_update_err_irq(MPAMMSCState *s)
{
    bool int_en = FIELD_EX32(s->ecr, MPAMF_ECR, INTEN);
    bool active = FIELD_EX64(s->esr, MPAMF_ESR, ERR_CODE) != 0;

    qemu_set_irq(s->err_irq, int_en && active);
}

/*
 * Record an error in MPAMF_ESR and raise the error interrupt if enabled.
 * PARTID_MON also carries a selector value (e.g. MON_SEL) for the errors
 * where Table 7-1 lists "MON_SEL and RIS" as the fields in effect.
 */
static void mpam_msc_inject_error(MPAMMSCState *s, uint32_t errcode,
                                  uint16_t partid_mon, uint8_t pmg,
                                  uint8_t ris)
{
    uint64_t esr = 0;

    if (FIELD_EX64(s->esr, MPAMF_ESR, ERR_CODE)) {
        esr = FIELD_DP64(esr, MPAMF_ESR, OVRWR, 1);
    }
    esr = FIELD_DP64(esr, MPAMF_ESR, PARID_MON, partid_mon);
    esr = FIELD_DP64(esr, MPAMF_ESR, PMG, pmg);
    esr = FIELD_DP64(esr, MPAMF_ESR, ERR_CODE, errcode);
    esr = FIELD_DP64(esr, MPAMF_ESR, RIS, ris);
    s->esr = esr;
    mpam_msc_sync_esr_ecr(s);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "MPAM: error 0x%x partid/mon %u pmg %u ris %u\n",
                  errcode, partid_mon, pmg, ris);
    mpam_msc_update_err_irq(s);
}

/*
 * IHI 0099 8.3: the overflow interrupt is signalled while any monitor has
 * latched OFLOW_STATUS with OFLOW_INTR set; clearing OFLOW_STATUS of all
 * overflowed monitors (W1C) deasserts it.
 */
static void mpam_msc_update_oflow_irq(MPAMMSCState *s)
{
    bool active = false;
    int m;

    for (int i = 0; i < s->num_ris && !active; i++) {
        for (m = 0; m < s->num_mbwu_mon; m++) {
            MpamMbwuMon *mon = &s->mpamf[i].mbwu_mons[m];

            if (mon->oflow &&
                FIELD_EX32(mon->ctl, MSMON_CFG_MBWU_CTL, OFLOW_INTR)) {
                active = true;
                break;
            }
        }
    }
    qemu_set_irq(s->oflow_irq, active);
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

/*
 * Lazily advance the monitor's counter.  While enabled it accumulates
 * the nominal bandwidth scaled by the MBW_MAX fraction of the PARTID
 * the filter selects, so monitor readings track the configured
 * bandwidth control.
 */
DECLARE_INSTANCE_CHECKER(MPAMMSCState, MPAM_MSC_DEVICE, TYPE_MPAM_MSC);

static uint64_t mpam_mbwu_value(MPAMMSCState *s, Mpamf *mf, MpamMbwuMon *m)
{
    uint64_t v = m->base;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delta = now - m->t0;

    if (FIELD_EX32(m->ctl, MSMON_CFG_MBWU_CTL, EN) && delta > 0) {
        /*
         * Scale the configured bandwidth by the partition fraction.
         * Convert to MB/s first so the muldiv64() intermediate stays
         * in range; the ns -> bytes conversion below accounts for it.
         */
        uint64_t rate_mbs = s->bw_nominal_bps / 1000000;

        if (FIELD_EX32(m->ctl, MSMON_CFG_MBWU_CTL, MATCH_PARTID)) {
            uint32_t partid = FIELD_EX32(m->flt, MSMON_CFG_MBWU_FLT,
                                         PARTID);
            MPAMMSCState *cs = s;
            Mpamf *cmf = mf;
            MpamfPerNrwId *cfg;
            uint64_t frac_num, frac_den;
            uint32_t nrw;

            /*
             * A memory-side MSC caps the traffic the L3 cache sends to
             * memory, but the cache MSC may implement no bandwidth
             * control of its own.  In that case follow the bandwidth
             * control of the linked memory-side MSC.
             */
            if (!FIELD_EX32(mf->idr, MPAMF_IDR, HAS_MBW_PART) &&
                s->bw_source) {
                cs = MPAM_MSC_DEVICE(s->bw_source);
                cmf = &cs->mpamf[0];
            }

            /*
             * The board gives both MSCs the same PARTID geometry, so the
             * filter PARTID resolves through the memory-side map too.
             */
            nrw = mpam_nrw_id_for(cs, cmf, partid);
            cfg = &cmf->per_nrw_id[nrw];

            /*
             * The configured bandwidth comes from either MBW_MAX or,
             * on classes that advertise MBW_PBM, the PBM bitmap
             * (popcount of the programmed bits over its width).
             *
             * Both can be programmed at the same time: the portion
             * bitmap grants bandwidth and MBW_MAX caps it.  Software
             * that uses only one leaves the other at full bandwidth, so
             * use the more restrictive of the two.
             */
            if (cfg->cfg_mbw_pbm[0]) {
                frac_num = __builtin_popcount(cfg->cfg_mbw_pbm[0] &
                                     ((1u << MPAM_MBW_PART) - 1));
                frac_den = MPAM_MBW_PART;
            } else {
                frac_num = 0xffff;
                frac_den = 0xffff;
            }

            /*
             * MBW_MAX is a 16-bit fraction; 0xffff = full bandwidth.
             * A soft limit (HARDLIM == 0) only constrains allocation
             * when the resource is contended, which this model does not
             * simulate, so it does not throttle the monitored rate.
             */
            if (FIELD_EX32(cfg->cfg_mbw_max, MPAMF_MPAMCFG_MBW_MAX,
                           HARDLIM) &&
                (cfg->cfg_mbw_max & 0xffff) * frac_den <
                frac_num * 0xffff) {
                frac_num = cfg->cfg_mbw_max & 0xffff;
                frac_den = 0xffff;
            }
            rate_mbs = muldiv64(rate_mbs, frac_num, frac_den);
        }

        /* rate_mbs is MB/s and delta is ns: ns * MB/s / 1000 = bytes. */
        v += muldiv64(delta, rate_mbs, 1000);
        /* 48-bit counter wraps (MSMON_MBWU_L is 48 bits of value). */
        if (v > (1ULL << 48)) {
            if (!m->oflow) {
                m->oflow = true;
                mpam_msc_update_oflow_irq(s);
            }
            v &= (1ULL << 48) - 1;
        }
    }
    m->base = v;
    m->t0 = now;

    return v;
}

static MpamCsuMon *mpam_csu_mon(MPAMMSCState *s, Mpamf *mf)
{
    unsigned mon = FIELD_EX32(s->mon_sel, MSMON_CFG_MON_SEL, MON_SEL);

    if (mon >= s->num_csu_mon) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPAM: MON_SEL %d out of range (num-csu-mon %d)\n",
                      mon, s->num_csu_mon);
        mon = 0;
    }
    return &mf->csu_mons[mon];
}

/*
 * Snapshot occupancy: the cache capacity scaled by the fraction of CPBM
 * portions the filtered PARTID is allowed to occupy.
 */
static uint32_t mpam_csu_value(MPAMMSCState *s, Mpamf *mf, MpamCsuMon *m)
{
    if (FIELD_EX32(m->ctl, MSMON_CFG_CSU_CTL, EN) &&
        FIELD_EX32(m->ctl, MSMON_CFG_CSU_CTL, MATCH_PARTID)) {
        uint32_t partid = FIELD_EX32(m->flt, MSMON_CFG_CSU_FLT, PARTID);
        uint32_t nrw = mpam_nrw_id_for(s, mf, partid);
        uint32_t cpbm = mf->per_nrw_id[nrw].cfg_cpbm[0];

        return muldiv64(s->csu_capacity, ctpop32(cpbm), MPAM_CACHE_PART);
    }
    return 0;
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

/*
 * IHI 0099 9.4: a width-reported fraction field occupies the most
 * significant 'wd' bits of its 16-bit field; the remaining low bits are
 * RAZ/WI.  Returns the mask of bits software is allowed to set.
 */
static uint32_t mpam_fracwd_mask(uint32_t wd)
{
    if (wd >= 16) {
        return 0xffffu;
    }
    return ~((1u << (16 - wd)) - 1) & 0xffffu;
}

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

static const VMStateDescription vmstate_mpam_mbwu_mon = {
    .name = "mpam/mbwu-mon",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(flt, MpamMbwuMon),
        VMSTATE_UINT32(ctl, MpamMbwuMon),
        VMSTATE_UINT64(base, MpamMbwuMon),
        VMSTATE_INT64(t0, MpamMbwuMon),
        VMSTATE_BOOL(oflow, MpamMbwuMon),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_mpam_csu_mon = {
    .name = "mpam/csu-mon",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(flt, MpamCsuMon),
        VMSTATE_UINT32(ctl, MpamCsuMon),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_mpamf_per_nrw_id = {
    .name = "mpam/per-nrw-id",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(cfg_cpbm, MpamfPerNrwId,
                             (MPAM_CACHE_PART + 31) / 32),
        VMSTATE_UINT32_ARRAY(cfg_mbw_pbm, MpamfPerNrwId,
                             (MPAM_MBW_PART + 31) / 32),
        VMSTATE_UINT32(cfg_pri, MpamfPerNrwId),
        VMSTATE_UINT32(cfg_cmax, MpamfPerNrwId),
        VMSTATE_UINT32(cfg_cmin, MpamfPerNrwId),
        VMSTATE_UINT32(cfg_cassoc, MpamfPerNrwId),
        VMSTATE_UINT32(cfg_mbw_min, MpamfPerNrwId),
        VMSTATE_UINT32(cfg_mbw_max, MpamfPerNrwId),
        VMSTATE_UINT32(cfg_mbw_prop, MpamfPerNrwId),
        VMSTATE_END_OF_LIST()
    },
};

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
        VMSTATE_VARRAY_UINT32(cfg_intpartid, Mpamf, num_partid_vm, 0,
                              vmstate_info_uint32, uint32_t),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(per_nrw_id, Mpamf,
                                             num_int_partid_vm,
                                             vmstate_mpamf_per_nrw_id,
                                             MpamfPerNrwId),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(mbwu_mons, Mpamf,
                                             num_mbwu_mon_vm,
                                             vmstate_mpam_mbwu_mon,
                                             MpamMbwuMon),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(csu_mons, Mpamf,
                                             num_csu_mon_vm,
                                             vmstate_mpam_csu_mon,
                                             MpamCsuMon),
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
        VMSTATE_UINT32(mon_sel, MPAMMSCState),
        VMSTATE_UINT32(ecr, MPAMMSCState),
        VMSTATE_UINT64(esr, MPAMMSCState),
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
    uint32_t nrw_part_sel = mpam_get_nrw_id(s);

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
    case A_MSMON_CFG_MON_SEL:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MSMON)) {
            return 0;
        }
        return s->mon_sel;
    case A_MSMON_CFG_CSU_FLT:
        if (!mpam_has_csu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CSU_FLT with no CSU monitor\n");
            return 0;
        }
        return mpam_csu_mon(s, mpam_mon_ris(s))->flt;
    case A_MSMON_CFG_CSU_CTL:
        if (!mpam_has_csu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CSU_CTL with no CSU monitor\n");
            return 0;
        }
        return mpam_csu_mon(s, mpam_mon_ris(s))->ctl;
    case A_MSMON_CSU ... (A_MSMON_CSU + 3):
    case A_MSMON_CSU_CAPTURE ... (A_MSMON_CSU_CAPTURE + 3):
    {
        if (!mpam_has_csu_mon(mpamf)) {
            return 0;
        }
        uint32_t v = mpam_csu_value(s, mpam_mon_ris(s),
                                    mpam_csu_mon(s, mpam_mon_ris(s)));
        hwaddr off = (offset >= A_MSMON_CSU_CAPTURE) ?
                     offset - A_MSMON_CSU_CAPTURE : offset - A_MSMON_CSU;

        /* NRDY (bit 31) is never set: the snapshot is always ready */
        return extract32(v, off * 8, 32 - off * 8);
    }
    case A_MSMON_CFG_MBWU_FLT:
        if (!mpam_has_mbwu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBWU_FLT with no MBWU monitor\n");
            return 0;
        }
        return mpam_mbwu_mon(s, mpam_mon_ris(s))->flt;
    case A_MSMON_CFG_MBWU_CTL:
    {
        MpamMbwuMon *m;

        if (!mpam_has_mbwu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBWU_CTL with no MBWU monitor\n");
            return 0;
        }
        m = mpam_mbwu_mon(s, mpam_mon_ris(s));
        {
            uint32_t ctl = m->ctl;

            if (m->oflow) {
                ctl |= R_MSMON_CFG_MBWU_CTL_OFLOW_STATUS_MASK;
            }
            return ctl;
        }
    }
    case A_MSMON_MBWU ... (A_MSMON_MBWU + 3):
    {
        uint64_t v;

        if (!mpam_has_mbwu_mon(mpamf)) {
            return 0;
        }
        v = mpam_mbwu_value(s, mpam_mon_ris(s),
                            mpam_mbwu_mon(s, mpam_mon_ris(s)));

        return extract64(v, (offset - A_MSMON_MBWU) * 8, 32);
    }
    case A_MSMON_MBWU_L ... (A_MSMON_MBWU_L + 7):
    {
        uint64_t v;

        if (!mpam_has_mbwu_mon(mpamf)) {
            return 0;
        }
        v = mpam_mbwu_value(s, mpam_mon_ris(s),
                            mpam_mbwu_mon(s, mpam_mon_ris(s)));

        return extract64(v, (offset - A_MSMON_MBWU_L) * 8, 32);
    }
    case A_MSMON_MBWU_CAPTURE ... (A_MSMON_MBWU_CAPTURE + 3):
    {
        uint64_t v;

        if (!mpam_has_mbwu_mon(mpamf)) {
            return 0;
        }
        v = mpam_mbwu_value(s, mpam_mon_ris(s),
                            mpam_mbwu_mon(s, mpam_mon_ris(s)));

        return (uint32_t)v;
    }
    case A_MSMON_MBWU_CAPTURE_L ... (A_MSMON_MBWU_CAPTURE_L + 7):
    {
        uint64_t v;

        if (!mpam_has_mbwu_mon(mpamf)) {
            return 0;
        }
        v = mpam_mbwu_value(s, mpam_mon_ris(s),
                            mpam_mbwu_mon(s, mpam_mon_ris(s)));

        return extract64(v, (offset - A_MSMON_MBWU_CAPTURE_L) * 8, 32);
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
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_ESR)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected read of ECR with no ESR support\n");
            return 0;
        }
        return s->ecr;
    case A_MPAMF_ESR:
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_ESR)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected read of ESR with no ESR support\n");
            return 0;
        }
        return s->esr;
    case A_MPAMF_ESR + 0x4:
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_ESR) ||
            !FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_EXTD_ESR)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected read of extended ESR\n");
            return 0;
        }
        return s->esr >> 32;
    case A_MPAMCFG_PART_SEL:
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
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART) ||
            FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, NO_CMAX)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CMAX with no CMAX support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_cmax;
    case A_MPAMF_MPAMCFG_CMIN:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART) ||
            !FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, HAS_CMIN)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CMIN with no CMIN support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_cmin;
    case A_MPAMF_MPAMCFG_CASSOC:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART) ||
            !FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, HAS_CASSOC)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CASSOC with no CASSOC support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_cassoc;
    case A_MPAMF_MPAMCFG_MBW_MIN:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_MIN)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBW_MIN with no MBW_MIN support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_min;
    case A_MPAMF_MPAMCFG_MBW_MAX:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_MAX)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBW_MAX with no MBW_MAX support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_max;
    case A_MPAMF_MPAMCFG_PRI:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PRI_PART)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of CFG_PRI with no PRI PART support\n");
            return 0;
        }
        return mpamf->per_nrw_id[nrw_part_sel].cfg_pri;
    case A_MPAMF_MPAMCFG_MBW_PROP:
        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_PROP)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBW_PROP with no MBW_PROP support\n");
            return 0;
        }
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

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_PBM)) {
            qemu_log_mask(LOG_UNIMP,
                "MPAM: Unexpected read of MBW_PBM with no MBW_PBM support\n");
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
    Mpamf *mpamf = &s->mpamf[s->ris];
    uint32_t nrw_part_sel = mpam_get_nrw_id(s);

    switch (offset) {
    case A_MPAMCFG_PART_SEL: {
        uint32_t ris = FIELD_EX32(value, MPAMCFG_PART_SEL, RIS);

        if (value & ~MPAMCFG_PART_SEL_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CFG_PART_SEL Mask=%x Value=%lx\n",
                          MPAMCFG_PART_SEL_WRITE_MASK, value);
        }
        /*
         * Field matches for all RIS. The range check subsumes the !HAS_RIS
         * case: such an MSC implements only RIS 0, so any nonzero RIS is
         * undefined (IHI 0099 Table 7-1: Undefined_RIS_PART_SEL).
         */
        if (ris >= s->num_ris) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: RIS %d out of range (num-ris %d)\n",
                          ris, s->num_ris);
            mpam_msc_inject_error(s, MPAM_ERRCODE_UNDEF_RIS_PART_SEL,
                                  FIELD_EX32(value, MPAMCFG_PART_SEL,
                                             PARTID_SEL),
                                  0, ris);
            return;
        }
        {
            /*
             * IHI 0099 7.2.1: the MSC does not check PARTID_SEL on write
             * (the first IMPLEMENTATION DEFINED behaviour); the selector
             * wraps into the supported PARTID space instead.
             */
            uint32_t limit = FIELD_EX32(value, MPAMCFG_PART_SEL, INTERNAL) ?
                             s->num_int_partid : s->num_partid;

            s->part_sel = FIELD_EX32(value, MPAMCFG_PART_SEL, PARTID_SEL) % limit;
        }
        s->ris = ris;
        s->internal_part_sel = FIELD_EX32(value, MPAMCFG_PART_SEL, INTERNAL);
        mpamf = &s->mpamf[s->ris];
        mpamf->cfg_part_sel = value & MPAMCFG_PART_SEL_WRITE_MASK;
        return;
    }
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
        /*
         * Table 7-1: writing INTERNAL as zero, or an out-of-range
         * intPARTID, raises intPARTID_Range and is not written.
         */
        if (!FIELD_EX32(value, MPAMF_MPAMCFG_INTPARTID, INTERNAL) ||
            FIELD_EX32(value, MPAMF_MPAMCFG_INTPARTID, INTPARTID) >=
                s->num_int_partid) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: INTPARTID write invalid (value %x, max %d)\n",
                          FIELD_EX32(value, MPAMF_MPAMCFG_INTPARTID, INTPARTID),
                          s->num_int_partid - 1);
            mpam_msc_inject_error(s, MPAM_ERRCODE_INTPARTID_RANGE,
                                  FIELD_EX32(value, MPAMF_MPAMCFG_INTPARTID,
                                             INTPARTID),
                                  0, s->ris);
            return;
        }

        mpamf->cfg_intpartid[s->part_sel] =
            FIELD_EX32(value, MPAMF_MPAMCFG_INTPARTID, INTPARTID);
        return;
    case A_MPAMF_MPAMCFG_CMAX:
    {
        uint32_t wd, valid;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART) ||
            FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, NO_CMAX)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CMAX when not supported\n");
            return;
        }
        wd = FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, CMAX_WD);
        valid = mpam_fracwd_mask(wd);
        if (FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR,
                       HAS_CMAX_SOFTLIM)) {
            valid |= R_MPAMF_MPAMCFG_CMAX_SOFTLIM_MASK;
        }
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CMAX Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_cmax = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_CMIN:
    {
        uint32_t wd, valid;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART) ||
            !FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, HAS_CMIN)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CMIN when not supported\n");
            return;
        }
        /* CMIN uses the same implemented width as CMAX (IHI 0099 5.2.4). */
        wd = FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, CMAX_WD);
        valid = mpam_fracwd_mask(wd);
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CMIN Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_cmin = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_CASSOC:
    {
        uint32_t wd, valid;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CCAP_PART) ||
            !FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, HAS_CASSOC)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CASSOC when not supported\n");
            return;
        }
        wd = FIELD_EX32(mpamf->ccap_idr, MPAMF_CCAP_IDR, CASSOC_WD);
        valid = mpam_fracwd_mask(wd);
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CASSOC Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_cassoc = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_MBW_MIN:
    {
        uint32_t wd, valid;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_MIN)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_MIN when not supported\n");
            return;
        }
        wd = FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, BWA_WD);
        valid = mpam_fracwd_mask(wd);
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_MIN Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_min = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_MBW_MAX:
    {
        uint32_t wd, valid;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_MAX)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_MAX when not supported\n");
            return;
        }
        wd = FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, BWA_WD);
        valid = mpam_fracwd_mask(wd) |
                R_MPAMF_MPAMCFG_MBW_MAX_HARDLIM_MASK;
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_MAX Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_max = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_PRI:
    {
        uint32_t valid = 0;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_PRI_PART)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CFG_PRI when !HAS_PRI_PART\n");
            return;
        }
        /*
         * IHI 0099 9.6 MPAMCFG_PRI: INTPRI and DSPRI use their
         * least-significant *pri_wd bits, unlike the fraction fields.
         */
        if (FIELD_EX32(mpamf->pri_idr, MPAMF_PRI_IDR, HAS_INTPRI)) {
            uint32_t wd = FIELD_EX32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                     INTPRI_WD);

            valid |= wd >= 16 ? 0xffffu : ((1u << wd) - 1);
        }
        if (FIELD_EX32(mpamf->pri_idr, MPAMF_PRI_IDR, HAS_DSPRI)) {
            uint32_t wd = FIELD_EX32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                     DSPRI_WD);
            uint32_t mask = wd >= 16 ? 0xffffu : ((1u << wd) - 1);

            valid |= mask << 16;
        }
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CFG_PRI Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_pri = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_MBW_PROP:
    {
        uint32_t wd, valid;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_PROP)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_PROP when not supported\n");
            return;
        }
        wd = FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, BWA_WD);
        valid = mpam_fracwd_mask(wd) |
                R_MPAMF_MPAMCFG_MBW_PROP_EN_MASK;
        if (value & ~valid) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_PROP Mask=%x Value=%lx\n",
                          valid, value);
        }
        mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_prop = value & valid;
        return;
    }
    case A_MPAMF_MPAMCFG_CPBM0...
        (A_MPAMF_MPAMCFG_CPBM0 + ((MPAM_CACHE_PART + 31) / 32 - 1) * 4):
    {
        uint32_t array_offset;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_CPOR_PART)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CPBM when !HAS_CPOR_PART\n");
            return;
        }
        array_offset = (offset - A_MPAMF_MPAMCFG_CPBM0) / sizeof(uint32_t);
        {
            uint32_t cpbm_wd = FIELD_EX32(mpamf->cpor_idr,
                                          MPAMF_CPOR_IDR, CPBM_WD);
            uint32_t bit = array_offset * 32;
            uint32_t mask;

            if (bit >= cpbm_wd) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "MPAM: CPBM%d out of range (CPBM_WD %u)\n",
                              array_offset, cpbm_wd);
                return;
            }
            mask = cpbm_wd - bit >= 32 ? UINT32_MAX :
                   (uint32_t)MAKE_64BIT_MASK(0, cpbm_wd - bit);
            mpamf->per_nrw_id[nrw_part_sel].cfg_cpbm[array_offset] =
                value & mask;
        }
        return;
    }
    case A_MPAMF_MPAMCFG_MBW_PBM0...
        (A_MPAMF_MPAMCFG_MBW_PBM0 + ((MPAM_MBW_PART + 31) / 32 - 1) * 4):
    {
        uint32_t array_offset;

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MBW_PART) ||
            !FIELD_EX32(mpamf->mbw_idr, MPAMF_MBW_IDR, HAS_PBM)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBW_PBM when not supported\n");
            return;
        }
        array_offset = (offset - A_MPAMF_MPAMCFG_MBW_PBM0) / sizeof(uint32_t);
        {
            uint32_t wd = FIELD_EX32(mpamf->mbw_idr,
                                     MPAMF_MBW_IDR, BWPBM_WD);
            uint32_t bit = array_offset * 32;
            uint32_t mask;

            if (bit >= wd) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "MPAM: MBW_PBM%d out of range (BWPBM_WD %u)\n",
                              array_offset, wd);
                return;
            }
            mask = wd - bit >= 32 ? UINT32_MAX :
                   (uint32_t)MAKE_64BIT_MASK(0, wd - bit);
            mpamf->per_nrw_id[nrw_part_sel].cfg_mbw_pbm[array_offset] =
                value & mask;
        }
        return;
    }
    case A_MSMON_CFG_MON_SEL:
    {
        uint32_t mon = FIELD_EX32(value, MSMON_CFG_MON_SEL, MON_SEL);
        uint32_t ris = FIELD_EX32(value, MSMON_CFG_MON_SEL, RIS);
        uint32_t num_mon = MAX(s->num_csu_mon, s->num_mbwu_mon);

        if (!FIELD_EX32(mpamf->idr, MPAMF_IDR, HAS_MSMON)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MON_SEL with no MSMON\n");
            return;
        }

        /*
         * Check-and-reject (IHI 0099 7.2.9): RIS selects a nonexistent
         * resource instance or MON_SEL exceeds every monitor type this
         * MSC implements.
         */
        if (ris >= s->num_ris) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: MON_SEL RIS %d out of range (num-ris %d)\n",
                          ris, s->num_ris);
            mpam_msc_inject_error(s, MPAM_ERRCODE_UNDEF_RIS_MON_SEL,
                                  mon, 0, ris);
            return;
        }
        if (mon >= num_mon) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: MON_SEL %d out of range (max %d)\n",
                          mon, num_mon - 1);
            mpam_msc_inject_error(s, MPAM_ERRCODE_MONITOR_RANGE,
                                  mon, 0, ris);
            return;
        }
        s->mon_sel = value & (R_MSMON_CFG_MON_SEL_MON_SEL_MASK |
                              R_MSMON_CFG_MON_SEL_RIS_MASK);
        return;
    }
    case A_MSMON_CFG_MBWU_FLT:
    {
        uint32_t partid = FIELD_EX32(value, MSMON_CFG_MBWU_FLT, PARTID);

        if (!mpam_has_mbwu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBWU_FLT with no MBWU monitor\n");
            return;
        }
        if (partid >= s->num_partid) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: MBWU_FLT PARTID %d out of range\n", partid);
            mpam_msc_inject_error(s, MPAM_ERRCODE_MSMONCFG_ID_RANGE,
                                  partid,
                                  FIELD_EX32(value, MSMON_CFG_MBWU_FLT, PMG),
                                  FIELD_EX32(s->mon_sel, MSMON_CFG_MON_SEL,
                                             RIS));
            return;
        }
        {
            uint32_t mask = R_MSMON_CFG_MBWU_FLT_PARTID_MASK |
                            R_MSMON_CFG_MBWU_FLT_PMG_MASK;

            if (FIELD_EX32(mpamf->mbwumon_idr, MPAMF_MBWUMON_IDR,
                           HAS_RWBW)) {
                mask |= R_MSMON_CFG_MBWU_FLT_RWBW_MASK;
            }
            mpam_mbwu_mon(s, mpam_mon_ris(s))->flt = value & mask;
        }
        return;
    }
    case A_MSMON_CFG_MBWU_CTL:
    {
        MpamMbwuMon *m;

        if (!mpam_has_mbwu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to MBWU_CTL with no MBWU monitor\n");
            return;
        }
        m = mpam_mbwu_mon(s, mpam_mon_ris(s));

        m->ctl = value & (R_MSMON_CFG_MBWU_CTL_TYPE_MASK |
                          R_MSMON_CFG_MBWU_CTL_MATCH_PARTID_MASK |
                          R_MSMON_CFG_MBWU_CTL_MATCH_PMG_MASK |
                          R_MSMON_CFG_MBWU_CTL_SCLEN_MASK |
                          R_MSMON_CFG_MBWU_CTL_SUBTYPE_MASK |
                          R_MSMON_CFG_MBWU_CTL_OFLOW_FRZ_MASK |
                          R_MSMON_CFG_MBWU_CTL_OFLOW_INTR_MASK |
                          R_MSMON_CFG_MBWU_CTL_CAPT_RESET_MASK |
                          R_MSMON_CFG_MBWU_CTL_CAPT_EVNT_MASK |
                          R_MSMON_CFG_MBWU_CTL_EN_MASK);
        if (value & R_MSMON_CFG_MBWU_CTL_OFLOW_STATUS_MASK) {
            m->oflow = false;               /* write-one-to-clear */
        }
        mpam_msc_update_oflow_irq(s);
        return;
    }
    case A_MSMON_MBWU:
    case A_MSMON_MBWU_L:
    case A_MSMON_MBWU_CAPTURE:
    case A_MSMON_MBWU_CAPTURE_L:
    {
        MpamMbwuMon *m;

        if (!mpam_has_mbwu_mon(mpamf)) {
            return;
        }
        m = mpam_mbwu_mon(s, mpam_mon_ris(s));

        m->base = 0;
        m->t0 = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        return;
    }
    case A_MSMON_CFG_CSU_FLT:
    {
        uint32_t partid = FIELD_EX32(value, MSMON_CFG_CSU_FLT, PARTID);

        if (!mpam_has_csu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CSU_FLT with no CSU monitor\n");
            return;
        }
        if (partid >= s->num_partid) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "MPAM: CSU_FLT PARTID %d out of range\n", partid);
            mpam_msc_inject_error(s, MPAM_ERRCODE_MSMONCFG_ID_RANGE,
                                  partid,
                                  FIELD_EX32(value, MSMON_CFG_CSU_FLT, PMG),
                                  FIELD_EX32(s->mon_sel, MSMON_CFG_MON_SEL,
                                             RIS));
            return;
        }
        mpam_csu_mon(s, mpam_mon_ris(s))->flt =
            value & (R_MSMON_CFG_CSU_FLT_PARTID_MASK |
                     R_MSMON_CFG_CSU_FLT_PMG_MASK |
                     R_MSMON_CFG_CSU_FLT_XCL_MASK);
        return;
    }
    case A_MSMON_CFG_CSU_CTL:
        if (!mpam_has_csu_mon(mpamf)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to CSU_CTL with no CSU monitor\n");
            return;
        }
        mpam_csu_mon(s, mpam_mon_ris(s))->ctl =
            value & (R_MSMON_CFG_CSU_CTL_TYPE_MASK |
                     R_MSMON_CFG_CSU_CTL_MATCH_PARTID_MASK |
                     R_MSMON_CFG_CSU_CTL_MATCH_PMG_MASK |
                     R_MSMON_CFG_CSU_CTL_EN_MASK);
        return;
    case A_MSMON_CSU:
    case A_MSMON_CSU_CAPTURE:
        /*
         * These are probed with MSMON___NRDY to determine whether the
         * counter needs polling.  Ignoring the write leaves the snapshot
         * permanently ready, which is what the emulation models.
         */
        return;
    case A_MSMON_CAPT_EVNT:
        if (!mpam_has_csu_mon(mpamf) && !mpam_has_mbwu_mon(mpamf)) {
            return;
        }
        qemu_log_mask(LOG_UNIMP,
                      "MPAM: capture events not implemented (write %lx)\n",
                      offset);
        return;
    case A_MPAMF_ECR:
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_ESR)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to ECR with no ESR support\n");
            return;
        }
        if (value & ~MPAMF_ECR_WRITE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to ECR Mask=%x Value=%lx\n",
                          MPAMF_ECR_WRITE_MASK, value);
        }
        s->ecr = value & MPAMF_ECR_WRITE_MASK;
        mpam_msc_sync_esr_ecr(s);
        mpam_msc_update_err_irq(s);
        return;
    case A_MPAMF_ESR:
        /*
         * Software clears an error by writing ERRCODE as zero; writing a
         * non-zero ERRCODE activates the level error interrupt (8.2).
         * Bits [30:28] are reserved.
         */
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_ESR)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to ESR with no ESR support\n");
            return;
        }
        s->esr = deposit64(s->esr, 0, 32, value & ~0x70000000u);
        mpam_msc_sync_esr_ecr(s);
        mpam_msc_update_err_irq(s);
        return;
    case A_MPAMF_ESR + 0x4:
        if (!FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_ESR) ||
            !FIELD_EX64(mpamf->idr, MPAMF_IDR, HAS_EXTD_ESR)) {
            qemu_log_mask(LOG_UNIMP,
                          "MPAM: Unexpected write to extended ESR\n");
            return;
        }
        /* Bits [63:36] are reserved; [35:32] hold RIS. */
        s->esr = deposit64(s->esr, 32, 32, value & ~0xfffffff0u);
        mpam_msc_sync_esr_ecr(s);
        mpam_msc_update_err_irq(s);
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
 * Power-on state of the MSC, shared between realize and reset: ID narrowing
 * map identity-wrapped, partition config at reset values, monitors off.
 */
static void mpam_msc_state_init(MPAMMSCState *s)
{
    s->ris = 0;
    s->part_sel = 0;
    s->internal_part_sel = false;
    s->mon_sel = 0;
    s->ecr = 0;
    s->esr = 0;

    for (int i = 0; i < s->num_ris; i++) {
        Mpamf *mpamf = &s->mpamf[i];
        mpamf->ecr = 0;
        mpamf->esr = 0;
        memset(mpamf->per_nrw_id, 0,
               sizeof(MpamfPerNrwId) * s->num_int_partid);
        memset(mpamf->cfg_intpartid, 0,
               sizeof(uint32_t) * s->num_partid);
        memset(mpamf->mbwu_mons, 0,
               sizeof(MpamMbwuMon) * s->num_mbwu_mon);
        memset(mpamf->csu_mons, 0, sizeof(MpamCsuMon) * s->num_csu_mon);

        /*
         * t0 is a virtual-clock timestamp; zero is a valid value (the
         * clock starts at zero), so initialise it explicitly rather than
         * treating zero as "unset" on the first read.
         */
        for (int m = 0; m < s->num_mbwu_mon; m++) {
            mpamf->mbwu_mons[m].t0 =
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        /*
         * Until software programs the map, wrap each request PARTID onto an
         * internal PARTID so the narrowing indirection is always in range.
         */
        for (int p = 0; p < s->num_partid; p++) {
            mpamf->cfg_intpartid[p] = p % s->num_int_partid;
        }
        /*
         * per_nrw_id is indexed by INTERNAL PARTID: it has
         * num_int_partid entries, not num_partid.  Writing it with the
         * request-PARTID count overflowed the allocation by 224 entries
         * (malloc(): corrupted top size once the struct grew).
         */
        for (int p = 0; p < s->num_int_partid; p++) {
            /* MBW_MAX resets to full bandwidth on real hardware */
            mpamf->per_nrw_id[p].cfg_mbw_max = 0xffff;
        }
    }
}

static void mpam_msc_reset(DeviceState *dev)
{
    MPAMMSCState *s = MPAM_MSC_DEVICE(dev);

    mpam_msc_state_init(s);
    /* Deassert the wired interrupt lines */
    mpam_msc_update_err_irq(s);
    mpam_msc_update_oflow_irq(s);
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

    /*
     * A memory-side MSC always advertises an MBWU monitor, and both
     * subtypes allocate the array, so it must not be empty.
     */
    if (s->num_mbwu_mon < 1 || s->num_mbwu_mon > 65536) {
        error_setg(errp, "num-mbwu-mon must be in the range 1..65536");
        return false;
    }
    if (s->num_csu_mon > 65536) {
        error_setg(errp, "num-csu-mon must be <= 65536");
        return false;
    }
    s->num_ris_vm = s->num_ris;
    s->mpamf = g_new0(Mpamf, s->num_ris);
    for (int i = 0; i < s->num_ris; i++) {
        s->mpamf[i].num_partid_vm = s->num_partid;
        s->mpamf[i].num_int_partid_vm = s->num_int_partid;
        s->mpamf[i].per_nrw_id = g_new0(MpamfPerNrwId, s->num_int_partid);
        s->mpamf[i].cfg_intpartid = g_new0(uint32_t, s->num_partid);
        s->mpamf[i].num_mbwu_mon_vm = s->num_mbwu_mon;
        s->mpamf[i].mbwu_mons = g_new0(MpamMbwuMon, s->num_mbwu_mon);
        s->mpamf[i].num_csu_mon_vm = s->num_csu_mon;
        s->mpamf[i].csu_mons = g_new0(MpamCsuMon, s->num_csu_mon);
    }
    mpam_msc_state_init(s);

    memory_region_init_io(&s->mr, OBJECT(s), &mpam_msc_ops, s, "mpam_msc",
                          MPAM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
    /* Wired interrupt outputs: index 0 = error, index 1 = overflow */
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->err_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->oflow_irq);
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
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_MSMON, 1);
        /* Memory specific bit */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_MBW_PART, 1);
        /*
         * Priority partitioning on the memory side too: advertise
         * HAS_PRI_PART and the same PRI_IDR view as the cache subtype.
         */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_PRI_PART, 1);
        mpamf->pri_idr = FIELD_DP32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                    HAS_INTPRI, 1);
        mpamf->pri_idr = FIELD_DP32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                    INTPRI_0_IS_LOW, 1);
        mpamf->pri_idr = FIELD_DP32(mpamf->pri_idr, MPAMF_PRI_IDR,
                                    INTPRI_WD, 2);

        mpamf->msmon_idr = FIELD_DP32(mpamf->msmon_idr,
                                      MPAMF_MSMON_IDR, MSMON_MBWU, 1);
        mpamf->msmon_idr = FIELD_DP32(mpamf->msmon_idr,
                                      MPAMF_MSMON_IDR, HAS_OFLOW_SR, 1);
        mpamf->mbwumon_idr = FIELD_DP32(mpamf->mbwumon_idr,
                                        MPAMF_MBWUMON_IDR, NUM_MON,
                                        s->num_mbwu_mon);
        mpamf->mbwumon_idr = FIELD_DP32(mpamf->mbwumon_idr,
                                        MPAMF_MBWUMON_IDR, HAS_LONG, 1);
        mpamf->mbwumon_idr = FIELD_DP32(mpamf->mbwumon_idr,
                                        MPAMF_MBWUMON_IDR, HAS_RWBW, 1);

        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, IMPLEMENTER,
                                 MPAM_IIDR_IMPLEMENTER);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, REVISION, 0);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, VARIANT, 0);
        mpamf->iidr = FIELD_DP64(mpamf->iidr, MPAMF_IIDR, PRODUCT_ID, 'Q');

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
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, HAS_MSMON, 1);

        /* Need to implement for RME */
        mpamf->idr = FIELD_DP64(mpamf->idr, MPAMF_IDR, SP4, 0);

        mpamf->msmon_idr = FIELD_DP32(mpamf->msmon_idr,
                                      MPAMF_MSMON_IDR, MSMON_MBWU, 1);
        mpamf->msmon_idr = FIELD_DP32(mpamf->msmon_idr,
                                      MPAMF_MSMON_IDR, HAS_OFLOW_SR, 1);
        mpamf->mbwumon_idr = FIELD_DP32(mpamf->mbwumon_idr,
                                        MPAMF_MBWUMON_IDR, NUM_MON,
                                        s->num_mbwu_mon);
        mpamf->mbwumon_idr = FIELD_DP32(mpamf->mbwumon_idr,
                                        MPAMF_MBWUMON_IDR, HAS_LONG, 1);
        mpamf->mbwumon_idr = FIELD_DP32(mpamf->mbwumon_idr,
                                        MPAMF_MBWUMON_IDR, HAS_RWBW, 1);

        if (s->num_csu_mon) {
            mpamf->msmon_idr = FIELD_DP32(mpamf->msmon_idr,
                                          MPAMF_MSMON_IDR, MSMON_CSU, 1);
            mpamf->csumon_idr = FIELD_DP32(mpamf->csumon_idr,
                                           MPAMF_CSUMON_IDR, NUM_MON,
                                           s->num_csu_mon);
        }

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
        mpamf->iidr = FIELD_DP32(mpamf->iidr, MPAMF_IIDR, PRODUCT_ID, 'Q');

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
                                     CASSOC_WD, 4);
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
    DEFINE_PROP_LINK("bw-source", MPAMMSCState, bw_source,
                     TYPE_MPAM_MSC, DeviceState *),
    DEFINE_PROP_UINT32("num-csu-mon", MPAMMSCState, num_csu_mon, 4),
    DEFINE_PROP_UINT64("csu-capacity-bytes", MPAMMSCState, csu_capacity,
                       32 * MiB),
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
