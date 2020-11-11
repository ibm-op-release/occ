/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_gpe0/apss_read.c $                                    */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2015,2017                        */
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


#include "pk.h"
#include "ppe42_scom.h"
#include "ipc_api.h"
#include "ipc_async_cmd.h"
#include "pss_constants.h"
#include <apss_structs.h>
#include "gpe_util.h"

#define MAX_EXECUTION_TIMER (PK_INTERVAL_SCALE((uint32_t)PK_MICROSECONDS(250)))

extern uint8_t G_apss_mode;
extern gpe_shared_data_t * G_gpe_shared_data;


uint32_t g_max_apss_start __attribute__((section(".sbss.debug"))) = 0;
uint32_t g_max_apss_cont  __attribute__((section(".sbss.debug")))  = 0;
uint32_t g_max_apss_comp  __attribute__((section(".sbss.debug")))  = 0;

/*
 * Function Specifications:
 *
 * Name: apss_start_pwr_meas_read
 *
 * Description:  Start a read of the power measurement from APSS
 *
 * Inputs:       cmd is a pointer to IPC msg's cmd and cmd_data struct
 *
 * Outputs:      error: sets rc, address, and ffdc in the cmd_data's
 *                      GpeErrorStruct
 *
 * End Function Specification
 */

void apss_start_pwr_meas_read(ipc_msg_t* cmd, void* arg)
{
    // Note: arg was set to 0 in ipc func table (ipc_func_tables.c), so don't use it.
    // the ipc arguments passed through the ipc_msg_t structure, has a pointer
    // to the G_gpe_start_pwr_meas_read_args struct.

#ifdef DEBUG_APSS_SEQ
    PK_TRACE("apss_start_pwr_meas_read: enter");
#endif
    uint32_t end_time = 0;
    uint32_t diff_time = 0;
    uint32_t start_time = pk_timebase32_get();

    int      rc;
    uint64_t regValue; // a pointer to hold the putscom_abs register value
    ipc_async_cmd_t *async_cmd = (ipc_async_cmd_t*)cmd;
    apss_start_args_t *args = (apss_start_args_t*)async_cmd->cmd_data;

    do{

        // wait for ADC completion, or timeout after 5 micro seconds.
        // scom register SPIPSS_ADC_STATUS_REG's bit 0 (HWCTRL_ONGOING)
        // indicates when completion occurs.
        rc = wait_spi_completion(&(args->error), SPIPSS_ADC_STATUS_REG, 5);
        if(rc) // Timeout Reached, and SPI transaction didn't complete, copy over rc
        {
            PK_TRACE("apss_start_pwr_meas_read:wait_spi_completion Timed out, rc = 0x%08x",
                     rc);
            // FFDC set in wait_spi_completion
            break;
        }

        // Setup control regs:

        // SPIPSS_ADC_CTRL_REG0:
        // frame_size=16, out_count=0, in_count=16
        //    rc = putscom(0, SPIPSS_ADC_CTRL_REG0, uint64_t 0x4000100000000000);
        regValue = 0x4000100000000000;
        rc = putscom_abs(SPIPSS_ADC_CTRL_REG0, regValue);
        if(rc)
        {
            PK_TRACE("apss_start_pwr_meas_read: SPIPSS_ADC_CTRL_REG0 putscom failed. rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_CTRL_REG0, GPE_RC_SCOM_PUT_FAILED, rc);
            break;
        }

        // SPIPSS_ADC_CTRL_REG1: ADC FSM
        if (APSS_MODE_COMPOSITE == G_apss_mode)
        {
            // clock_divider=36, frames=17 (i.e. 18)
            if(G_gpe_shared_data->spipss_spec_p9)
            {
                regValue = 0x8092200000000000;  // P9 Spec
            }
            else
            {
                regValue = 0x8093C00000000000;  // P9 Actual (16 frames)
            }
        }
        else
        {
            // clock_divider=36, frames=15 (i.e. 16)
            if(G_gpe_shared_data->spipss_spec_p9)
            {
                regValue = 0x8091E00000000000;  // P9 Spec
            }
            else
            {
                regValue = 0x8093C00000000000;  // P9 Actual
            }
        }
        rc = putscom_abs(SPIPSS_ADC_CTRL_REG1, regValue);
        if(rc)
        {
            PK_TRACE("apss_start_pwr_meas_read: SPIPSS_ADC_CTRL_REG1 putscom failed. rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_CTRL_REG1, GPE_RC_SCOM_PUT_FAILED, rc);
            break;
        }

        // SPIPSS_ADC_CTRL_REG2: ADC interframe delay
        // 5 usec
        // rc = putscom_abs(SPIPSS_ADC_CTRL_REG2, 0x0019000000000000);
        regValue = 0x0019000000000000;
        rc = putscom_abs(SPIPSS_ADC_CTRL_REG2, regValue);
        if(rc)
        {
            PK_TRACE("apss_start_pwr_meas_read: SPIPSS_ADC_CTRL_REG2 putscom failed. rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_CTRL_REG2, GPE_RC_SCOM_PUT_FAILED, rc);
            break;
        }

        // SPIPSS_ADC_WDATA_REG:
        // APSS command to continue previous command
        // rc = putscom_abs(SPIPSS_ADC_WDATA_REG, 0x0000000000000000);
        regValue = 0x0000000000000000;
        rc = putscom_abs(SPIPSS_ADC_WDATA_REG, regValue);
        if(rc)
        {
            PK_TRACE("apss_start_pwr_meas_read: SPIPSS_ADC_WDATA_REG putscom failed. rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_WDATA_REG, GPE_RC_SCOM_PUT_FAILED, rc);
            break;
        }

        // SPIPSS_ADC_COMMAND_REG:
        // Start SPI Transaction
        // rc = putscom_abs(SPIPSS_ADC_COMMAND_REG, 0x8000000000000000);
        regValue = 0x8000000000000000;
        rc = putscom_abs(SPIPSS_ADC_COMMAND_REG, regValue);

        if(rc)
        {
            PK_TRACE("apss_start_pwr_meas_read: SPIPSS_ADC_COMMAND_REG putscom failed. rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_COMMAND_REG, GPE_RC_SCOM_PUT_FAILED, rc);
            break;
        }

    } while(0);

#ifdef DEBUG_APSS_SEQ
    PK_TRACE("apss_start_pwr_meas_read: calling ipc_send_rsp()");
#endif
    end_time = pk_timebase32_get();

    diff_time = end_time - start_time;

    if(diff_time > g_max_apss_start)
    {
        g_max_apss_start = diff_time;

        if(diff_time > MAX_EXECUTION_TIMER)
        {
            PK_TRACE("apss_start_pwr_meas_read took longer than expected. Delta OTBR: %x",
                     diff_time);
        }
    }


    // send back a response, IPC success even if ffdc/rc are non zeros
    rc = ipc_send_rsp(cmd, IPC_RC_SUCCESS);
    if(rc)
    {
        PK_TRACE("apss_start_pwr_meas_read: Failed to send response back. Halting GPE0. rc = 0x%08X", rc);
        gpe_set_ffdc(&(args->error), 0x00, GPE_RC_IPC_SEND_FAILED, rc);
        pk_halt();
    }
}

/*
 * Function Specifications:
 *
 * Name: apss_continue_pwr_meas_read
 *
 * Description:  Continue the read of the power measurement from APSS
 *               Read the 16 12-bit channels
 *
 * Inputs:       cmd is a pointer to IPC msg's cmd and cmd_data struct
 *
 * Outputs:      cmd->cmd_data->meas_data[]: contains 16 channels data,
 *                                           formated as 4*64 bits array.
 *               error:                      sets rc, address, and ffdc
 *                                           in the cmd_data's GpeErrorStruct
 *
 * End Function Specification
 */

void apss_continue_pwr_meas_read(ipc_msg_t* cmd, void* arg)
{
    // the ipc arguments are passed through the ipc_msg_t structure, has a pointer
    // to the G_gpe_continue_pwr_meas_read_args

    uint32_t end_time = 0;
    uint32_t diff_time = 0;
    uint32_t start_time = pk_timebase32_get();

    uint64_t     regValue = 0;
    int          rc;
    ipc_async_cmd_t *async_cmd = (ipc_async_cmd_t*)cmd;
    apss_continue_args_t *args = (apss_continue_args_t*)async_cmd->cmd_data;

#ifdef DEBUG_APSS_SEQ
    PK_TRACE("apss_continue_pwr_meas_read: enter");
#endif

    do{
        // wait for ADC completion, or timeout after 120us (from Jordan for 16 channels)
        // scom register SPIPSS_ADC_STATUS_REG's bit 0 (HWCTRL_ONGOING)
        // indicates when completion occurs.
        rc = wait_spi_completion(&(args->error), SPIPSS_ADC_STATUS_REG, 120);
        if(rc) // Timeout Reached, and SPI transaction didn't complete
        {
            PK_TRACE("apss_continue_pwr_meas_read:wait_spi_completion Timed out, rc = 0x%08x",
                     rc);
            // FFDC already set inside wait_spi_completion
            break;
        }

        // SIMICS VERIFY: ADC readings are done for 32 bytes = 4 * 64 bit reads
        // they are saved in the common OCC-GPE0 area: verify using SIMICS
        // Check every scom read, store rc in the error.rc if it fails.

        // read four channels - adc[0-3] - from APSS into meas_data[0]
        rc = getscom_abs(SPIPSS_ADC_RDATA_REG0, (uint64_t*) args->meas_data);
        if(rc)
        {
            PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_RDATA_REG0 getscom failed with rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_RDATA_REG0, GPE_RC_SCOM_GET_FAILED, rc);
            break;
        }

        // read four channels - adc[4-7] - from APSS into meas_data[1]
        rc = getscom_abs(SPIPSS_ADC_RDATA_REG1, (uint64_t*) &args->meas_data[1]);
        if(rc)
        {
            PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_RDATA_REG1 getscom failed with rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_RDATA_REG1, GPE_RC_SCOM_GET_FAILED, rc);
            break;
        }

        // read four channels - adc[8-11] - from APSS into meas_data[2]
        rc = getscom_abs(SPIPSS_ADC_RDATA_REG2, (uint64_t*) &args->meas_data[2]);
        if(rc)
        {
            PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_RDATA_REG2 getscom failed with rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_RDATA_REG2, GPE_RC_SCOM_GET_FAILED, rc);
            break;
        }

        // read four channels - adc[12-15] - from APSS into meas_data[3]
        rc = getscom_abs(SPIPSS_ADC_RDATA_REG3, (uint64_t*) &args->meas_data[3]);
        if(rc)
        {
            PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_RDATA_REG3 getscom failed with rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), SPIPSS_ADC_RDATA_REG3, GPE_RC_SCOM_GET_FAILED, rc);
            break;
        }

        // If we're trying to do composite mode for the P8 spec, we need to configure
        // the ADC controller again. P9 spec does not need to do this since there would
        // be  room for up to 32 frames.
        if ( (APSS_MODE_COMPOSITE == G_apss_mode) &&(!G_gpe_shared_data->spipss_spec_p9) )
        {
            // ADC FSM, clock_divider=7, frames=1 (ie 2 for gpio ports)
            regValue = 0x8090400000000000;
            rc = putscom_abs(SPIPSS_ADC_CTRL_REG1, regValue);
            if(rc)
            {
                PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_CTRL_REG1 putscom failed. rc = 0x%08x",
                         rc);
                gpe_set_ffdc(&(args->error), SPIPSS_ADC_CTRL_REG1, GPE_RC_SCOM_PUT_FAILED, rc);
                break;
            }

            // APSS command to continue previous command
            regValue = 0x0000000000000000;
            rc = putscom_abs(SPIPSS_ADC_WDATA_REG, regValue);
            if(rc)
            {
                PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_WDATA_REG putscom failed. rc = 0x%08x",
                         rc);
                gpe_set_ffdc(&(args->error), SPIPSS_ADC_WDATA_REG, GPE_RC_SCOM_PUT_FAILED, rc);
                break;
            }

            busy_wait(5);

            // Start SPI Transaction
            regValue = 0x8000000000000000;
            rc = putscom_abs(SPIPSS_ADC_COMMAND_REG, regValue);
            if(rc)
            {
                PK_TRACE("apss_continue_pwr_meas_read: SPIPSS_ADC_COMMAND_REG putscom failed. rc = 0x%08x",
                         rc);
                gpe_set_ffdc(&(args->error), SPIPSS_ADC_COMMAND_REG, GPE_RC_SCOM_PUT_FAILED, rc);
                break;
            }
        }
    } while(0);

#ifdef DEBUG_APSS_SEQ
    PK_TRACE("apss_continue_pwr_meas_read: calling ipc_send_rsp()");
#endif

    end_time = pk_timebase32_get();

    diff_time = end_time - start_time;

    if(diff_time > g_max_apss_cont)
    {
        g_max_apss_cont = diff_time;

        if(diff_time > MAX_EXECUTION_TIMER)
        {
            PK_TRACE("apss_continue took longer than expected. Delta OTBR: %x",
                     diff_time);
        }
    }

    // send back a response, IPC success (even if ffdc/rc are non zeros)
    rc = ipc_send_rsp(cmd, IPC_RC_SUCCESS);
    if(rc)
    {
        PK_TRACE("apss_continue_pwr_meas_read: Failed to send response back. Halting GPE0. rc = 0x%08x",
                 rc);
        gpe_set_ffdc(&(args->error), 0x00, GPE_RC_IPC_SEND_FAILED, rc);
        pk_halt();
    }
}

/*
 * Function Specifications:
 *
 * Name: apss_complete_pwr_meas_read
 *
 * Description:  Complete the reading of the power measurement from APSS
 *               Read the TOD (and GPIOs)
 *
 * Inputs:       cmd is a pointer to IPC msg's cmd and cmd_data struct
 *
 * Outputs:      cmd->cmd_data->meas_data[3]:TOD
 *               error:                      sets rc, address, and ffdc
 *                                           in the cmd_data's GpeErrorStruct
 *
 * End Function Specification
 */

void apss_complete_pwr_meas_read(ipc_msg_t* cmd, void* arg)
{
    // the ipc arguments are passed through the ipc_msg_t structure, has a pointer
    // to the G_gpe_complete_pwr_meas_read_args
#ifdef DEBUG_APSS_SEQ
    PK_TRACE("apss_complete_pwr_meas_read: enter");
#endif
    uint32_t end_time = 0;
    uint32_t diff_time = 0;
    uint32_t start_time = pk_timebase32_get();


    int          rc;
    ipc_async_cmd_t *async_cmd = (ipc_async_cmd_t*)cmd;
    apss_complete_args_t *args = (apss_complete_args_t*)async_cmd->cmd_data;
    uint32_t     rdata_reg = 0;
    uint64_t l_temp64 = 0;
    do {
        // Get Time of Day
        rc = getscom_abs(TOD_VALUE_REG, &args->meas_data[3]);
        if(rc)
        {
            PK_TRACE("apss_complete_pwr_meas_read: TOD_VALUE_REG getscom failed. rc = 0x%08x",
                     rc);
            gpe_set_ffdc(&(args->error), TOD_VALUE_REG, GPE_RC_SCOM_GET_FAILED, rc);
            break;
        }

        // wait for completion, or timeout after 40us (from Jordan for GPIOs)
        // scom register SPIPSS_ADC_STATUS_REG's bit 0 (HWCTRL_ONGOING)
        // indicates when completion occurs.
        rc = wait_spi_completion(&(args->error), SPIPSS_ADC_STATUS_REG, 40);
        if(rc) // Timeout Reached, and SPI transaction didn't complete
        {
            PK_TRACE("apss_complete_pwr_meas_read:wait_spi_completion Timed out, rc = 0x%08x",
                     rc);
            // FFDC already set inside wait_spi_completion
            break;
        }

        // If we're in composite mode, collect the GPIO data
        if (APSS_MODE_COMPOSITE == G_apss_mode)
        {
            // RDATA4-7 do not exist on P9 HW, spec says they should.
            if(G_gpe_shared_data->spipss_spec_p9) rdata_reg = SPIPSS_ADC_RDATA_REG4;
            else rdata_reg = SPIPSS_ADC_RDATA_REG0;

            // Read first 8 bytes of data (GPIO frames) into meas_data[0]
            rc = getscom_abs(rdata_reg, &args->meas_data[0]);
            if(rc)
            {
                PK_TRACE("apss_complete_pwr_meas_read: RDATA_REG(0x%08X) getscom failed. rc = 0x%08x",
                         rdata_reg, rc);
                gpe_set_ffdc(&(args->error), SPIPSS_ADC_RDATA_REG4, GPE_RC_SCOM_GET_FAILED, rc);
                break;
            }
        }
    } while(0);

#ifdef DEBUG_APSS_SEQ
    PK_TRACE("apss_complete_pwr_meas_read: calling ipc_send_rsp()");
#endif
    end_time = pk_timebase32_get();

    diff_time = end_time - start_time;

    if(diff_time > g_max_apss_comp)
    {
        g_max_apss_comp = diff_time;

        if(diff_time > MAX_EXECUTION_TIMER)
        {
            PK_TRACE("apss_complete took longer than expected. Delta OTBR: %x",
                     diff_time);
        }
    }
    // Read Mbox scratch register 7 (used to know if POWR detected EPOW for NVDIMM CSAVE)
    // don't mark APSS data as bad if this SCOM fails
    rc = getscom_abs(MBOX_SCRATCH7_REG, &l_temp64);
    if(rc)
    {
        PK_TRACE("apss_complete_pwr_meas_read: MBOX_SCRATCH7_REG getscom failed. rc = 0x%08x",
                 rc);
        // indicate failure in the parm, leave the APSS data as good
        args->mboxScratch7 = SCRATCH7_READ_ERROR;
    }
    else
        args->mboxScratch7 = (uint32_t)(l_temp64 >> 32);

    // send back a response, IPC success (even if ffdc/rc are non zeros)
    rc = ipc_send_rsp(cmd, IPC_RC_SUCCESS);
    if(rc)
    {
        PK_TRACE("apss_complete_pwr_meas_read: Failed to send response back. Halting GPE0. rc = 0x%08X", rc);
        gpe_set_ffdc(&(args->error), 0x00, GPE_RC_IPC_SEND_FAILED, rc);
        pk_halt();
    }
}
