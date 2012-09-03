/**
 Copyright (C) 2012 Nils Weiss, Patrick Bruenn.
 
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

#include "platform.h"
#include "ScriptCtrl.h"
#include "ledstrip.h"
#include "eeprom.h"
#include "trace.h"

/**************** private functions/ macros *****************/
/**
 * Helper to calculate an eeprom address from a command pointer
 */
#define ScriptBufAddr(x) (EEPROM_SCRIPTBUF_BASE + ((x)*sizeof(struct led_cmd)))

/**
 * Helper to increment a ScriptBuf pointer
 */
#define ScriptBufInc(x) ((x + 1) & SCRIPTCTRL_NUM_CMD_MAX)

/**
 * Setter for ScriptBuf.inLoop
 */
#define ScriptBufSetInLoop(x) { \
	Eeprom_Write(EEPROM_SCRIPTBUF_INLOOP, x); \
	gScriptBuf.inLoop = x; \
}

/**
 * Setter for ScriptBuf.read
 */
#define ScriptBufSetRead(x) { \
	Eeprom_Write(EEPROM_SCRIPTBUF_READ, x); \
	gScriptBuf.read = x; \
}

/**
 * Setter for ScriptBuf.write
 */
#define ScriptBufSetWrite(x) { \
	Eeprom_Write(EEPROM_SCRIPTBUF_WRITE, x); \
	gScriptBuf.write = x; \
}

/**
 * Clear all command from buffer
 */
void ScriptCtrl_Clear(void);

/**
 * save command to eeprom
 */
void ScriptCtrl_Write(struct led_cmd* pCmd);

/* private globals */
struct ScriptBuf gScriptBuf;
struct led_cmd nextCmd;

void ScriptCtrl_Add(struct led_cmd* pCmd)
{
	/* We have to reject all commands until buffer was cleared completely */
	if(gScriptBuf.isClearing)
	{
		return;
	}

	switch(pCmd->cmd)
	{
		case DELETE:
			gScriptBuf.isClearing = TRUE;
			break;
		case LOOP_ON:
			gScriptBuf.loopStart[gScriptBuf.loopDepth] = gScriptBuf.write;
			gScriptBuf.loopDepth++;
			ScriptCtrl_Write(pCmd);
			break;
		case LOOP_OFF:
		{
			gScriptBuf.loopDepth--;
			uns8 loopStart = gScriptBuf.loopStart[gScriptBuf.loopDepth];
			pCmd->data.loopEnd.startIndex = ScriptBufInc(loopStart);
			pCmd->data.loopEnd.depth = gScriptBuf.loopDepth;
			pCmd->data.loopEnd.counter = pCmd->data.loopEnd.numLoops;
			Trace_String("Add LOOP_OFF: ");
			Trace_Hex(gScriptBuf.write);
			Trace_Hex(pCmd->data.loopEnd.startIndex);
			Trace_Hex(pCmd->data.loopEnd.depth);
			Trace_Hex(pCmd->data.loopEnd.counter);
			Trace_String("\n");
			ScriptCtrl_Write(pCmd);
			break;
		}
		default:
			ScriptCtrl_Write(pCmd);
			break;		
	}
}

void ScriptCtrl_Clear(void)
{
	ScriptBufSetInLoop(FALSE);
	ScriptBufSetRead(EEPROM_SCRIPTBUF_BASE);
	ScriptBufSetWrite(EEPROM_SCRIPTBUF_BASE);
	gScriptBuf.execute = gScriptBuf.read;
	gScriptBuf.isClearing = FALSE;
}

void ScriptCtrl_Init(void)
{
	gScriptBuf.inLoop = Eeprom_Read(EEPROM_SCRIPTBUF_INLOOP);
	gScriptBuf.read = Eeprom_Read(EEPROM_SCRIPTBUF_READ);
	gScriptBuf.write = Eeprom_Read(EEPROM_SCRIPTBUF_WRITE);
	gScriptBuf.execute = gScriptBuf.read;
}

void ScriptCtrl_Run(void)
{
	/* delete command was triggered? */
	if(gScriptBuf.isClearing)
	{
		ScriptCtrl_Clear();
	}

	/* cmd available? */
	if(gScriptBuf.execute == gScriptBuf.write)
	{
		return;
	}

	/* read next cmd from buffer */
	uns16 tempAddress = ScriptBufAddr(gScriptBuf.execute);
	Eeprom_ReadBlock((uns8*)&nextCmd, tempAddress, sizeof(nextCmd));

	switch(nextCmd.cmd)
	{
		case LOOP_ON:
			Trace_String("LOOP_ON\n");
			/* move execute pointer to the next command */
			gScriptBuf.execute = ScriptBufInc(gScriptBuf.execute);
			ScriptBufSetInLoop(TRUE);
			break;
		case LOOP_OFF:
			if(LOOP_INFINITE == nextCmd.data.loopEnd.counter)
			{
				Trace_String("End of infinite loop reached\n");
				/* move execute pointer to the top of this loop */
				gScriptBuf.execute = nextCmd.data.loopEnd.startIndex;
			}
			else if(nextCmd.data.loopEnd.counter > 1)
			{
				Trace_String("normal loop iteration");
				Trace_Hex(nextCmd.data.loopEnd.counter);
				Trace_Hex(nextCmd.data.loopEnd.depth);
				Trace_String("\n");
				/* update counter and set execute pointer to start of the loop */
				nextCmd.data.loopEnd.counter--;
				Eeprom_WriteBlock((uns8*)&nextCmd, tempAddress, sizeof(struct led_cmd));

				/* move execute pointer to the top of this loop */
				gScriptBuf.execute = nextCmd.data.loopEnd.startIndex;
			}
			else
			{
				if(0 == nextCmd.data.loopEnd.depth)
				{
					Trace_String("End of top loop reached\n");
					/* move execute pointer to the next command */
					gScriptBuf.execute = ScriptBufInc(gScriptBuf.execute);

					/* delete loop body from buffer */
					ScriptBufSetRead(gScriptBuf.execute);
					ScriptBufSetInLoop(FALSE);
				}
				else
				{
					Trace_String("End of inner loop reached\n");
					/* reinit counter for next iteration */
					nextCmd.data.loopEnd.counter = nextCmd.data.loopEnd.numLoops;
					uns16 tempAddress = ScriptBufAddr(gScriptBuf.execute);
					Eeprom_WriteBlock((uns8*)&nextCmd, tempAddress, sizeof(struct led_cmd));

					/* move execute pointer to the next command */
					gScriptBuf.execute = ScriptBufInc(gScriptBuf.execute);
				}
			}
			break;
		case SET_COLOR:
			Ledstrip_SetColor(&nextCmd.data.set_color);
			/* move execute pointer to the next command */
			gScriptBuf.execute = ScriptBufInc(gScriptBuf.execute);
			if(!gScriptBuf.inLoop)
			{
				ScriptBufSetRead(gScriptBuf.execute);
			}
			break;
		case SET_FADE:
			Ledstrip_SetFade(&nextCmd.data.set_fade);
			/* move execute pointer to the next command */
			gScriptBuf.execute = ScriptBufInc(gScriptBuf.execute);
			if(!gScriptBuf.inLoop)
			{
				ScriptBufSetRead(gScriptBuf.execute);
			}
			break;
	}	
}

void ScriptCtrl_Write(struct led_cmd* pCmd)
{
	uns8 writeNext = ScriptBufInc(gScriptBuf.write);
	if(writeNext != gScriptBuf.read)
	{
		uns16 tempAddress = ScriptBufAddr(gScriptBuf.write);
		Eeprom_WriteBlock((uns8*)pCmd, tempAddress, sizeof(struct led_cmd));
		ScriptBufSetWrite(writeNext);
	}	
}

