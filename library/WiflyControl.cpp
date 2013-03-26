/**
		Copyright (C) 2012, 2013 Nils Weiss, Patrick Bruenn.

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

#include "WiflyControl.h"
#include "crc.h"
#include "trace.h"

#include <algorithm>
#include <cctype>
#include <stddef.h>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <unistd.h>
#include "intelhexclass.h"

#include "WiflyColor.h"

using std::cout;
using std::ifstream;
using std::hex;
using std::stringstream;

static const int g_DebugZones = ZONE_ERROR | ZONE_WARNING | ZONE_INFO | ZONE_VERBOSE;

/**
 * Macro to reduce code redundancy, while converting two 32 bit values into
 * an address array and red, green, blue values. 
 */
#define SetAddrRgb(REF, ADDRESS, ARGB) { \
	REF.addr[3] = ((ADDRESS) & 0xff000000) >> 24; \
	REF.addr[2] = ((ADDRESS) & 0x00ff0000) >> 16; \
	REF.addr[1] = ((ADDRESS) & 0x0000ff00) >> 8; \
	REF.addr[0] = ((ADDRESS) & 0x000000ff); \
	REF.red = ((ARGB) & 0x00ff0000) >> 16; \
	REF.green = ((ARGB) & 0x0000ff00) >> 8; \
	REF.blue = (ARGB) & 0x000000ff; \
}

const std::string WiflyControl::LEDS_ALL{"ffffffff"};

WiflyControl::WiflyControl(uint32_t addr, uint16_t port)
: mSock(addr, port), mProxy(mSock), mTelnet(mSock)
{
	//TODO remove length
	mCmdFrame.length = (uns8)sizeof(struct cmd_set_fade) + 2;
}

/** ------------------------- BOOTLOADER METHODES ------------------------- **/
void WiflyControl::BlEepromErase(void) const
{
	unsigned char buffer[EEPROM_SIZE];
	std::fill_n(buffer, EEPROM_SIZE, 0xff);
	
	BlWriteEeprom((unsigned int)0, &buffer[0], sizeof(buffer));
}

void WiflyControl::BlEnableAutostart(void) const
{
	unsigned char value = 0xff;
	BlWriteEeprom((unsigned int)BL_AUTOSTART_ADDRESS, &value, sizeof(value));
}

std::string WiflyControl::BlReadFwVersion(void) const
{
	BlInfo info;
	BlReadInfo(info);
	
	uint8_t buffer[256];
	std::fill_n(buffer, sizeof(buffer), 0xff);
	
	/* set first block to half of program memory */
	uint32_t readblock = info.GetAddress() / 2;
	uint32_t address = readblock;
	const uint32_t byteblock = 32;
	
	while(readblock > byteblock)
	{
		Trace(ZONE_INFO,"READ@%x", address);
		size_t bytesRead = BlReadFlash(buffer, address, byteblock);
		bool flashEmpty = true;
		
		/* check if all read data contains no program code */
		for(size_t i = 0; i < bytesRead; i++)
			if(buffer[i] != 0xff) flashEmpty = false;
		
		readblock /= 2;
		if(flashEmpty)
			address -= readblock;
		else
			address += readblock;
	}
	
	address -= readblock * 2;
	size_t bytesRead = BlReadFlash(buffer, address, byteblock * 2);
	
	uint8_t *pString = (uint8_t*) &buffer[bytesRead];
	while(*pString == 0xff)
		if(pString-- < &buffer[0])
			return std::string("ERROR");
	
	if(pString - 7 < &buffer[0])
		return std::string("ERROR");
	
	return std::string((const char*)pString - 7, 7);
}

void WiflyControl::BlFlashErase(void) const
{
	BlInfo info;
	BlReadInfo(info);
	
	const unsigned int firstAddress = info.GetAddress() - 1;
	unsigned int address = firstAddress;

	while(address > FLASH_ERASE_BLOCKSIZE * FLASH_ERASE_BLOCKS)
	{
		BlFlashErase(address, FLASH_ERASE_BLOCKS);
		address -= FLASH_ERASE_BLOCKSIZE * FLASH_ERASE_BLOCKS;
	}
	/* now we erased everything until a part of the flash smaller than FLASH_ERASE_BLOCKS * FLASH_ERASE_BLOCKSIZE
	 * so we set our startaddress at the beginning of this block and erase */
	BlFlashErase(FLASH_ERASE_BLOCKS * FLASH_ERASE_BLOCKSIZE -1, FLASH_ERASE_BLOCKS);
}

void WiflyControl::BlFlashErase(const uint32_t endAddress, const uint8_t numPages) const
{
	unsigned char response;
	BlFlashEraseRequest request(endAddress, numPages);

	// always sync for flash erase
	BlRead(request, &response, sizeof(response), true);
	
	// we expect only one byte as response, the command code 0x03
	if(0x03 != response)
	{
		Trace(ZONE_VERBOSE, "Erase flash failed at address: %x\n", endAddress);
		throw BlNoResponseException(request, "Erase flash failed!");
	}
}

size_t WiflyControl::BlRead(BlRequest& req, unsigned char* pResponse, const size_t responseSize, bool doSync) const
{
	unsigned char buffer[BL_MAX_MESSAGE_LENGTH];
	size_t bytesReceived = mProxy.Send(req, buffer, sizeof(buffer), doSync);
	Trace(ZONE_INFO, " %zd:%ld \n", bytesReceived, sizeof(BlInfo));
	TraceBuffer(ZONE_VERBOSE, (uint8_t*)&buffer[0], bytesReceived, "0x%02x, ", "Message: ");
	if(responseSize != bytesReceived)
	{
		throw BlNoResponseException(req);
	}
	memcpy(pResponse, buffer, responseSize);
	return responseSize;
}

size_t WiflyControl::BlReadCrcFlash(unsigned char* pBuffer, unsigned int address, uint16_t numBlocks) const
{
	if(numBlocks * FLASH_ERASE_BLOCKSIZE + address > FLASH_SIZE)
	{
		Trace(ZONE_VERBOSE, "Couldn't performe crc check outside the flash. \n");
		throw WiflyControlException("Couldn't performe crc check outside the flash. \n");
	}
  
	size_t bytesRead;
	size_t sumBytesRead = 0;
	while(numBlocks > FLASH_CRC_BLOCKSIZE)
	{
		BlFlashCrc16Request readRequest(address, FLASH_CRC_BLOCKSIZE);
		bytesRead = BlRead(readRequest, pBuffer, FLASH_CRC_BLOCKSIZE * 2);
		sumBytesRead += bytesRead;
		address += (FLASH_CRC_BLOCKSIZE * FLASH_ERASE_BLOCKSIZE);
		numBlocks -= FLASH_CRC_BLOCKSIZE;
		pBuffer += FLASH_CRC_BLOCKSIZE * 2;
	}

	BlFlashCrc16Request readRequest(address, numBlocks);
	bytesRead = BlRead(readRequest, pBuffer, numBlocks * 2);
	sumBytesRead += bytesRead;
	return sumBytesRead;
}

size_t WiflyControl::BlReadEeprom(uint8_t* pBuffer, uint32_t address, size_t numBytes) const
{
	if(numBytes + address > EEPROM_SIZE)
	{
		Trace(ZONE_VERBOSE, "Couldn't performe read outside the eeprom. \n");
		throw WiflyControlException("Couldn't performe read outside the eeprom. \n");
	}
	
	size_t bytesRead;
	size_t sumBytesRead = 0;
	BlEepromReadRequest readRequest;
	while(numBytes > EEPROM_READ_BLOCKSIZE)
	{
		readRequest.SetAddressNumBytes(address, EEPROM_READ_BLOCKSIZE);
		bytesRead = BlRead(readRequest, pBuffer, EEPROM_READ_BLOCKSIZE);
		sumBytesRead += bytesRead;
		address += EEPROM_READ_BLOCKSIZE;
		numBytes -= EEPROM_READ_BLOCKSIZE;
		pBuffer += EEPROM_READ_BLOCKSIZE;
	}
	readRequest.SetAddressNumBytes(address, numBytes);
	bytesRead = BlRead(readRequest, pBuffer, numBytes);
	sumBytesRead += bytesRead;
	return sumBytesRead;
}

size_t WiflyControl::BlReadFlash(uint8_t* pBuffer, uint32_t address, size_t numBytes) const
{
	if(numBytes + address > FLASH_SIZE)
	{
		Trace(ZONE_VERBOSE, "Couldn't performe read outside the flash. \n");
		throw WiflyControlException("Couldn't performe read outside the flash. \n");
	}
  
	size_t bytesRead;
	size_t sumBytesRead = 0;
	BlFlashReadRequest readRequest;
	while(numBytes > FLASH_READ_BLOCKSIZE)
	{
		readRequest.SetAddressNumBytes(address, FLASH_READ_BLOCKSIZE);
		bytesRead = BlRead(readRequest, pBuffer, FLASH_READ_BLOCKSIZE);
		sumBytesRead += bytesRead;
		address += FLASH_READ_BLOCKSIZE;
		numBytes -= FLASH_READ_BLOCKSIZE;
		pBuffer += FLASH_READ_BLOCKSIZE;
	}

	readRequest.SetAddressNumBytes(address, numBytes);
	bytesRead = BlRead(readRequest, pBuffer, numBytes);
	sumBytesRead += bytesRead;
	return sumBytesRead;
}

void WiflyControl::BlReadInfo(BlInfo& blInfo) const
{
	BlInfoRequest request;
	BlRead(request, reinterpret_cast<unsigned char*>(&blInfo), sizeof(BlInfo));
}

void WiflyControl::BlWriteFlash(unsigned int address, unsigned char* pBuffer, size_t bufferLength) const
{
	if(bufferLength + address > FLASH_SIZE)
	{
		Trace(ZONE_VERBOSE, "Couldn't performe write outside the flash. \n");
		throw WiflyControlException("Couldn't performe write outside the flash. \n");
	}
  
	BlFlashWriteRequest request;
	unsigned char response;
	
	size_t bytesLeft = bufferLength;
	while(bytesLeft > FLASH_WRITE_BLOCKSIZE)
	{
		request.SetData(address, pBuffer, FLASH_WRITE_BLOCKSIZE);

		// we expect only the 0x04 command byte as response
		BlRead(request, &response, sizeof(response));
		if(response != 0x04)
		{
			throw BlNoResponseException(request);
		}
		address += FLASH_WRITE_BLOCKSIZE;
		pBuffer += FLASH_WRITE_BLOCKSIZE;
		bytesLeft -= FLASH_WRITE_BLOCKSIZE;
	}

	request.SetData(address, pBuffer, bytesLeft);
	BlRead(request, &response, sizeof(response));
	if(response != 0x04)
	{
		throw BlNoResponseException(request);
	}
}

void WiflyControl::BlWriteEeprom(unsigned int address, unsigned char* pBuffer, size_t bufferLength) const
{
	if(bufferLength + address > EEPROM_SIZE)
	{
		Trace(ZONE_VERBOSE, "Couldn't performe write outside the eeprom. \n");
		throw WiflyControlException("Couldn't performe write outside the eeprom. \n");
	}
      
	BlEepromWriteRequest request;
	unsigned char response;
	
	size_t bytesLeft = bufferLength;
	while(bytesLeft > EEPROM_WRITE_BLOCKSIZE)
	{
		request.SetData(address, pBuffer, EEPROM_WRITE_BLOCKSIZE);

		// we expect only the 0x06 command byte as response
		BlRead(request, &response, sizeof(response));
		if(response != 0x06)
		{
			throw BlNoResponseException(request);
		}
		
		address += EEPROM_WRITE_BLOCKSIZE;
		pBuffer += EEPROM_WRITE_BLOCKSIZE;
		bytesLeft -= EEPROM_WRITE_BLOCKSIZE;
	}

	request.SetData(address, pBuffer, bytesLeft);
	BlRead(request, &response, sizeof(response));
	if(response != 0x06)
	{
		throw BlNoResponseException(request);
	}
}

void WiflyControl::BlProgramFlash(const std::string& pFilename)
{
	std::ifstream hexFile;
	hexFile.open(const_cast<char*>(pFilename.c_str()), ifstream::in);
	
	if(!hexFile.good())
	{
		throw WiflyControlException("opening '" + pFilename + "' failed");
	}
	
	intelhex hexConverter;
	hexFile >> hexConverter;
	
	BlInfo info;
	BlReadInfo(info);
	
	/*Check if last address of programmcode is not in the bootblock */
	
	unsigned long endAddress;
	
	if(hexConverter.endAddress(&endAddress))
	{
	    if(endAddress >= (unsigned long)(info.GetAddress()))
	    {
			Trace(ZONE_VERBOSE, "endaddress of program code is in bootloader area of the target device flash \n");
			throw WiflyControlException("endaddress of program code is in bootloader area of the target device flash \n");
	    }
	}
	else
	{
		Trace(ZONE_VERBOSE, "can't read endAddress from hexConverter \n");
	    throw WiflyControlException("can't read endAddress from hexConverter \n");
	}
	
	unsigned short word1, word2;
	unsigned int bootAddress;
	unsigned char resetVector[4];
	unsigned char appVector[4];
	
	/* Calculate the resetVector */
	bootAddress = (info.GetAddress() + 2) / 2;
	
	word1 = 0xEF00 | (bootAddress & 0xff);
	word2 = 0xF000 | ((bootAddress >> 8) & 0x0FFF);
	
	resetVector[0] = (unsigned char)word1;
	resetVector[1] = (unsigned char)(word1 >> 8);
	resetVector[2] = (unsigned char)word2;
	resetVector[3] = (unsigned char)(word2 >> 8);
	
	/*Put AppVektor from the beginning of hexfile at the startaddress of the bootloader */
	
	hexConverter.begin();
	if(hexConverter.currentAddress() != 0)
	{
		Trace(ZONE_VERBOSE, "program code does not start at address 0x0000 \n");
		throw WiflyControlException("program code does not start at address 0x0000 \n");
	}
	
	for(unsigned int i = 0; i < sizeof(appVector); i++)
	{
		if(!hexConverter.getData(&appVector[i],(unsigned long)i))
		{
			Trace(ZONE_VERBOSE, "can not read data at address 0x%08lx \n", hexConverter.currentAddress());
		    throw WiflyControlException("can not read data at address " + hexConverter.currentAddress());
		}
	}
	
	BlEnableAutostart();
	BlFlashErase();
	
	unsigned int writeAddress = 0;
	unsigned char flashBuffer[FLASH_SIZE];
	unsigned char nextByte;
	
	if(endAddress > FLASH_SIZE)
	{
		Trace(ZONE_VERBOSE, "endaddress of program code is outside the target device flash\n");
		throw WiflyControlException("endaddress of program code is outside the target device flash\n");
	}
	
	/* Write the resetVector to temporary flashBuffer*/
	memcpy(&flashBuffer[0], &resetVector[0], sizeof(resetVector));
	
	writeAddress += sizeof(resetVector);
	
	for(;writeAddress <= (unsigned int)endAddress; writeAddress++)
	{
		if(hexConverter.getData(&nextByte, writeAddress))
		{
		    flashBuffer[writeAddress] = nextByte;
		}
		else
		{
		    flashBuffer[writeAddress] = 0xff;	
		} 
	}
	BlWriteFlash(0, &flashBuffer[0], (size_t)endAddress+1);
	
	/* we always have to write a FLASH_WRITE Block when we wanna write to device flash,
	 * so we have to pack the appVector at the end of a Block of data */
	
	unsigned char appVecBuf[FLASH_WRITE_BLOCKSIZE];
	
	for(int i = 0; i < FLASH_WRITE_BLOCKSIZE; i++)
	{
		appVecBuf[i] = 0xff;
	}
	
	memcpy(&appVecBuf[sizeof(appVecBuf) - sizeof(appVector)], &appVector[0], sizeof(appVector));
	BlWriteFlash(info.GetAddress() - FLASH_WRITE_BLOCKSIZE, &appVecBuf[0], FLASH_WRITE_BLOCKSIZE);
}

void WiflyControl::BlRunApp(void) const
{
	BlRunAppRequest request;
	unsigned char buffer[32];
	size_t bytesRead = BlRead(request, &buffer[0], 6);
	
	Trace(ZONE_VERBOSE, "We got %zd bytes response.\n", bytesRead);
	if(4 <= bytesRead)
	{
		struct response_frame *pResponse = (response_frame*)&buffer[0];
		if(pResponse->cmd == FW_STARTED) return;
	}
	throw BlNoResponseException(request);
}

std::string WiflyControl::ConfGetSsid(void) const
{
	std::string result{};
	if(mTelnet.Open())
	{
		mTelnet.RecvString("get wlan\r\n", "SSID=", result);
		mTelnet.Close(false);
	}
	return result;
}

bool WiflyControl::ConfSetDefaults(void) const
{
	static const std::string commands[] = {
		"set broadcast interval 1\r\n",    // to support fast broadcast recognition
		"set comm close 0\r\n",            // Disable *CLOS* string
		"set comm open 0\r\n",             // Disable *OPEN* string
		"set comm remote 0\r\n",           // Disable *Hello* string
//		"set dns name rn.microchip.com\r\n",	// set dns of updateserver
		"set ip dhcp 1\r\n",               // enable DHCP client
		"set ftp address 169.254.7.57\r\n",// configure localhost as ftp server in ad-hoc connection
		"set ftp pass Pass123\r\n",        // configure ftp password
		"set ftp user roving\r\n",         // configure ftp username
		"set opt deviceid Wifly_Light\r\n",// Set deviceid which appears in broadcastmsg to "Wifly_Light"
		"set uart baud 115200\r\n",        // PIC uart parameter
		"set uart flow 0\r\n",             // PIC uart parameter
		"set uart mode 0\r\n",             // PIC uart parameter
		"set wlan auth 4\r\n",             // use WPA2 protection
		"set wlan join 1\r\n",             // scan for ap and auto join
		"set wlan rate 0\r\n",             // slowest datarate but highest range
		"set wlan tx 12\r\n",              // Set the Wi-Fi transmit power to maximum
	};

	if(!mTelnet.Open())
	{
		Trace(ZONE_ERROR, "open telnet connection failed\n");
		return false;
	}

	for(size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
	{
		if(!mTelnet.Send(commands[i]))
		{
			Trace(ZONE_ERROR, "command: '%s' failed -> exit without saving\n", commands[i].data());
			return mTelnet.Close(false);
		} 
	}
	return mTelnet.Close(true);
}

bool WiflyControl::ConfSetWlan(const std::string& phrase, const std::string& ssid) const
{
	static const size_t PHRASE_MAX = 63;
	static const size_t SSID_MAX = 32;

	if((phrase.size() < 1) || (phrase.size() > PHRASE_MAX) || 0 != std::count_if(phrase.begin(), phrase.end(), iscntrl))
	{
		Trace(ZONE_WARNING, "Invalid wlan passphrase '%s'\n", phrase.data());
		return false;
	}
	
	if((ssid.size() < 1) || (ssid.size() > SSID_MAX))
	{
		Trace(ZONE_WARNING, "Invalid wlan ssid '%s'\n", phrase.data());
		return false;
	}

	if(!mTelnet.Open())
	{
		Trace(ZONE_ERROR, "open telnet connection failed\n");
		return false;
	}

	if(!mTelnet.SendString("set wlan phrase ", phrase))
	{
		Trace(ZONE_ERROR, "set wlan phrase to '%s' failed\n", phrase.data());
		mTelnet.Close(false);
		return false;
	}

	if(!mTelnet.SendString("set wlan ssid ", ssid))
	{
		Trace(ZONE_ERROR, "set wlan ssid to '%s' failed\n", ssid.data());
		mTelnet.Close(false);
		return false;
	}
	return mTelnet.Close(true);
}

void WiflyControl::FwClearScript(WiflyResponse& response)
{
	mCmdFrame.led.cmd = CLEAR_SCRIPT;
	FwSend(&mCmdFrame, 0, response);
}

CycletimeResponse& WiflyControl::FwGetCycletime(CycletimeResponse& response)
{
	mCmdFrame.led.cmd = GET_CYCLETIME;
	FwSend(&mCmdFrame, 0, response);
	return response;
}

void WiflyControl::FwGetRtc(RtcResponse& response)
{    
	mCmdFrame.led.cmd = GET_RTC;
	FwSend(&mCmdFrame, 0, response);
}

TracebufferResponse& WiflyControl::FwGetTracebuffer(TracebufferResponse& response)
{
	mCmdFrame.led.cmd = GET_TRACE;
	FwSend(&mCmdFrame, 0, response);
	return response;
}

FirmwareVersionResponse& WiflyControl::FwGetVersion(FirmwareVersionResponse& response)
{
	mCmdFrame.led.cmd = GET_FW_VERSION;
	FwSend(&mCmdFrame, 0, response);
	return response;
}

void WiflyControl::FwLoopOn(WiflyResponse& response)
{
	mCmdFrame.led.cmd = LOOP_ON;
	FwSend(&mCmdFrame, 0, response);
}

void WiflyControl::FwLoopOff(WiflyResponse& response, unsigned char numLoops)
{
	mCmdFrame.led.cmd = LOOP_OFF;
	mCmdFrame.led.data.loopEnd.numLoops = numLoops;
	
	FwSend(&mCmdFrame, sizeof(cmd_set_fade), response);
}

WiflyResponse& WiflyControl::FwSend(struct cmd_frame* pFrame, size_t length, WiflyResponse& response) const
{
	pFrame->length = length + 2; //add cmd and length byte
	response_frame buffer;
	const size_t maxCrcRetries = 5;
	size_t numCrcRetries = 0;
	do
	{
		numCrcRetries++;
		int bytesRead = mProxy.Send(pFrame, &buffer, sizeof(buffer), false);
	
		TraceBuffer(ZONE_VERBOSE, (uint8_t*)&buffer, (size_t)bytesRead, "%02x ", "We got %d bytes response.\nMessage: ", bytesRead);
		response.Init(&buffer, bytesRead);
	
		Trace(ZONE_INFO, "CRC-Check %s \n", (response.IsCrcCheckFailed() ? "failed" : "successful"));
	}while (response.IsCrcCheckFailed() && (numCrcRetries <  maxCrcRetries));
	
	if(!response.IsValid()) throw FwNoResponseException(pFrame);
	if(response.IsScriptBufferFull()) throw ScriptBufferFullException(pFrame);
	
	return response;
}

void WiflyControl::FwSetColorDirect(WiflyResponse& response, unsigned char* pBuffer, size_t bufferLength)
{
	if(pBuffer == NULL) throw std::bad_alloc();
  
	mCmdFrame.led.cmd = SET_COLOR_DIRECT;
	static const size_t maxBufferLength = NUM_OF_LED * 3;
	bufferLength = std::min(bufferLength, maxBufferLength);
	memcpy(mCmdFrame.led.data.set_color_direct.ptr_led_array, pBuffer, bufferLength);
	memset(mCmdFrame.led.data.set_color_direct.ptr_led_array + bufferLength, 0, maxBufferLength - bufferLength);
	FwSend(&mCmdFrame, sizeof(struct cmd_set_color_direct),response);
}

void WiflyControl::FwSetFade(WiflyResponse& response, uint32_t argb, uint32_t fadeTmms, uint32_t addr, bool parallelFade)
{
	fadeTmms = fadeTmms / calibrationValue;
	if(fadeTmms < 4) fadeTmms = 4;
	
	mCmdFrame.led.cmd = SET_FADE;
	SetAddrRgb(mCmdFrame.led.data.set_fade, addr, argb);
	mCmdFrame.led.data.set_fade.fadeTmms = htons((uint16_t)fadeTmms);
	mCmdFrame.led.data.set_fade.parallelFade = parallelFade;
	FwSend(&mCmdFrame, sizeof(cmd_set_fade), response);
}

void WiflyControl::FwSetFade(WiflyResponse& response, const string& rgb, uint32_t fadeTmms, const string& addr, bool parallelFade)
{
	FwSetFade(response, 0xff000000 | WiflyColor::ToARGB(rgb), fadeTmms, WiflyColor::ToARGB(addr), parallelFade);
}

void WiflyControl::FwSetRtc(SimpleResponse& response, struct tm* timeValue)
{
	if(timeValue == NULL) throw std::bad_alloc();
  
  	mCmdFrame.led.cmd = SET_RTC;
	mCmdFrame.led.data.set_rtc.tm_sec  = (uns8) timeValue->tm_sec;
	mCmdFrame.led.data.set_rtc.tm_min  = (uns8) timeValue->tm_min;
	mCmdFrame.led.data.set_rtc.tm_hour = (uns8) timeValue->tm_hour;
	mCmdFrame.led.data.set_rtc.tm_mday = (uns8) timeValue->tm_mday;
	mCmdFrame.led.data.set_rtc.tm_mon  = (uns8) timeValue->tm_mon;
	mCmdFrame.led.data.set_rtc.tm_year = (uns8) timeValue->tm_year;
	mCmdFrame.led.data.set_rtc.tm_wday = (uns8) timeValue->tm_wday;
	
	FwSend(&mCmdFrame, sizeof(struct rtc_time),response);
}

void WiflyControl::FwSetWait(WiflyResponse& response, uint32_t waitTmms)
{
	mCmdFrame.led.cmd = WAIT;
	mCmdFrame.led.data.wait.waitTmms = htons((uint16_t)(waitTmms / calibrationValue));
	
	FwSend(&mCmdFrame, sizeof(cmd_set_fade), response);
}

void WiflyControl::FwStressTest(void)
{
	SimpleResponse clrResp(CLEAR_SCRIPT);
	SimpleResponse setColorDirectResp(SET_COLOR_DIRECT);
	
	FwClearScript(clrResp);

	uns8 ledArr[NUM_OF_LED * 3];
	uns8 color = 0;
	while(true){
		color++;
		std::fill_n(ledArr, sizeof(ledArr), color);
		FwSetColorDirect(setColorDirectResp, ledArr, sizeof(ledArr));
	}
}

std::string WiflyControl::ExtractFwVersion(const std::string& pFilename) const
{
	std::ifstream hexFile;
	hexFile.open(const_cast<char*>(pFilename.c_str()), ifstream::in);
	
	if(!hexFile.good())
	{
		throw WiflyControlException("opening '" + pFilename + "' failed");
	}
	
	intelhex hexConverter;
	hexFile >> hexConverter;
	unsigned long endAddress;
	unsigned char buffer[16];
	std::fill_n(buffer, sizeof(buffer), 0x00);
	
	if(!hexConverter.endAddress(&endAddress))
	{
		Trace(ZONE_VERBOSE, "can't read endAddress from hexConverter \n");
	    throw WiflyControlException("can't read endAddress from hexConverter \n");
	}
	endAddress -= 7;
	for(unsigned int i = 0; i < 7; i++)
	{
		hexConverter.getData(&buffer[i], endAddress++);
	}
	return std::string((const char*)&buffer[0], 7);
}


void WiflyControl::FwTest(void)
{
#if 1
	static const timespec sleepTime{0, 50000000};
	SimpleResponse setFadeResp(SET_FADE);
	uint32_t color = 0xff;
	for(size_t i = 0; i < 100; ++i)
	{
		color = ((color & 0xff) << 24) | (color >> 8);
		FwSetFade(setFadeResp, color);
		nanosleep(&sleepTime, NULL);
	}
#else
	SimpleResponse clrResp(CLEAR_SCRIPT);
	SimpleResponse loopOffResp(LOOP_OFF);
	SimpleResponse loopOnResp(LOOP_ON);
	SimpleResponse setFadeResp(SET_FADE);
	SimpleResponse setWaitResp(WAIT);
		
	FwClearScript(clrResp);
	
	WiflyColor ledColor(0xffffffff);
	
	FwClearScript(clrResp);
	FwLoopOn(loopOnResp);
	FwSetFade(setFadeResp, WiflyColor::RED, 2000, 0xFFFFFFFF, false);
	
	uint32_t bitMask = 0x01;
	for(unsigned int i = 0; i < NUM_OF_LED; i++)
	{
		ledColor.red((uint8_t)((0xff / NUM_OF_LED) * i));
		ledColor.green((uint8_t)((0xff / NUM_OF_LED) * i));
		ledColor.blue(0xff);
		FwSetFade(setFadeResp, bitMask, ledColor.argb(), 20000 + i * 200, true);
		bitMask = bitMask << 1;
	}
	
	FwSetWait(setWaitResp, 30000);
	FwSetFade(setFadeResp, WiflyColor::GREEN,2000, 0xFFFFFFFF, false);
	FwSetFade(setFadeResp, WiflyColor::RED,  2000, 0x000000FF, true);
	FwSetFade(setFadeResp, WiflyColor::GREEN,2000, 0x0000FF00, true);
	FwSetFade(setFadeResp, WiflyColor::BLUE, 2000, 0x00FF0000, true);
	FwSetFade(setFadeResp, WiflyColor::WHITE,2000, 0xFF000000, false);
	FwSetWait(setWaitResp, 2000);
	FwSetFade(setFadeResp, WiflyColor::BLACK,2000, 0xFFFFFFFF, false);
	FwLoopOff(loopOffResp, 0);
#endif
}

void WiflyControl::FwStartBl(SimpleResponse& response)
{
	mCmdFrame.led.cmd = START_BL;
	FwSend(&mCmdFrame, 0, response);
}

