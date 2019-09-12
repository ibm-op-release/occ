/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_gpe0/firdata/scom_util.c $                            */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2015,2019                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

/* Support for SCOM operations */
#include <fsi.h>
#include <native.h>
#include "gpe_export.h"
#include <scom_util.h>
#include "scom_addr_util.h"
#include <sbe_fifo.h>
#include <ppe42_scom.h>
#include <gpe_util.h>

enum {
    /*FSI addresses are byte offsets, so need to multiply by 4
      since each register is 4 bytes long.
      prefix with 0x10xx for FSI2PIB engine offset */
    DATA0_REG         = 0x1000,  /* SCOM Data Register 0 (0x00) */
    DATA1_REG         = 0x1004,  /* SCOM Data Register 1 (0x01) */
    COMMAND_REG       = 0x1008,  /* SCOM Command Register (0x02) */
    ENGINE_RESET_REG  = 0x1018,  /* Engine Reset Register (0x06) */
    STATUS_REG        = 0x101C,  /* STATUS Register (0x07) */
    PIB_RESET_REG     = 0x101C,  /* PIB Reset Register (0x07) */

    PIB_ABORT_BIT     = 0x00100000, /* 12= PIB Abort */
    PIB_ERROR_BITS    = 0x00007000, /* 17:19= PCB/PIB Errors */
};

/**
 * @brief Indirect SCOM Status
 */
typedef union
{
    uint64_t data64;
    struct
    {
        uint64_t :12;        /*0:11*/
        uint64_t addr:20;    /*12:31*/
        uint64_t done:1;     /*32*/
        uint64_t piberr:3;   /*33:35*/
        uint64_t userstat:4; /*36:39*/
        uint64_t :8;         /*40:47*/
        uint64_t data:16;    /*48:63*/
    };
} IndirectScom_t;

enum {
    MCS_MASK =             0xFFFFFFFF7FFFFF80,
    MCS_BASEADDR =         0x0000000002011800,
    MCS_DMI_BASEADDR =     0x0000000002011A00,
    IND_MCS_BASEADDR =     0x8000006002011A00,
    IND_MCS_DMI_BASEADDR = 0x8000006002011A3F,
    MBA_MASK =             0xFFFFFFFF7FFFFC00,
    MBA_BASEADDR =         0x0000000003010400,
    TCM_MBA_BASEADDR =     0x0000000003010800,
    IND_MBA_BASEADDR =     0x800000000301143f,
};

/**
 * @brief  Translates a relative unit address to a real physical address
 * @param  i_trgt Chip/unit to SCOM.
 * @param  i_addr Address to SCOM (the unit's 0th address for unit SCOMs).
 * @param  o_addr Return address.
 * @return Non-SUCCESS if an internal function fails. SUCCESS otherwise.
 */
int32_t translate_addr( SCOM_Trgt_t i_trgt, uint64_t i_addr, uint64_t * o_addr )
{
    #define FUNC "[translate_addr] "

    int32_t rc = SUCCESS;
    uint8_t l_chip_unit_num = SCOM_Trgt_getChipUnitPos(i_trgt);

    *o_addr = i_addr;

    //The following translation logic is a copy of p9_scominfo_createChipUnitScomAddr
    //function from EKB (chips/p9/common/scominfo/p9_scominfo.C)

    if(i_trgt.type == TRGT_PROC || i_trgt.type == TRGT_OCMB)
    {
        //No need to translate here.
        //We already assigned i_addr to o_addr above, so just return SUCCESS.
        return rc;
    }
    else if(i_trgt.type == TRGT_CAPP) //CAPP
    {
        set_chiplet_id(N0_CHIPLET_ID + (l_chip_unit_num * 2), o_addr);
    }
    else if(i_trgt.type == TRGT_XBUS) //XBUS
    {
        uint8_t l_ring_id = get_ring(i_addr) & 0xF;
        if(l_ring_id <= XB_IOX_2_RING_ID && l_ring_id >= XB_IOX_0_RING_ID)
        {
            set_ring((XB_IOX_0_RING_ID + l_chip_unit_num) & 0xF, o_addr);
        }
        else if(l_ring_id <= XB_PBIOX_2_RING_ID &&
                l_ring_id >= XB_PBIOX_0_RING_ID)
        {
            set_ring((XB_PBIOX_0_RING_ID + l_chip_unit_num) & 0xF, o_addr);
        }
    }
    else if(i_trgt.type == TRGT_OBUS) //OBUS
    {
        set_chiplet_id(OB0_CHIPLET_ID + l_chip_unit_num, o_addr);
    }
    else if(i_trgt.type == TRGT_PEC) //PEC
    {
        if(get_chiplet_id(i_addr) == N2_CHIPLET_ID)
        {
            // nest
            set_ring((N2_PCIS0_0_RING_ID + l_chip_unit_num) & 0xF, o_addr);
        }
        else
        {
            // iopci / pci
            set_chiplet_id(PCI0_CHIPLET_ID + l_chip_unit_num, o_addr);
        }
    }
    else if (i_trgt.type == TRGT_PHB) //PHB
    {
        if(get_chiplet_id(i_addr) == N2_CHIPLET_ID)
        {
            // nest
            if(l_chip_unit_num == 0)
            {
                set_ring(N2_PCIS0_0_RING_ID & 0xF, o_addr);
                set_sat_id((get_sat_id(i_addr) < 4) ?  1 : 4, o_addr);
            }
            else
            {
                set_ring((N2_PCIS0_0_RING_ID + (l_chip_unit_num / 3) + 1) & 0xF,
                                                                        o_addr);
                set_sat_id(((get_sat_id(i_addr) < 4) ? 1 : 4) +
                           ((l_chip_unit_num % 2) ? 0 : 1) +
                           (2 * l_chip_unit_num / 5), o_addr);
            }
        }
        else
        {
            // pci
            if(l_chip_unit_num == 0)
            {
                set_chiplet_id(PCI0_CHIPLET_ID, o_addr);
                set_sat_id(((get_sat_id(i_addr) < 4) ? 1 : 4), o_addr);
            }
            else
            {
                set_chiplet_id(PCI0_CHIPLET_ID + (l_chip_unit_num / 3) + 1, o_addr);
                set_sat_id(((get_sat_id(i_addr) < 4) ? 1 : 4) +
                           ((l_chip_unit_num % 2) ? 0 : 1) +
                           2 * l_chip_unit_num / 5, o_addr);
            }
        }
    }
    else if(i_trgt.type == TRGT_EX) //EX
    {
        if(get_chiplet_id(i_addr) <= EP05_CHIPLET_ID &&
           get_chiplet_id(i_addr) >= EP00_CHIPLET_ID)
        {
            set_chiplet_id(EP00_CHIPLET_ID + (l_chip_unit_num / 2), o_addr);
            uint8_t l_ring_id = get_ring(i_addr) & 0xF;
            l_ring_id = (l_ring_id - (l_ring_id % 2)) + (l_chip_unit_num % 2);
            set_ring(l_ring_id & 0xF, o_addr);
        }
        else if(get_chiplet_id(i_addr) <= EC23_CHIPLET_ID &&
                get_chiplet_id(i_addr) >= EC00_CHIPLET_ID)
        {
            set_chiplet_id(EC00_CHIPLET_ID +
                           (get_chiplet_id(i_addr) % 2) +
                           (l_chip_unit_num * 2), o_addr);
        }
    }
    else if(i_trgt.type == TRGT_EQ) //EQ
    {
        set_chiplet_id(EP00_CHIPLET_ID + l_chip_unit_num, o_addr);
    }
    else if(i_trgt.type == TRGT_EC) //EC
    {
        set_chiplet_id(EC00_CHIPLET_ID + l_chip_unit_num, o_addr);
    }
    else if(i_trgt.type == TRGT_MCS || //MCS
            i_trgt.type == TRGT_MI)    //MI
    {
        set_chiplet_id(N3_CHIPLET_ID - (2 * (l_chip_unit_num / 2)), o_addr);
        set_sat_id(2 * (l_chip_unit_num % 2), o_addr);
    }
    else if(i_trgt.type == TRGT_MCBIST || //MCBIST
            i_trgt.type == TRGT_MC)       //MC
    {
        set_chiplet_id(MC01_CHIPLET_ID + l_chip_unit_num, o_addr);
    }
    else if(i_trgt.type == TRGT_MCA) //MCA
    {
        if(get_chiplet_id(i_addr) == MC01_CHIPLET_ID ||
           get_chiplet_id(i_addr) == MC23_CHIPLET_ID)
        {
            set_chiplet_id(MC01_CHIPLET_ID + (l_chip_unit_num / 4), o_addr);

            if((get_ring(i_addr) & 0xf) == MC_MC01_0_RING_ID)
            {
                // mc
                uint8_t l_sat_id = get_sat_id(i_addr);
                set_sat_id((l_sat_id - (l_sat_id % 4)) + (l_chip_unit_num % 4)
                                                                      , o_addr);
            }
            else
            {
                // iomc
                set_ring((MC_IOM01_0_RING_ID + (l_chip_unit_num % 4)) & 0xF
                                                                      , o_addr);
            }
        }
        else
        {
            // mcs->mca registers
            uint8_t l_mcs_unitnum = l_chip_unit_num / 2;
            set_chiplet_id(N3_CHIPLET_ID - (2 * (l_mcs_unitnum / 2)) , o_addr);
            set_sat_id(2 * (l_chip_unit_num % 2), o_addr);
            uint8_t l_mcs_sat_offset = 0x2F & get_sat_offset(i_addr);
            l_mcs_sat_offset |= (l_chip_unit_num % 2) << 4;
            set_sat_offset(l_mcs_sat_offset, o_addr);
        }
    }
    else if ( i_trgt.type == TRGT_MCC )
    {
        if (((get_chiplet_id(i_addr) == N3_CHIPLET_ID) || (get_chiplet_id(i_addr) == N1_CHIPLET_ID)))
        {
            //SCOM3   (See mc_clscom_rlm.fig <= 0xB vs mc_scomfir_rlm.fig > 0xB)
            //DMI0           05     02       0   0x2X (X <= 0xB)
            //DMI1           05     02       0   0x3X (X <= 0xB)
            //DMI2           05     02       2   0x2X (X <= 0xB)
            //DMI3           05     02       2   0x3X (X <= 0xB)
            //DMI4           03     02       0   0x2X (X <= 0xB)
            //DMI5           03     02       0   0x3X (X <= 0xB)
            //DMI6           03     02       2   0x2X (X <= 0xB)
            //DMI7           03     02       2   0x3X (X <= 0xB)
            set_chiplet_id(N3_CHIPLET_ID - (2 * (l_chip_unit_num / 4)), o_addr);
            set_sat_id(2 * ((l_chip_unit_num / 2) % 2), o_addr);
            uint8_t l_sat_offset = get_sat_offset(i_addr);
            l_sat_offset = (l_sat_offset & 0xF) + ((2 + (l_chip_unit_num % 2)) << 4);
            set_sat_offset(l_sat_offset, o_addr);
        }

        if (((get_chiplet_id(i_addr) == MC01_CHIPLET_ID) || (get_chiplet_id(i_addr) == MC23_CHIPLET_ID)))
        {
            //CHANX.USTL.  Sat_id: 10 + port_id (8,9,10,11)
            //CHANX.DSTL.  Sat_id: 01 + port_id (4,5,6,7)
            set_chiplet_id(MC01_CHIPLET_ID + (l_chip_unit_num / 4), o_addr);

            uint8_t l_sat_id = get_sat_id(i_addr);

            if (P9A_MC_DSTL_CHAN0_SAT_ID <= l_sat_id && l_sat_id <= P9A_MC_DSTL_CHAN3_SAT_ID)
            {
                set_sat_id(P9A_MC_DSTL_CHAN0_SAT_ID + (l_chip_unit_num % 4), o_addr);
            }

            if (P9A_MC_USTL_CHAN0_SAT_ID <= l_sat_id && l_sat_id <= P9A_MC_USTL_CHAN3_SAT_ID)
            {
                set_sat_id(P9A_MC_USTL_CHAN0_SAT_ID + (l_chip_unit_num % 4), o_addr);
            }
        }
    }
    else if ( i_trgt.type == TRGT_OMIC )
    {
        set_chiplet_id(MC01_CHIPLET_ID + (l_chip_unit_num / 3), o_addr);

        uint8_t l_ring_id = get_ring(i_addr) & 0xF;

        if (P9A_MC_OMIC0_RING_ID <= l_ring_id && l_ring_id <= P9A_MC_OMIC2_RING_ID)
        {
            set_ring(P9A_MC_OMIC0_RING_ID + (l_chip_unit_num % 3), o_addr);
        }

        if (P9A_MC_OMIC0_PPE_RING_ID <= l_ring_id && l_ring_id <= P9A_MC_OMIC2_PPE_RING_ID)
        {
            set_ring(P9A_MC_OMIC0_PPE_RING_ID + (l_chip_unit_num % 3), o_addr);
        }
    }
    else if ( i_trgt.type == TRGT_NPU )
    {
        // NPU0 and NPU1 exist on the N3 chiplet, NPU2 exists on the N1 chiplet instead
        uint8_t l_chiplet_id = ( 2 == l_chip_unit_num ) ? N1_CHIPLET_ID : N3_CHIPLET_ID ;
        set_chiplet_id( l_chiplet_id, o_addr );
        uint8_t l_sat_id = get_sat_id( i_addr );
        uint8_t l_ring = get_ring(i_addr) & 0xF;

        // Covers the following addresses:
        // NPU0: 05011000 to 050113FF
        // NPU1: 05011400 to 050117FF
        // NPU2: 03011C00 to 03011FFF
        if ( N3_NPU_0_RING_ID  == l_ring ||
             N3_NPU_1_RING_ID  == l_ring ||
             P9A_NPU_2_RING_ID == l_ring )
        {
            // NPU0/NPU1
            if ( N3_CHIPLET_ID == l_chiplet_id )
            {
                set_ring( N3_NPU_0_RING_ID + l_chip_unit_num, o_addr );
            }
            // NPU2
            else if ( N1_CHIPLET_ID == l_chiplet_id )
            {
                set_ring( P9A_NPU_2_RING_ID, o_addr );
            }
        }
        // Covers the following addresses:
        // NPU0: 05013C00 to 05013C8F
        // NPU1: 05013CC0 to 05013D4F
        // NPU2: 03012000 to 0301208F
        else if ( P9A_NPU_0_FIR_RING_ID == l_ring ||
                  P9A_NPU_2_FIR_RING_ID == l_ring )
        {
            // NPU0/NPU1
            if ( N3_CHIPLET_ID == l_chiplet_id )
            {
                set_ring( P9A_NPU_0_FIR_RING_ID, o_addr );
                set_sat_id( (l_sat_id % 3) + (3 * l_chip_unit_num), o_addr );
            }
            // NPU2
            else if ( N1_CHIPLET_ID == l_chiplet_id )
            {
                set_ring( P9A_NPU_2_FIR_RING_ID, o_addr );
                set_sat_id( (l_sat_id % 3), o_addr );
            }
        }
    }
    else
    {
        TRAC_ERR( FUNC"unsupported unit type %d", i_trgt.type );
        rc = FAIL;
    }

    return rc;

    #undef FUNC
}

/**
 * @brief  Executes standard getscom.
 * @param  i_trgt Chip to SCOM.
 * @param  i_addr Address to SCOM.
 * @param  o_val  Returned value.
 * @return Non-SUCCESS if an internal function fails. SUCCESS otherwise.
 */
int32_t SCOM_getScom( SCOM_Trgt_t i_trgt, uint32_t i_addr, uint64_t * o_val )
{
    int32_t rc = SUCCESS;

    /* Get the parent chip. */
    SCOM_Trgt_t chip_targ = SCOM_Trgt_getParentChip(i_trgt);

    /* Get the address relative to the parent chip. */
    uint64_t trans_addr;
    rc = translate_addr( i_trgt, i_addr, &trans_addr );
    if ( SUCCESS == rc )
    {
        //Use SBE FIFO if it's a slave proc
        if(!chip_targ.isMaster)
        {
            rc = getFifoScom(&chip_targ, trans_addr, o_val);
        }
        else
        {
            rc = getscom_abs(trans_addr, o_val);

            // Add exception for chiplet offline errors on the UNIT_CS or
            // HOST_ATTN broadcast registers. The value returned will still be
            // valid. Even though one or more of the chiplets may have been
            // offline.
            if ( 2 == rc &&
                 (0x50040018 == i_addr || 0x50040009 == i_addr) )
            {
                rc = SUCCESS;
            }
        }
    }

    if (rc != SUCCESS)
    {
        TRAC_ERR("SCOM_getScom(0x%08X): returning %d", i_addr, rc);
    }

    return rc;
}

/**
 * @brief  Execute indirect getscom.
 * @param  i_trgt Chip to SCOM.
 * @param  i_addr Address to SCOM.
 * @param  o_val  Returned value.
 * @return Non-SUCCESS if an internal function fails. SUCCESS otherwise.
 */
int32_t SCOM_getIdScom( SCOM_Trgt_t i_trgt, uint64_t i_addr, uint32_t * o_val )
{
    #define FUNC "[SCOM_getIdScom] "

    int32_t rc = SUCCESS;

    *o_val = 0;

    /* Get the parent chip */
    SCOM_Trgt_t chip_targ = SCOM_Trgt_getParentChip(i_trgt);

    /* Get the address relative to the parent chip. */
    uint64_t trans_addr;
    rc = translate_addr( i_trgt, i_addr, &trans_addr );
    if ( SUCCESS != rc ) return rc;

    /* An indirect SCOM is performed by putting the top of the 64-bit address
     * into the first data word of the SCOM */

    /* Zero out the indirect address from the buffer. */
    /* bit 0-31 - indirect area. */
    /* bit 32 - always 0 */
    /* bit 33-47 - bcast/chipletID/port */
    /* bit 48-63 - local addr */
    uint32_t phys_addr = trans_addr & 0x000000007FFFFFFF;

    /* To do a read we need to do a write first. */

    /* Start with the 20bit indirect address */
    uint64_t data_buffer = trans_addr & 0x001FFFFF00000000;
    /* Turn the read bit on. */
    data_buffer |= 0x8000000000000000;

    /* perform write before the read with the new */
    if(!chip_targ.isMaster)
    {
        rc = putFifoScom(&chip_targ, phys_addr, data_buffer);
    }
    else
    {
        rc = putscom_abs(phys_addr, data_buffer);
    }
    if ( SUCCESS != rc ) return rc;

    // Loop on read until we see done, error, or we timeout
    IndirectScom_t scomout;
    uint64_t elapsed_indScom_time_ns = 0;
    do
    {
        /* Now perform the op requested using the passed in */
        /* IO_Buffer to pass the read data back to caller. */
        //Use SBE FIFO if it's a slave proc
        if(!chip_targ.isMaster)
        {
            rc = getFifoScom(&chip_targ, trans_addr, &(scomout.data64));
        }
        else
        {
            rc = getscom_abs(trans_addr, &(scomout.data64));
        }
        if ( SUCCESS != rc )
        {
            TRAC_ERR("SCOM_getIdScom(0x%08X): returning %d", i_addr, rc);
            return rc;
        }

        /* Check for PIB error. */
        if ( scomout.piberr )
        {
            TRAC_ERR( FUNC"ID SCOM PIB error: phys_addr=0x%08x "
                      "trans_addr=0x%08x%08x", phys_addr,
                      (uint32_t)(trans_addr >> 32), (uint32_t)trans_addr );
            return FAIL;
        }

        /* Jump out when done. */
        if ( scomout.done ) break;

        busy_wait( 10 ); /* sleep for 10,000 ns */
        elapsed_indScom_time_ns += 10000;

    } while ( elapsed_indScom_time_ns <= 100000 ); /* wait for .1ms */

    if ( !scomout.done )
    {
        TRAC_ERR( FUNC"ID SCOM loop timeout exceeded: phys_addr=0x%08x "
                      "trans_addr=0x%08x%08x", phys_addr,
                      (uint32_t)(trans_addr >> 32), (uint32_t)trans_addr );
        return FAIL;
    }

    *o_val = scomout.data;

    return rc;

    #undef FUNC
}

/**
 * @brief  Executes standard putscom.
 * @param  i_trgt Chip to SCOM.
 * @param  i_addr Address to SCOM.
 * @param  i_val  Value to put.
 * @return Non-SUCCESS if an internal function fails. SUCCESS otherwise.
*/
int32_t SCOM_putScom( SCOM_Trgt_t i_trgt, uint32_t i_addr, uint64_t i_val )
{
    int32_t l_rc = SUCCESS;

    /* Get the parent chip. */
    SCOM_Trgt_t l_chip_targ = SCOM_Trgt_getParentChip(i_trgt);

    /* Get the address relative to the parent chip. */
    uint64_t l_trans_addr;
    l_rc = translate_addr( i_trgt, i_addr, &l_trans_addr );
    if ( SUCCESS == l_rc )
    {
        /* Do the SCOM. */
        if(!l_chip_targ.isMaster)
        {
            return putFifoScom(&l_chip_targ, i_addr, i_val);
        }
        else
        {
            l_rc = putscom_abs(l_trans_addr, i_val);
        }
    }

    return l_rc;
}
