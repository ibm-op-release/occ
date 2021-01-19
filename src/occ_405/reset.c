/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/reset.c $                                         */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2017                        */
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

#include <occ_common.h>
#include <common_types.h>
#include "ssx_io.h"
#include "trac.h"
#include "rtls.h"
#include "state.h"
#include "dcom.h"

// Holds the state of the reset state machine
uint8_t G_reset_state = RESET_NOT_REQUESTED;

SsxTimebase G_reset_delay_start_time = 0;

// Function Specification
//
// Name: isSafeStateRequested
//
// Description: Helper function for determining if we should go to safe state
//
// End Function Specification
bool isSafeStateRequested(void)
{
    return ((RESET_REQUESTED_DUE_TO_ERROR == G_reset_state) ? TRUE : FALSE);
}


// Function Specification
//
// Name: reset_state_request
//
// Description: Request Reset States
//
// End Function Specification
void reset_state_request(uint8_t i_request)
{
  switch(i_request)
  {
    case RESET_REQUESTED_DUE_TO_ERROR:
      // If we aren't already in reset, set the reset
      // state to enter the reset state machine.
      if( G_reset_state == RESET_NOT_REQUESTED )
      {
        // check if this system supports NVDIMMs and we are ACTIVE
        // if we aren't active that means we aren't monitoring for EPOW anyway
        // so no need to delay
        if( (G_sysConfigData.apss_gpio_map.nvdimm_epow != SYSCFG_INVALID_PIN) &&
            (CURRENT_STATE() == OCC_STATE_ACTIVE) )
        {
            // delay reset to allow for EPOW detection
            TRAC_IMP("Delaying %dms for Activating reset required state.", NVDIMM_EPOW_SAFE_DELAY_MS);
            G_reset_state = RESET_NVDIMM_DELAY;
            G_reset_delay_start_time = ssx_timebase_get();
        }
        else
        {
            TRAC_IMP("Activating reset required state.");
            G_reset_state = RESET_REQUESTED_DUE_TO_ERROR;

            // Post the semaphore to wakeup the thread that
            // will put us into SAFE state.
            ssx_semaphore_post(&G_dcomThreadWakeupSem);

            // Set RTL Flags here too, depending how urgent it is that we stop
            // running tasks.
            rtl_set_run_mask(RTL_FLAG_RST_REQ);
        }
      }
      break;

    case RESET_CLEAR_DELAY:
            // Delay expired, move into safe state
            G_reset_state = RESET_REQUESTED_DUE_TO_ERROR;

            // Post the semaphore to wakeup the thread that
            // will put us into SAFE state.
            ssx_semaphore_post(&G_dcomThreadWakeupSem);

            // Set RTL flags to stop running tasks.
            rtl_set_run_mask(RTL_FLAG_RST_REQ);
      break;

    default:
      break;

  }
}

