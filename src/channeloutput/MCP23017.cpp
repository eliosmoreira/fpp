/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2022 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the GPL v2 as described in the
 * included LICENSE.GPL file.
 */

/*
 * This driver requires a MCP23017 I2C chip with the following connections:
 *
 * MCP Pin      Connection
 * ----------   ------------------------
 *   9 (VDD)  - Pi Pin 4 (5V)
 *  10 (VSS)  - Pi Pin 6 (Ground)
 *  12 (SCL)  - Pi Pin 5 (SCL)
 *  13 (SDA)  - Pi Pin 3 (SDA)
 *  18 (Reset)- Pi Pin 4 (5V)
 *  15 (A0)   - Pi Pin 6 (Ground) (for 0x20 hex, 32 int device ID)
 *  16 (A1)   - Pi Pin 6 (Ground) (this can be configured in channelouputs.json)
 *  17 (A2)   - Pi Pin 6 (Ground) (if more than one MCP23017 is attached)
 *
 */

/*
 * Sample channeloutputs.json config
 *
 * {
 *       "channelOutputs": [
 *               {
 *                       "type": "MCP23017",
 *                       "enabled": 1,
 *                       "deviceID": 32,
 *                       "startChannel": 1,
 *                       "channelCount": 16
 *               }
 *       ]
 * }
 *
 */
#include "fpp-pch.h"

#include "MCP23017.h"

#define MCP23x17_IOCON 0x0A
#define MCP23x17_IODIRA 0x00
#define MCP23x17_IODIRB 0x01
#define MCP23x17_GPIOA 0x12
#define MCP23x17_GPIOB 0x13

#define IOCON_INIT 0x20

#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)     \
    (byte & 0x80 ? 1 : 0),     \
        (byte & 0x40 ? 1 : 0), \
        (byte & 0x20 ? 1 : 0), \
        (byte & 0x10 ? 1 : 0), \
        (byte & 0x08 ? 1 : 0), \
        (byte & 0x04 ? 1 : 0), \
        (byte & 0x02 ? 1 : 0), \
        (byte & 0x01 ? 1 : 0)

#include "Plugin.h"
class MCP23017Plugin : public FPPPlugins::Plugin, public FPPPlugins::ChannelOutputPlugin {
public:
    MCP23017Plugin() :
        FPPPlugins::Plugin("MCP23017") {
    }
    virtual ChannelOutput* createChannelOutput(unsigned int startChannel, unsigned int channelCount) override {
        return new MCP23017Output(startChannel, channelCount);
    }
};

extern "C" {
FPPPlugins::Plugin* createPlugin() {
    return new MCP23017Plugin();
}
}

/*
 *
 */
MCP23017Output::MCP23017Output(unsigned int startChannel, unsigned int channelCount) :
    ChannelOutput(startChannel, channelCount),
    i2c(nullptr) {
    LogDebug(VB_CHANNELOUT, "MCP23017Output::MCP23017Output(%u, %u)\n",
             startChannel, channelCount);
}

/*
 *
 */
MCP23017Output::~MCP23017Output() {
    LogDebug(VB_CHANNELOUT, "MCP23017Output::~MCP23017Output()\n");
    if (i2c) {
        delete i2c;
    }
}

/*
 *
 */
int MCP23017Output::Init(Json::Value config) {
    LogDebug(VB_CHANNELOUT, "MCP23017Output::Init(JSON)\n");

    if (config["deviceID"].isString()) {
        m_deviceID = std::atoi(config["deviceID"].asString().c_str());
    } else {
        m_deviceID = config["deviceID"].asInt();
    }

    if (m_deviceID < 0x20 || m_deviceID > 0x27) {
        LogErr(VB_CHANNELOUT, "Invalid MSCP23017 Address: %X\n", m_deviceID);
        return 0;
    }

    i2c = new I2CUtils(1, m_deviceID);
    if (!i2c->isOk()) {
        LogErr(VB_CHANNELOUT, "Error opening I2C device for MCP23017 output\n");
        return 0;
    }

    // Initialize
    i2c->writeByteData(MCP23x17_IOCON, IOCON_INIT);

    // Enable all pins for output
    i2c->writeByteData(MCP23x17_IODIRA, 0b00000000);
    i2c->writeByteData(MCP23x17_IODIRB, 0b00000000);

    return ChannelOutput::Init(config);
}

/*
 *
 */
int MCP23017Output::Close(void) {
    LogDebug(VB_CHANNELOUT, "MCP23017Output::Close()\n");

    return ChannelOutput::Close();
}

/*
 *
 */
int MCP23017Output::SendData(unsigned char* channelData) {
    LogExcess(VB_CHANNELOUT, "MCP23017Output::SendData(%p)\n", channelData);

    unsigned char* c = channelData;
    int bank = 0;
    int bankbox = 0;
    int byte1 = 0;
    int byte2 = 0;
    int ch = 0;

    for (int x = 0; ch < m_channelCount; x++, ch++) {
        if (*(c++)) {
            if (x < 8)
                byte1 |= 0x1 << x;
            else
                byte2 |= 0x1 << (x - 8);
        }
    }

    LogExcess(VB_CHANNELOUT,
              "Byte1: 0b" BYTETOBINARYPATTERN ", Byte2: 0b" BYTETOBINARYPATTERN "\n",
              BYTETOBINARY(byte1), BYTETOBINARY(byte2));

    i2c->writeByteData(MCP23x17_GPIOA, byte1);
    i2c->writeByteData(MCP23x17_GPIOB, byte2);

    return m_channelCount;
}

/*
 *
 */
void MCP23017Output::DumpConfig(void) {
    LogDebug(VB_CHANNELOUT, "MCP23017Output::DumpConfig()\n");

    LogDebug(VB_CHANNELOUT, "    deviceID: %X\n", m_deviceID);

    ChannelOutput::DumpConfig();
}
