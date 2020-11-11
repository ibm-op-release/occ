/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/pss/apss.h $                                      */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2015                        */
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

#ifndef _APSS_H
#define _APSS_H

#include <apss_structs.h>
#include <occ_common.h>
#include <trac_interface.h>
#include <errl.h>
#include <rtls.h>

#define NUM_OF_APSS_GPIO_PORTS   2
#define NUM_OF_APSS_PINS_PER_GPIO_PORT 8
#define MAX_APSS_ADC_CHANNELS   16
#define MAX_APSS_GPIO_PORTS      NUM_OF_APSS_GPIO_PORTS
#define MEAS_PADDING_REQUIRED (28-MAX_APSS_ADC_CHANNELS+MAX_APSS_GPIO_PORTS)
#define APSS_12BIT_ADC_MASK   0x0fff

#if ( (!defined(NO_TRAC_STRINGS)) && defined(TRAC_TO_SIMICS) )
void dumpHexString(const void *i_data, const unsigned int len, const char *string);
#endif

#define APSS_DATA_FAIL_PMAX_RAIL 100  //Number of steps before we lower Pmax_rail to nominal. This should allow for 50ms/100 ticks with no APSS data.
#define APSS_DATA_FAIL_MAX       200  //Number of steps we reach before reseting OCC.  This should allow for 100ms/200ticks with no APSS data.
#define APSS_DATA_FAILURE_STEP     1  //Number of steps to increment FAIL_COUNT due to a failed APSS data collection.
#define APSS_ERRORLOG_RESET_THRESHOLD 16 //When to allow apss tasks to log another error if count goes back to 0 again.
#define APSS_MAX_NUM_INIT_RETRIES 2
#define APSS_MAX_NUM_RESET_RETRIES 3
extern uint16_t G_apss_fail_updown_count;     //Used to keep count of number of APSS data collection fails.

//Decrement APSS_FAIL_COUNT to 0.
#define APSS_SUCCESS() {(G_apss_fail_updown_count = 0);}

// Increment APSS_FAIL_COUNT by APSS_DATA_FAILURE_STEP to a maximum of APSS_DATA_FAIL_MAX.
#define APSS_FAIL()   {((APSS_DATA_FAIL_MAX - G_apss_fail_updown_count) >= APSS_DATA_FAILURE_STEP)? \
                        (G_apss_fail_updown_count += APSS_DATA_FAILURE_STEP): \
                        (G_apss_fail_updown_count = APSS_DATA_FAIL_MAX);}

// Apss reset task is run every other tic (1 ms)
#define APSS_RESET_STATE_START        0
#define APSS_RESET_STATE_WAIT_1MS     1
#define APSS_RESET_STATE_WAIT_DONE    51    // Wait 50 ms
#define APSS_RESET_STATE_REINIT       52
#define APSS_RESET_STATE_COMPLETE     53

struct apssPwrMeasStruct
{
  uint16_t adc[MAX_APSS_ADC_CHANNELS];
  uint16_t gpio[MAX_APSS_GPIO_PORTS];
  uint16_t pad[MEAS_PADDING_REQUIRED]; // padding to allow TOD to be 8 byte aligned
  uint64_t tod;      // Time of Day that the ADC Collection Completed
} __attribute__ ((__packed__));
typedef struct apssPwrMeasStruct apssPwrMeasStruct_t;

// G_apss_pwr_meas: power and GPIO readings that OCC gathers from APSS every tick
extern apssPwrMeasStruct_t G_apss_pwr_meas;

// Used to tell slave inbox that pwr meas is complete AND valid
extern volatile bool G_ApssPwrMeasCompleted;

// Used to tell slave inbox that pwr meas is complete but NOT valid
extern volatile bool G_ApssPwrMeasDoneInvalid;

// contents of Mbox scratch register 7 for tracing later
extern volatile uint32_t G_mboxScratch7;

extern initGpioArgs_t G_gpe_apss_initialize_gpio_args;
extern setApssModeArgs_t G_gpe_apss_set_mode_args;

extern uint64_t G_gpe_apss_time_start;
extern uint64_t G_gpe_apss_time_end;

// apss_initialize is product applet
void task_apss_start_pwr_meas(task_t *i_self);
void task_apss_continue_pwr_meas(task_t *i_self);
void task_apss_complete_pwr_meas(task_t *i_self);
void task_apss_reset(task_t *i_self);

void apss_test_pwr_meas(); // used to test measurements
void reformat_meas_data();
bool apss_gpio_get(uint8_t i_pin_number, uint8_t *o_pin_value);
errlHndl_t initialize_apss(void);
extern volatile bool G_apss_recovery_requested;

#endif //_APSS_H
