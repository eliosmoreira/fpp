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
 *   Packet Format: (based on captures from LED Studio 12.x to RV908T card)
 *
 *   FIXME - add packet format here once figured out
 *
 *   First packet of data frame
 *   - bytes  0 -  5 = Dst MAC (00:00:00:00:00:fe)
 *   - bytes  6 - 11 = Src MAC (PC's MAC) (must be same as used to configure)
 *   - bytes 12 - 13 = Protocol (0xAA55)
 *   - bytes 14 - 15 = 2-byte packet number for frame (LSB first)
 *   - byte  16      = 0x00
 *   - byte  17      = 0x00
 *   - byte  18      = 0x00
 *   - byte  19      = 0x00
 *   - byte  20      = 0x00
 *   - byte  21      = 0x00
 *   - byte  22      = 0x96
 *   - byte  23      = 0x00
 *   - byte  24      = 0x00
 *   - byte  25      = 0x00
 *   - byte  26      = 0x85 = (133)
 *   - byte  27      = 0x0f = ( 15)
 *   - byte  28      = 0xff = (255) // something to do with brightness
 *   - byte  29      = 0xff = (255) // something to do with brightness
 *   - byte  30      = 0xff = (255) // something to do with brightness
 *   - byte  31      = 0xff = (255) // something to do with brightness
 *   - byte  32      = 0x00
 *   - byte  33      = 0x00
 *   - byte  34      = 0x00
 *   - byte  35      = 0x00
 *   - byte  36      = 0x00
 *   - byte  37      = 0x00
 *   - byte  38      = 0x00
 *   - bytes 39 - 44 = Src MAC (PC's MAC) (see note above)
 *   - byte  45      = 0xd2 = (210)
 *   - bytes 46 - 1485 = RGB Data
 */
#include "fpp-pch.h"

#ifndef PLATFORM_OSX
#include <linux/if_packet.h>
#include <netinet/ether.h>
#else
#include <net/bpf.h>
#include <net/if_dl.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#endif

#include "../SysSocket.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "Linsn-RV9.h"
#include "overlays/PixelOverlay.h"

#include "Plugin.h"
class LinsnRV9Plugin : public FPPPlugins::Plugin, public FPPPlugins::ChannelOutputPlugin {
public:
    LinsnRV9Plugin() :
        FPPPlugins::Plugin("LinsnRV9") {
    }
    virtual ChannelOutput* createChannelOutput(unsigned int startChannel, unsigned int channelCount) override {
        return new LinsnRV9Output(startChannel, channelCount);
    }
};

extern "C" {
FPPPlugins::Plugin* createPlugin() {
    return new LinsnRV9Plugin();
}
}

/*
 *
 */
LinsnRV9Output::LinsnRV9Output(unsigned int startChannel, unsigned int channelCount) :
    ChannelOutput(startChannel, channelCount),
    m_width(0),
    m_height(0),
    m_colorOrder(kColorOrderRGB),
    m_fd(-1),
    m_header(NULL),
    m_data(NULL),
    m_pktSize(LINSNRV9_BUFFER_SIZE),
    m_framePackets(0),
    m_panelWidth(0),
    m_panelHeight(0),
    m_panels(0),
    m_rows(0),
    m_outputs(0),
    m_longestChain(0),
    m_invertedData(0),
    m_matrix(NULL),
    m_panelMatrix(NULL),
    m_formatIndex(-1) {
    LogDebug(VB_CHANNELOUT, "LinsnRV9Output::LinsnRV9Output(%u, %u)\n",
             startChannel, channelCount);

    // NOTE: These must be ordered smallest to largest
    //struct FormatCode fc_e0 = { 0xe0, 32, 32, 96, 0x01 };
    //m_formatCodes.push_back(fc_e0);

    //struct FormatCode fc_e1 = { 0xe1, 64, 32, 96, 0x03 );
    //m_formatCodes.push_back(fc_e1);

    // maybe some unknown range here
    //struct FormatCode fc_eX = { 0xeX, ??, ??, ??, 0x07 };
    //m_formatCodes.push_back(fc_eX);

    struct FormatCode fc_d2 = { 0xd2, 512, 256, 96, 0x0f };
    m_formatCodes.push_back(fc_d2);

    struct FormatCode fc_c2 = { 0xc2, 1024, 512, 1632, 0x1f };
    m_formatCodes.push_back(fc_c2);
}

/*
 *
 */
LinsnRV9Output::~LinsnRV9Output() {
    LogDebug(VB_CHANNELOUT, "LinsnRV9Output::~LinsnRV9Output()\n");

    if (m_fd >= 0)
        close(m_fd);

    if (m_outputFrame)
        delete[] m_outputFrame;
}

/*
 *
 */
int LinsnRV9Output::Init(Json::Value config) {
    LogDebug(VB_CHANNELOUT, "LinsnRV9Output::Init(JSON)\n");

    m_panelWidth = config["panelWidth"].asInt();
    m_panelHeight = config["panelHeight"].asInt();

    if (!m_panelWidth)
        m_panelWidth = 32;

    if (!m_panelHeight)
        m_panelHeight = 16;

    m_invertedData = config["invertedData"].asInt();

    m_colorOrder = ColorOrderFromString(config["colorOrder"].asString());

    m_panelMatrix =
        new PanelMatrix(m_panelWidth, m_panelHeight, m_invertedData);

    if (!m_panelMatrix) {
        LogErr(VB_CHANNELOUT, "Unable to create PanelMatrix\n");
        return 0;
    }

    for (int i = 0; i < config["panels"].size(); i++) {
        Json::Value p = config["panels"][i];
        char orientation = 'N';
        std::string o = p["orientation"].asString();
        if (o != "") {
            orientation = o[0];
        }

        if (p["colorOrder"].asString() == "")
            p["colorOrder"] = ColorOrderToString(m_colorOrder);

        m_panelMatrix->AddPanel(p["outputNumber"].asInt(),
                                p["panelNumber"].asInt(), orientation,
                                p["xOffset"].asInt(), p["yOffset"].asInt(),
                                ColorOrderFromString(p["colorOrder"].asString()));

        if (p["outputNumber"].asInt() > m_outputs)
            m_outputs = p["outputNumber"].asInt();

        if (p["panelNumber"].asInt() > m_longestChain)
            m_longestChain = p["panelNumber"].asInt();
    }

    // Both of these are 0-based, so bump them up by 1 for comparisons
    m_outputs++;
    m_longestChain++;

    m_panels = m_panelMatrix->PanelCount();

    m_rows = m_outputs * m_panelHeight;

    m_width = m_panelMatrix->Width();
    m_height = m_panelMatrix->Height();

    m_formatIndex = -1;
    for (int i = 0; i < m_formatCodes.size() && (m_formatIndex == -1); i++) {
        if ((m_formatCodes[i].width >= m_width) &&
            (m_formatCodes[i].height >= m_height)) {
            m_formatIndex = i;
        }
    }

    if (m_formatIndex == -1) {
        LogErr(VB_CHANNELOUT, "Error finding valid format code for width: %d\n", m_width);
        return 0;
    }

    m_channelCount = m_width * m_height * 3;

    // Calculate max frame size and allocate
    m_outputFrameSize = m_formatCodes[m_formatIndex].width * m_formatCodes[m_formatIndex].height * 3 + m_formatCodes[m_formatIndex].dataOffset;
    m_outputFrame = new char[m_outputFrameSize];

    // Calculate the minimum number of packets to send the height we need
    m_framePackets = ((m_height * m_formatCodes[m_formatIndex].width + m_formatCodes[m_formatIndex].dataOffset) / 480) + 1;

    m_matrix = new Matrix(m_startChannel, m_width, m_height);

    if (config.isMember("subMatrices")) {
        for (int i = 0; i < config["subMatrices"].size(); i++) {
            Json::Value sm = config["subMatrices"][i];

            m_matrix->AddSubMatrix(
                sm["enabled"].asInt(),
                sm["startChannel"].asInt() - 1,
                sm["width"].asInt(),
                sm["height"].asInt(),
                sm["xOffset"].asInt(),
                sm["yOffset"].asInt());
        }
    }

    float gamma = 1.0;
    if (config.isMember("gamma")) {
        gamma = atof(config["gamma"].asString().c_str());
    }
    if (gamma < 0.01 || gamma > 50.0) {
        gamma = 1.0;
    }
    for (int x = 0; x < 256; x++) {
        float f = x;
        f = 255.0 * pow(f / 255.0f, gamma);
        if (f > 255.0) {
            f = 255.0;
        }
        if (f < 0.0) {
            f = 0.0;
        }
        m_gammaCurve[x] = round(f);
    }

    if (config.isMember("interface"))
        m_ifName = config["interface"].asString();
    else
        m_ifName = "eth1";

    GetSrcMAC();

    if (config.isMember("sourceMAC")) {
        std::string srcMAC = config["sourceMAC"].asString();
        std::vector<std::string> macParts = split(srcMAC, ':');

        m_srcMAC[0] = (unsigned char)(strtol(macParts[0].c_str(), NULL, 16));
        m_srcMAC[1] = (unsigned char)(strtol(macParts[1].c_str(), NULL, 16));
        m_srcMAC[2] = (unsigned char)(strtol(macParts[2].c_str(), NULL, 16));
        m_srcMAC[3] = (unsigned char)(strtol(macParts[3].c_str(), NULL, 16));
        m_srcMAC[4] = (unsigned char)(strtol(macParts[4].c_str(), NULL, 16));
        m_srcMAC[5] = (unsigned char)(strtol(macParts[5].c_str(), NULL, 16));
    }

    ////////////////////////////
    // Set main data packet
    memset(m_buffer, 0, LINSNRV9_BUFFER_SIZE);
    m_eh = (struct ether_header*)m_buffer;
    m_header = m_buffer + sizeof(struct ether_header);
    m_data = m_header + LINSNRV9_HEADER_SIZE;
    m_eh->ether_type = htons(0xAA55);

    m_pktSize = sizeof(struct ether_header) + LINSNRV9_HEADER_SIZE + LINSNRV9_DATA_SIZE;

#ifndef PLATFORM_OSX
    // Open our raw socket
    if ((m_fd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1) {
        LogErr(VB_CHANNELOUT, "Error creating raw socket: %s\n", strerror(errno));
        return 0;
    }

    memset(&m_if_idx, 0, sizeof(struct ifreq));
    strcpy(m_if_idx.ifr_name, m_ifName.c_str());
    if (ioctl(m_fd, SIOCGIFINDEX, &m_if_idx) < 0) {
        LogErr(VB_CHANNELOUT, "Error getting index of %s inteface: %s\n",
               m_ifName.c_str(), strerror(errno));
        return 0;
    }

    m_sock_addr.sll_ifindex = m_if_idx.ifr_ifindex;
    m_sock_addr.sll_halen = ETH_ALEN;
    memcpy(m_sock_addr.sll_addr, m_eh->ether_dhost, 6);
#else
    char buf[11] = { 0 };
    int i = 0;
    for (int i = 0; i < 255; i++) {
        sprintf(buf, "/dev/bpf%i", i);
        m_fd = open(buf, O_RDWR);
        if (m_fd != -1) {
            break;
        }
    }
    if (m_fd == -1) {
        LogErr(VB_CHANNELOUT, "Error opening bpf file: %s\n", strerror(errno));
        return 0;
    }

    struct ifreq bound_if;
    memset(&bound_if, 0, sizeof(bound_if));
    strcpy(bound_if.ifr_name, m_ifName.c_str());
    if (ioctl(m_fd, BIOCSETIF, &bound_if) > 0) {
        LogErr(VB_CHANNELOUT, "Cannot bind bpf device to physical device %s, exiting\n", m_ifName.c_str());
    }
    int yes = 1;
    ioctl(m_fd, BIOCSHDRCMPLT, &yes);
#endif

    // Send discovery/wakeup packets
    SetDiscoveryMACs(m_buffer);

    m_buffer[22] = 0x96;

    m_buffer[26] = 0x85;
    m_buffer[27] = m_formatCodes[m_formatIndex].d27;
    m_buffer[28] = 0xff; // something to do with brightness
    m_buffer[29] = 0xff; // something to do with brightness
    m_buffer[30] = 0xff; // something to do with brightness
    m_buffer[31] = 0xff; // something to do with brightness

    m_buffer[45] = m_formatCodes[m_formatIndex].code;

    //	m_buffer[47] = 0xfe; // This was returned by receiver in byte offset 0x0a
    //	m_buffer[48] = 0xff; // This was returned by reciever in byte offset 0x0b
    // Have seen some cards return 0x00 and 0xe0 instead
    //	m_buffer[47] = 0x00; // This was returned by receiver in byte offset 0x0a
    //	m_buffer[48] = 0x00; // This was returned by reciever in byte offset 0x0b

    for (int i = 0; i < 2; i++) {
        if (Send(m_buffer, LINSNRV9_BUFFER_SIZE) < 0) {
            LogErr(VB_CHANNELOUT, "Error sending row data packet: %s\n", strerror(errno));
            return 0;
        }

        m_buffer[47] = 0xfe; // This value is returned by receiver in byte offset 0x0a
        m_buffer[48] = 0xff; // This value is returned by reciever in byte offset 0x0b

        //		usleep(200000);
        sleep(1);
    }

    // Set MACs for data packets
    // FIXME, this should use the MAC received during discovery
    SetHostMACs(m_buffer);

    if (PixelOverlayManager::INSTANCE.isAutoCreatePixelOverlayModels()) {
        std::string dd = "LED Panels";
        if (config.isMember("description")) {
            dd = config["description"].asString();
        }
        std::string desc = dd;
        int count = 0;
        while (PixelOverlayManager::INSTANCE.getModel(desc) != nullptr) {
            count++;
            desc = dd + "-" + std::to_string(count);
        }
        PixelOverlayManager::INSTANCE.addAutoOverlayModel(desc,
                                                          m_startChannel, m_channelCount, 3,
                                                          "H", m_invertedData ? "BL" : "TL",
                                                          m_height, 1);
    }
    return ChannelOutput::Init(config);
}

/*
 *
 */
int LinsnRV9Output::Close(void) {
    LogDebug(VB_CHANNELOUT, "LinsnRV9Output::Close()\n");

    return ChannelOutput::Close();
}

void LinsnRV9Output::GetRequiredChannelRanges(const std::function<void(int, int)>& addRange) {
    addRange(m_startChannel, m_startChannel + m_channelCount - 1);
}

void LinsnRV9Output::OverlayTestData(unsigned char* channelData, int cycleNum, int testType) {
    for (int output = 0; output < m_outputs; output++) {
        int panelsOnOutput = m_panelMatrix->m_outputPanels[output].size();
        for (int i = 0; i < panelsOnOutput; i++) {
            int panel = m_panelMatrix->m_outputPanels[output][i];

            m_panelMatrix->m_panels[panel].drawTestPattern(channelData + m_startChannel, cycleNum, testType);
            m_panelMatrix->m_panels[panel].drawNumber(output + 1, m_panelWidth/2 + 1, m_panelHeight > 16 ? 2 : 1, channelData + m_startChannel);
            m_panelMatrix->m_panels[panel].drawNumber(i + 1, m_panelWidth/2 + 8, m_panelHeight > 16 ? 2 : 1, channelData + m_startChannel);
        }
    }
}
    
/*
 *
 */
void LinsnRV9Output::PrepData(unsigned char* channelData) {
    m_matrix->OverlaySubMatrices(channelData);

    unsigned char* r = NULL;
    unsigned char* g = NULL;
    unsigned char* b = NULL;
    unsigned char* s = NULL;
    unsigned char* dst = NULL;
    int pw3 = m_panelWidth * 3;

    channelData += m_startChannel; // FIXME, this function gets offset 0

    for (int output = 0; output < m_outputs; output++) {
        int panelsOnOutput = m_panelMatrix->m_outputPanels[output].size();

        for (int i = 0; i < panelsOnOutput; i++) {
            int panel = m_panelMatrix->m_outputPanels[output][i];
            int chain = (panelsOnOutput - 1) - m_panelMatrix->m_panels[panel].chain;

            for (int y = 0; y < m_panelHeight; y++) {
                int px = chain * m_panelWidth;
                int yw = y * m_panelWidth * 3;

                dst = (unsigned char*)(m_outputFrame + (((((output * m_panelHeight) + y) * m_formatCodes[m_formatIndex].width) + px) * 3) + m_formatCodes[m_formatIndex].dataOffset);

                for (int x = 0; x < pw3; x += 3) {
                    *(dst++) = m_gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw + x]]];
                    *(dst++) = m_gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw + x + 1]]];
                    *(dst++) = m_gammaCurve[channelData[m_panelMatrix->m_panels[panel].pixelMap[yw + x + 2]]];

                    px++;
                }
            }
        }
    }
}

int LinsnRV9Output::Send(char* buffer, int len) {
#ifndef PLATFORM_OSX
    return sendto(m_fd, buffer, len, 0, (struct sockaddr*)&m_sock_addr, sizeof(struct sockaddr_ll));
#else
    return write(m_fd, buffer, len);
#endif
}

/*
 *
 */
int LinsnRV9Output::SendData(unsigned char* channelData) {
    LogExcess(VB_CHANNELOUT, "LinsnRV9Output::SendData(%p)\n", channelData);

    SetHostMACs(m_buffer);
    memset(m_data, 0, LINSNRV9_DATA_SIZE);

    // Clear the frame number
    m_buffer[14] = 0x00;
    m_buffer[15] = 0x00;

    m_buffer[22] = 0x96;

    m_buffer[26] = 0x85;
    m_buffer[27] = m_formatCodes[m_formatIndex].d27;
    m_buffer[28] = 0xff; // something to do with brightness
    m_buffer[29] = 0xff; // something to do with brightness
    m_buffer[30] = 0xff; // something to do with brightness
    m_buffer[31] = 0xff; // something to do with brightness

    m_buffer[45] = m_formatCodes[m_formatIndex].code;

    if (Send(m_buffer, LINSNRV9_BUFFER_SIZE) < 0) {
        LogErr(VB_CHANNELOUT, "Error sending row data packet: %s\n", strerror(errno));
        return 0;
    }

    int row = 0;
    int frameNumber = 1;
    int bytesSent = 0;
    int framesSent = 0;

    memset(m_header, 0, LINSNRV9_HEADER_SIZE);
    while (frameNumber < m_framePackets) {
        m_buffer[14] = (unsigned char)(frameNumber & 0x00FF);
        m_buffer[15] = (unsigned char)(frameNumber >> 8);

        memcpy(m_data, m_outputFrame + bytesSent, LINSNRV9_DATA_SIZE);

        if (Send(m_buffer, LINSNRV9_BUFFER_SIZE) < 0) {
            LogErr(VB_CHANNELOUT, "Error sending row data packet: %s\n", strerror(errno));
            return 0;
        }

        bytesSent += LINSNRV9_DATA_SIZE;
        frameNumber++;
    }

    return m_channelCount;
}

/*
 *
 */
void LinsnRV9Output::DumpConfig(void) {
    LogDebug(VB_CHANNELOUT, "LinsnRV9Output::DumpConfig()\n");

    LogDebug(VB_CHANNELOUT, "    Interface      : %s\n", m_ifName.c_str());
    LogDebug(VB_CHANNELOUT, "    Width          : %d\n", m_width);
    LogDebug(VB_CHANNELOUT, "    Height         : %d\n", m_height);
    LogDebug(VB_CHANNELOUT, "    m_fd           : %d\n", m_fd);
    LogDebug(VB_CHANNELOUT, "    m_pktSize      : %d\n", m_pktSize);
    LogDebug(VB_CHANNELOUT, "    m_framePackets : %d (0x%02x)\n",
             m_framePackets, m_framePackets);
    LogDebug(VB_CHANNELOUT, "    m_formatIndex  : %d\n", m_formatIndex);
    LogDebug(VB_CHANNELOUT, "    Fmt Code       : 0x%02x\n",
             m_formatCodes[m_formatIndex].code);
    LogDebug(VB_CHANNELOUT, "    Fmt Width      : %d\n",
             m_formatCodes[m_formatIndex].width);
    LogDebug(VB_CHANNELOUT, "    Fmt Height     : %d\n",
             m_formatCodes[m_formatIndex].height);
    LogDebug(VB_CHANNELOUT, "    Fmt Data Offset: %d\n",
             m_formatCodes[m_formatIndex].dataOffset);
    LogDebug(VB_CHANNELOUT, "    Fmt D27        : 0x%02x\n",
             m_formatCodes[m_formatIndex].d27);

    ChannelOutput::DumpConfig();
}

/*
 *
 */
void LinsnRV9Output::HandShake(void) {
    // Send a packet with 0x00, 0x00 bytes

    // Receive packet from receiver with its 2 bytes

    // Loop for 'X' times??  (or is each iteration adding another reciever possibly, need to test)
    // Send a packet with receiver's 2 bytes

    // Receive packet from receiver with its 2 bytes

    // Send a packet with receiver's 2 bytes

    // Receive packet from receiver with its 2 bytes
}

/*
 *
 */
void LinsnRV9Output::GetSrcMAC(void) {
    int s;

#ifndef PLATFORM_OSX
    struct ifreq ifr;
    s = socket(AF_INET, SOCK_DGRAM, 0);
    strcpy(ifr.ifr_name, m_ifName.c_str());
    ioctl(s, SIOCGIFHWADDR, &ifr);
    char* sa_data = ifr.ifr_hwaddr.sa_data;
    close(s);
#else
    char sa_data[24];
    ifaddrs* iflist;
    bool found = false;
    if (getifaddrs(&iflist) == 0) {
        for (ifaddrs* cur = iflist; cur; cur = cur->ifa_next) {
            if ((cur->ifa_addr->sa_family == AF_LINK) &&
                (strcmp(cur->ifa_name, m_ifName.c_str()) == 0) &&
                cur->ifa_addr) {
                sockaddr_dl* sdl = (sockaddr_dl*)cur->ifa_addr;
                memcpy(sa_data, LLADDR(sdl), sdl->sdl_alen);
                found = true;
                break;
            }
        }

        freeifaddrs(iflist);
    }
#endif
    m_srcMAC[0] = sa_data[0];
    m_srcMAC[1] = sa_data[1];
    m_srcMAC[2] = sa_data[2];
    m_srcMAC[3] = sa_data[3];
    m_srcMAC[4] = sa_data[4];
    m_srcMAC[5] = sa_data[5];
}

/*
 *
 */
void LinsnRV9Output::SetHostMACs(void* ptr) {
    struct ether_header* eh = (struct ether_header*)ptr;

    // Set the source MAC address
    eh->ether_shost[0] = m_srcMAC[0];
    eh->ether_shost[1] = m_srcMAC[1];
    eh->ether_shost[2] = m_srcMAC[2];
    eh->ether_shost[3] = m_srcMAC[3];
    eh->ether_shost[4] = m_srcMAC[4];
    eh->ether_shost[5] = m_srcMAC[5];

    // Set the dest MAC address
    eh->ether_dhost[0] = 0x00;
    eh->ether_dhost[1] = 0x00;
    eh->ether_dhost[2] = 0x00;
    eh->ether_dhost[3] = 0x00;
    eh->ether_dhost[4] = 0x00;
    eh->ether_dhost[5] = 0xfe;

    // Linsn also embed's sender MAC in its header
    ((unsigned char*)ptr)[39] = m_srcMAC[0];
    ((unsigned char*)ptr)[40] = m_srcMAC[1];
    ((unsigned char*)ptr)[41] = m_srcMAC[2];
    ((unsigned char*)ptr)[42] = m_srcMAC[3];
    ((unsigned char*)ptr)[43] = m_srcMAC[4];
    ((unsigned char*)ptr)[44] = m_srcMAC[5];
}

/*
 *
 */
void LinsnRV9Output::SetDiscoveryMACs(void* ptr) {
    struct ether_header* eh = (struct ether_header*)ptr;

    // Set the source MAC address
    eh->ether_shost[0] = m_srcMAC[0];
    eh->ether_shost[1] = m_srcMAC[1];
    eh->ether_shost[2] = m_srcMAC[2];
    eh->ether_shost[3] = m_srcMAC[3];
    eh->ether_shost[4] = m_srcMAC[4];
    eh->ether_shost[5] = m_srcMAC[5];

    // Set the dest MAC address
    eh->ether_dhost[0] = 0xff;
    eh->ether_dhost[1] = 0xff;
    eh->ether_dhost[2] = 0xff;
    eh->ether_dhost[3] = 0xff;
    eh->ether_dhost[4] = 0xff;
    eh->ether_dhost[5] = 0xff;

    // Linsn also embed's sender MAC in its header
    ((unsigned char*)ptr)[39] = m_srcMAC[0];
    ((unsigned char*)ptr)[40] = m_srcMAC[1];
    ((unsigned char*)ptr)[41] = m_srcMAC[2];
    ((unsigned char*)ptr)[42] = m_srcMAC[3];
    ((unsigned char*)ptr)[43] = m_srcMAC[4];
    ((unsigned char*)ptr)[44] = m_srcMAC[5];
}
