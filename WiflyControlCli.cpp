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

#include "WiflyControlCli.h"
#include "WiflyControlCmd.h"
#include <iostream>
#include <cstdlib>

using namespace std;

WiflyControlCli::WiflyControlCli(const char* pAddr, short port, bool useTcp)
: mControl(pAddr, port, useTcp), mRunning(true)
{
}

void WiflyControlCli::Run(void)
{
	ShowHelp();
	string nextCmd;
	while(mRunning)
	{
		cout << "WiflyControlCli: ";
		cin >> nextCmd;

		if("exit" == nextCmd) {
			return;
		}

		if("?" == nextCmd) {
			ShowHelp();
		} else {
			const WiflyControlCmd* pCmd = WiflyControlCmdBuilder::GetCmd(nextCmd);
			if(NULL != pCmd) {
				pCmd->Run(mControl);
				delete pCmd;
			}
		}
	}
}

void WiflyControlCli::ShowHelp(void) const
{
	ControlCmdAddColor addColor;
	ControlCmdBlInfo blInfo;
	ControlCmdBlCrcFlash crcFlash;
	ControlCmdBlEraseFlash eraseFlash;
	ControlCmdBlReadEeprom readEeprom;
	ControlCmdBlReadFlash readFlash;
//TODO implement on demand	ControlCmdBlWriteEeprom writeEeprom;
//TODO	ControlCmdBlWriteFlash writeFlash;
//TODO implement on demand	ControlCmdBlWriteFuse writeFuse;
	ControlCmdBlRunApp runApp;
	ControlCmdClearScript clearScript;
	ControlCmdSetColor setColor;
	ControlCmdSetFade setFade;
	ControlCmdStartBl startBl;
	ControlCmdBlEraseEeprom eraseEeprom;
	ControlCmdBlAutostartEnable autostartEnable;
	cout << "Command reference:" << endl;
	cout << "'?' - this help" << endl;
	cout << "'exit' - terminate cli" << endl;
	cout << addColor << endl;
	cout << blInfo << endl;
	cout << autostartEnable << endl;
	cout << crcFlash << endl;
	cout << eraseFlash << endl;
	cout << eraseEeprom << endl;
	cout << readEeprom << endl;
	cout << readFlash << endl;
	cout << runApp << endl;
	cout << clearScript << endl;
	cout << startBl << endl;
	cout << setColor << endl;
	cout << setFade << endl;
}

#ifdef ANDROID
#include <jni.h>
extern "C" {
void Java_biz_bruenn_WiflyLight_WiflyLightActivity_runClient(JNIEnv* env, jobject ref)
{
	WiflyControl control;
	colorLoop(control);
}
}
#endif

int main(int argc, const char* argv[])
{
	short port = 2000;
#if 1
	const char* pAddr = "127.0.0.1";
	bool useTcp = false;
#else
	const char* pAddr = "192.168.0.14";
	bool useTcp = true;
#endif

	cout << "Usage:   client.bin <ip> <port> [tcp]" << endl;
	cout << "Default: client.bin " << pAddr << " " << port << " --> " << (useTcp ? "tcp" : "udp");
	cout << " connection to " << pAddr << " " << endl;
	if(argc > 1)
	{
		pAddr = argv[1];
		if(argc > 2)
		{
			port = (short)atoi(argv[2]);
			if(argc > 3)
			{
				useTcp = (0 == strncmp(argv[3], "tcp", 3));
			}
		}
	}
	WiflyControlCli cli(pAddr, port, useTcp);
	cli.Run();
	return 0;
}
