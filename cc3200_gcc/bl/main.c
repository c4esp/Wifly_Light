/*
 Copyright (C) 2014 Nils Weiss, Patrick Bruenn.

 This file is part of Wifly_Light.

 Wifly_Light is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Wifly_Light is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Wifly_Light.  If not, see <http://www.gnu.org/licenses/>. */


// inc(TI) includes
#include "hw_types.h"
#include "hw_ints.h"

// driverlib includes
#include "rom_map.h"
#include "interrupt.h"
/**
#include <string.h>
#include <stdint.h>
#include "hw_memmap.h"

// SimpleLink includes
#include "simplelink.h"

#include "rom.h"
#include "prcm.h"
#include "utils.h"

// common interface includes
#include "uart_if.h"
#include "pinmux.h"

// wylight includes
#include "firmware_download.h"

#define UART_PRINT          Report
#define APP_NAME            "WyLight Bootloader"
//*/


// Loop forever, user can change it as per application's requirement
#define LOOP_FOREVER(line_number) \
            {\
                while(1); \
            }

// GLOBAL VARIABLES -- Start
extern void (* const g_pfnVectors[])(void);



// Board Initialization & Configuration
static void BoardInit(void)
{
	// Set vector table base
	MAP_IntVTableBaseSet((unsigned long) &g_pfnVectors[0]);

	// Enable Processor
	MAP_IntMasterEnable();
	MAP_IntEnable(FAULT_SYSTICK);

	PRCMCC3200MCUInit();
}

int main()
{
	BoardInit();

	// Configure the pinmux settings for the peripherals exercised
	PinMuxConfig();

	//
	// Configuring UART
	//
	InitTerm();
	Message("HUHU");
//	sl_WlanPolicySet();



	LOOP_FOREVER(__LINE__);

}

