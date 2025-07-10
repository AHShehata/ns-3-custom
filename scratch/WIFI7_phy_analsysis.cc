/*
 * Copyright (c) 2022
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Sebastien Deronne <sebastien.deronne@gmail.com>
 */

#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/eht-phy.h"
#include "ns3/enum.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/udp-server.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

#include <array>
#include <functional>
#include <numeric>
#include "ns3/attribute-container.h"
#include "ns3/wifi-module.h"

#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/attribute-container.h"

#include "ns3/propagation-loss-model.h"
#include "ns3/wifi-net-device.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-mac.h"
#include "ns3/config-store.h"
#include "ns3/node-list.h"
#include "ns3/pointer.h"



// This is a simple example in order to show how to configure an IEEE 802.11be Wi-Fi network.
//
// It outputs the UDP or TCP goodput for every EHT MCS value, which depends on the MCS value (0 to
// 13), the channel width (20, 40, 80 or 160 MHz) and the guard interval (800ns, 1600ns or 3200ns).
// The PHY bitrate is constant over all the simulation run. The user can also specify the distance
// between the access point and the station: the larger the distance the smaller the goodput.
//
// The simulation assumes a configurable number of stations in an infrastructure network:
//
//  STA     AP
//    *     *
//    |     |
//   n1     n2
//
// Packets in this simulation belong to BestEffort Access Class (AC_BE).
// By selecting an acknowledgment sequence for DL MU PPDUs, it is possible to aggregate a
// Round Robin scheduler to the AP, so that DL MU PPDUs are sent by the AP via DL OFDMA.


using namespace ns3;
NS_LOG_COMPONENT_DEFINE("eht-wifi-networktrial");


// Initialization for tracing

std::map<Mac48Address, uint64_t> packetsReceived; ///< Map that stores the total packets received
                                                  ///< per STA (and addressed to that STA)
std::map<Mac48Address, uint64_t> bytesReceived; ///< Map that stores the total bytes received per STA (and addressed to that STA)

std::ofstream phyTxTraceFile;   ///< File that traces PHY transmissions  over time
std::map<Mac48Address, uint64_t>
    packetsTransmitted; ///< Map that stores (keeps track) of  the total packets transmitted per AP
std::map<Mac48Address, uint64_t>
    rxEventWhileDecodingPreamble; ///< Map that stores the number of reception events per STA that
                                  ///< occurred while PHY was already decoding a preamble
std::map<Mac48Address, uint64_t>
    rxEventWhileTxing; ///< Map that stores the number of reception events per STA that occurred
                       ///< while PHY was already transmitting a PPDU
std::map<Mac48Address, uint64_t>
    rxEventWhileRxing; ///< Map that stores the number of reception events per STA that occurred
                       ///< while PHY was already receiving a PPDU
std::map<Mac48Address, uint64_t>
    rxEventAbortedByTx; ///< Map that stores the number of reception events aborted per STA because
                        ///< the PHY has started to transmit
std::map<Mac48Address, uint64_t>
     phyHeaderFailed; ///< Map that stores the total number of
                            ///< unsuccessfuly received PHY headers per STA


std::map<Mac48Address, Time>
    timeFirstReceived; ///< Map that stores the time at which the first packet was received per STA
                       ///< (and the packet is addressed to that STA)
std::map<Mac48Address, Time>
    timeLastReceived; ///< Map that stores the time at which the last packet was received per STA
                      ///< (and the packet is addressed to that STA)
std::map<Mac48Address, Time> timeFirstTransmitted; ///< Map that stores the time at which the first
                                                   ///< packet was transmitted per STA
std::map<Mac48Address, Time> timeLastTransmitted;  ///< Map that stores the time at which the last
                                                   ///< packet was transmitted per STA


bool tracing = false;    ///< Flag to enable/disable generation of tracing files
uint32_t payloadSize =700; // must fit in the max TX duration when transmitting at MCS 0 over an RU of 26 tones
uint32_t pktSize = 700;


// Functions for tracing
/**
 * Parse context strings of the form "/NodeList/x/DeviceList/x/..." and fetch the Mac address
 *
 * \param context The context to parse.
 * \return the device MAC address
 */
Mac48Address
ContextToMac(std::string context)
{     
    // Remove any thing before the 10th index
    std::string sub = context.substr(10);
    // find the position of this word in the context sring
    uint32_t pos = sub.find("/Device");
    // get only the sting from the beggining till the pos of device and then convert the string to integre using (stoi)
    uint32_t nodeId = std::stoi(sub.substr(0, pos));
    // fetches the node object corresponding to the node id
    Ptr<Node> n = NodeList::GetNode(nodeId);
    Ptr<WifiNetDevice> d;
    for (uint32_t i = 0; i < n->GetNDevices(); i++)
    {
        d = n->GetDevice(i)->GetObject<WifiNetDevice>();
        if (d)
        {
            break;
        }
    }
    return Mac48Address::ConvertFrom(d->GetAddress());
}




/**
 * Parse context strings of the form "/NodeList/x/DeviceList/x/..." to extract the NodeId integer
 *
 * \param context The context to parse.
 * \return the NodeId
 */
uint32_t
ContextToNodeId(std::string context)
{
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/Device");
    return std::stoi(sub.substr(0, pos));
}


// calculate the number of packets transmitted by each device in the node 
/**
 * Incremement the counter for a given address. 
 * 
 *
 * \param [out] counter The counter to increment.
 * \param addr The address to incremement the counter for.
 * \param increment The incremement (1 if omitted).
 */
void
IncrementCounter(std::map<Mac48Address, uint64_t>& counter,
                 Mac48Address addr,
                 uint64_t increment = 1)
{
    // return the poistion of the address in the map if it exist and :counter.end if the mac doesnot exist
    // and it is a reference to the pair
    auto it = counter.find(addr);
    if (it != counter.end())
    {
        it->second += increment;
    }
    else
    {
        counter.insert(std::make_pair(addr, increment));
    }
}




int numTxedPackets_started_data = 0;
int numTxedPackets_started_other = 0;
/**
 * PHY TX trace
 *
 * \param context The context.
 * \param p The packet.
 * \param txPowerW The TX power.
 */
void
PhyTxTrace(std::string context, Ptr<const Packet> p, double txPowerW)
{
    NS_LOG_INFO("PHY-TX-START time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                     << " size=" << p->GetSize() << " " << txPowerW);
    if (tracing)
    {
        phyTxTraceFile << Simulator::Now().GetSeconds() << " " << ContextToNodeId(context)
                       << " size=" << p->GetSize() << " " << txPowerW << std::endl;
    }
    if (p->GetSize() >= payloadSize) // ignore non-data frames
    {
        Mac48Address addr = ContextToMac(context);
        IncrementCounter(packetsTransmitted, addr);
        numTxedPackets_started_data++;
    }
    else{
          numTxedPackets_started_other++;
    }
    std::cout<< "Number of txed data packets started" << numTxedPackets_started_data  << std::endl;
    std::cout<< "Number of txed packets started" << numTxedPackets_started_other  << std::endl;

}




int numTxedPackets_ended = 0;
void PhyTxDoneTrace(std::string context, Ptr<const Packet> pkt){
      std::cout<< context <<std::endl;
      //std::cout<< "\tTrace: packet size = " << pkt->GetSize()
       //        << " time: " << Simulator::Now() << std::endl;
      NS_LOG_INFO("PHY-TX-END time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                   << " " << pkt->GetSize());
      numTxedPackets_ended++;
      std::cout<< "Number of txed packets" << numTxedPackets_ended  << std::endl;
}



int Txed_dropped = 0;
void PhyTxDrop(std::string context, Ptr<const Packet> pkt){
      Txed_dropped++;
      std::cout<< "Number of dropped packets " << Txed_dropped  << std::endl;
}



/**
 * PHY Rx trace.
 *
 * \param context The context.
 * \param p The packet.
 * \param power The Rx power.
 */
void
PhyRxTrace(std::string context, Ptr<const Packet> p, RxPowerWattPerChannelBand power)
{
    NS_LOG_INFO("PHY-RX-START time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                     << " size=" << p->GetSize());
}


int numrxedPackets_ended = 0;
/**
 * PHY Rx trace.
 *
 * \param context The context.
 * \param txVector The TX vector.
 * \param psduDuration The PDSU diration.
 */
void
PhyRxPayloadTrace(std::string context, WifiTxVector txVector, Time psduDuration)
{
    NS_LOG_INFO("PHY-RX-PAYLOAD-START time=" << Simulator::Now()
                                             << " node=" << ContextToNodeId(context)
                                             << " psduDuration=" << psduDuration);
    std::cout<< "Number of rxed packets" << numrxedPackets_ended  << std::endl;

}



/**
 * PHY RX end trace
 *
 * \param context The context.
 * \param p The packet.
 */
void
PhyRxDoneTrace(std::string context, Ptr<const Packet> p)
{
    NS_LOG_INFO("PHY-RX-END time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                   << " size=" << p->GetSize());
}


/**
 * PHY Drop trace.
 *
 * \param context The context.
 * \param p The packet.
 * \param reason The drop reason.
 */
void
PhyRxDropTrace(std::string context, Ptr<const Packet> p, WifiPhyRxfailureReason reason)
{
    NS_LOG_INFO("PHY-RX-DROP time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                    << " size=" << p->GetSize() << " reason=" << reason);
    Mac48Address addr = ContextToMac(context);
    switch (reason)
    {
    case UNSUPPORTED_SETTINGS:
        NS_FATAL_ERROR("RX packet with unsupported settings!");
        break;
    case CHANNEL_SWITCHING:
        NS_FATAL_ERROR("Channel is switching!");
        break;
    case BUSY_DECODING_PREAMBLE: {
        if (p->GetSize() >= payloadSize) // ignore non-data frames
        {
            IncrementCounter(rxEventWhileDecodingPreamble, addr);
        }
        break;
    }
    case RXING: {
        if (p->GetSize() >= payloadSize) // ignore non-data frames
        {
            IncrementCounter(rxEventWhileRxing, addr);
        }
        break;
    }
    case TXING: {
        if (p->GetSize() >= payloadSize) // ignore non-data frames
        {
            IncrementCounter(rxEventWhileTxing, addr);
        }
        break;
    }
    case SLEEPING:
        NS_FATAL_ERROR("Device is sleeping!");
        break;
    case PREAMBLE_DETECT_FAILURE:
        NS_FATAL_ERROR("Preamble should always be detected!");
        break;
    case RECEPTION_ABORTED_BY_TX: {
        if (p->GetSize() >= payloadSize) // ignore non-data frames
        {
            IncrementCounter(rxEventAbortedByTx, addr);
        }
        break;
    }
    case L_SIG_FAILURE: {
        if (p->GetSize() >= payloadSize) // ignore non-data frames
        {
            IncrementCounter(phyHeaderFailed, addr);
        }
        break;
    }
    case HT_SIG_FAILURE:
    case SIG_A_FAILURE:
    case SIG_B_FAILURE:
        NS_FATAL_ERROR("Unexpected PHY header failure!");
    case PREAMBLE_DETECTION_PACKET_SWITCH:
        NS_FATAL_ERROR("All devices should send with same power, so no packet switch during "
                       "preamble detection should occur!");
        break;
    case FRAME_CAPTURE_PACKET_SWITCH:
        NS_FATAL_ERROR("Frame capture should be disabled!");
        break;
    case OBSS_PD_CCA_RESET:
        NS_FATAL_ERROR("Unexpected CCA reset!");
        break;
    case UNKNOWN:
    default:
        NS_FATAL_ERROR("Unknown drop reason!");
        break;
    }
}



/*
//MonitorSnifferRx is a tracesource which could sniff all the received packets 
// so forexample if you trace at the AP and you had multiple stations
// each with different mac address.
// you can creat map includes the time where the first and teh last packet received from
// certain STA.

/**
 * Trace a packet reception.
 *
 * \param context The context.
 * \param p The packet.
 * \param channelFreqMhz The channel frequqncy.
 * \param txVector The TX vector.
 * \param aMpdu The AMPDU.
 * \param signalNoise The signal and noise dBm.
 * \param staId The STA ID.
 */

void
TracePacketReception(std::string context,
                     Ptr<const Packet> p,
                     uint16_t channelFreqMhz,
                     WifiTxVector txVector,
                     MpduInfo aMpdu,
                     SignalNoiseDbm signalNoise,
                     uint16_t staId)
{
    Ptr<Packet> packet = p->Copy();
    if (txVector.IsAggregation())
    {
        // if its aggregated mpdu so remove the header totally from the packet and 
        // then get the total length of the paylaod and take it 
        // check that the aggregated packet is having two macs understand its structure 
        AmpduSubframeHeader subHdr;
        uint32_t extractedLength;
        packet->RemoveHeader(subHdr);
        extractedLength = subHdr.GetLength();
        packet = packet->CreateFragment(0, static_cast<uint32_t>(extractedLength));
    }
    WifiMacHeader hdr;
    packet->PeekHeader(hdr);
    // hdr.GetAddr1() is the receiving MAC address
    if (hdr.GetAddr1() != ContextToMac(context))
    {
        return;
    }
    // hdr.GetAddr2() is the sending MAC address
    if (packet->GetSize() >= pktSize) // ignore non-data frames
    {
        IncrementCounter(packetsReceived, hdr.GetAddr2());
        IncrementCounter(bytesReceived, hdr.GetAddr2(), pktSize);
        auto itTimeFirstReceived = timeFirstReceived.find(hdr.GetAddr2());
        if (itTimeFirstReceived == timeFirstReceived.end())
        {
            timeFirstReceived.insert(std::make_pair(hdr.GetAddr2(), Simulator::Now()));
        }
        auto itTimeLastReceived = timeLastReceived.find(hdr.GetAddr2());
        if (itTimeLastReceived != timeLastReceived.end())
        {
            itTimeLastReceived->second = Simulator::Now();
        }
        else
        {
            timeLastReceived.insert(std::make_pair(hdr.GetAddr2(), Simulator::Now()));
        }
    }
}



/**
 * Reset the stats.
 */
void
RestartCalc()
{
    bytesReceived.clear();
    packetsReceived.clear();
    packetsTransmitted.clear();
    phyHeaderFailed.clear();
    timeFirstReceived.clear();
    timeLastReceived.clear();
    
}


/**
 * Get the Counter associated with a MAC address.
 *
 * \param counter The map of counters to inspect.
 * \param addr The MAC address.
 * \return the value of the counter,
 */
uint64_t
GetCount(const std::map<Mac48Address, uint64_t>& counter, Mac48Address addr)
{
    uint64_t count = 0;
    auto it = counter.find(addr);
    if (it != counter.end())
    {
        count = it->second;
    }
    return count;
}


int
main(int argc, char* argv[])
{

        // Parameters 
        bool udp{true}; // Application used either TCP/UDP
        bool downlink{true}; // Downlink/ UL
        bool useRts{false}; //Use RTS/CTS or not

        
        // EMLSR Parameters 
        //std::string emlsrLinks;  
        //std::set<std::string> emlsrLinks = {"0", "1", "2"};    //Not working sedfined as a set of strings
        std::string emlsrLinks = "0, 1";
        //std::string emlsrLinks = "0, 1, 2";

        // std::set<uint8_t> emlsrLinks = {0, 1, 2};
        uint16_t paddingDelayUsec{32};
        uint16_t transitionDelayUsec{128};
        uint16_t channelSwitchDelayUsec{100};
        bool switchAuxPhy{true};
        bool auxPhyTxCapable{true};
        uint16_t auxPhyChWidth{20};
        
        
        // Simulation parameters
        double simulationTime{1}; // seconds
        double distance{1.0};      // meters
        std::size_t nStations{1};
        double frequency{2.4};       // whether the first link operates in the 2.4, 5 or 6 GHz
        double frequency2{0}; // whether the second link operates in the 2.4, 5 or 6 GHz (0 means no
                            // second link exists)
        double frequency3{0}; // whether the third link operates in the 2.4, 5 or 6 GHz (0 means no third link exists)
        int mcs{-1}; // -1 indicates an unset value
        uint16_t mpduBufferSize{512};
        double minExpectedThroughput{0};
        double maxExpectedThroughput{0};
        Time accessReqInterval{0};
        Time tputInterval{0}; // interval for detailed throughput measurement
        bool verbose{true};

        // OFDMA parameters
        std::string dlAckSeqType{"NO-OFDMA"};
        bool enableUlOfdma{false};
        bool enableBsrp{false};
        int number_packets = 1000;



      

        CommandLine cmd(__FILE__);
        cmd.AddValue(
            "number_packets",
            "Max number of transmitted packets",
            number_packets);
        cmd.AddValue(
            "frequency",
            "Whether the first link operates in the 2.4, 5 or 6 GHz band (other values gets rejected)",
            frequency);
        cmd.AddValue(
            "frequency2",
            "Whether the second link operates in the 2.4, 5 or 6 GHz band (0 means the device has one "
            "link, otherwise the band must be different than first link and third link)",
            frequency2);
        cmd.AddValue(
            "frequency3",
            "Whether the third link operates in the 2.4, 5 or 6 GHz band (0 means the device has up to "
            "two links, otherwise the band must be different than first link and second link)",
            frequency3);
        cmd.AddValue("emlsrLinks",
                    "The comma separated list of IDs of EMLSR links (for MLDs only)",
                    emlsrLinks);
        cmd.AddValue("emlsrPaddingDelay",
                    "The EMLSR padding delay in microseconds (0, 32, 64, 128 or 256)",
                    paddingDelayUsec);
        cmd.AddValue("emlsrTransitionDelay",
                    "The EMLSR transition delay in microseconds (0, 16, 32, 64, 128 or 256)",
                    transitionDelayUsec);
        cmd.AddValue("emlsrAuxSwitch",
                    "Whether Aux PHY should switch channel to operate on the link on which "
                    "the Main PHY was operating before moving to the link of the Aux PHY. ",
                    switchAuxPhy);
        cmd.AddValue("emlsrAuxChWidth",
                    "The maximum channel width (MHz) supported by Aux PHYs.",
                    auxPhyChWidth);
        cmd.AddValue("emlsrAuxTxCapable",
                    "Whether Aux PHYs are capable of transmitting.",
                    auxPhyTxCapable);
        cmd.AddValue("channelSwitchDelay",
                    "The PHY channel switch delay in microseconds",
                    channelSwitchDelayUsec);
        cmd.AddValue("distance",
                    "Distance in meters between the station and the access point",
                    distance);
        cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
        cmd.AddValue("udp", "UDP if set to 1, TCP otherwise", udp);
        cmd.AddValue("downlink",
                    "Generate downlink flows if set to 1, uplink flows otherwise",
                    downlink);
        cmd.AddValue("useRts", "Enable/disable RTS/CTS", useRts);
        cmd.AddValue("mpduBufferSize",
                    "Size (in number of MPDUs) of the BlockAck buffer",
                    mpduBufferSize);
        cmd.AddValue("nStations", "Number of non-AP EHT stations", nStations);
        cmd.AddValue("dlAckType",
                    "Ack sequence type for DL OFDMA (NO-OFDMA, ACK-SU-FORMAT, MU-BAR, AGGR-MU-BAR)",
                    dlAckSeqType);
        cmd.AddValue("enableUlOfdma",
                    "Enable UL OFDMA (useful if DL OFDMA is enabled and TCP is used)",
                    enableUlOfdma);
        cmd.AddValue("enableBsrp",
                    "Enable BSRP (useful if DL and UL OFDMA are enabled and TCP is used)",
                    enableBsrp);
        cmd.AddValue(
            "muSchedAccessReqInterval",
            "Duration of the interval between two requests for channel access made by the MU scheduler",
            accessReqInterval);
        cmd.AddValue("mcs", "if set, limit testing to a specific MCS (0-11)", mcs);
        cmd.AddValue("payloadSize", "The application payload size in bytes", payloadSize);
        cmd.AddValue("tputInterval", "duration of intervals for throughput measurement", tputInterval);
        cmd.AddValue("minExpectedThroughput",
                    "if set, simulation fails if the lowest throughput is below this value",
                    minExpectedThroughput);
        cmd.AddValue("maxExpectedThroughput",
                    "if set, simulation fails if the highest throughput is above this value",
                    maxExpectedThroughput);
        cmd.Parse(argc, argv);

        if (verbose)
        {
            //LogComponentEnable ("WifiMac", LOG_LEVEL_ALL);
            //LogComponentEnable ("StaWifiMac", LOG_LEVEL_ALL);
            //LogComponentEnable ("FrameExchangeManager ", LOG_LEVEL_ALL);
            //LogComponentEnable ("EmlsrManager", LOG_LEVEL_ALL);
            //LogComponentEnable ("DefaultEmlsrManager", LOG_LEVEL_ALL);
            //LogComponentEnable ("WifiHelper", LOG_LEVEL_ALL);
            //WifiHelper::EnableLogComponents(LOG_LEVEL_INFO); // Turn on all Wifi logging



            //LogComponentEnable ("WifiRemoteStationManager", LOG_LEVEL_ALL);


            //LogComponentEnable("SpectrumWifiPhy", LOG_LEVEL_ALL);
            //LogComponentEnable("MultiModelSpectrumChannel", LOG_LEVEL_ALL);


            //LogComponentEnable ("UdpClient", LOG_LEVEL_INFO);
            //LogComponentEnable ("UdpServer", LOG_LEVEL_INFO);
        }
        LogComponentEnable("eht-wifi-networktrial", LOG_LEVEL_INFO);

        if (useRts)
        {
            Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("0"));
            Config::SetDefault("ns3::WifiDefaultProtectionManager::EnableMuRts", BooleanValue(true));
        }

        if (dlAckSeqType == "ACK-SU-FORMAT")
        {
            Config::SetDefault("ns3::WifiDefaultAckManager::DlMuAckSequenceType",
                            EnumValue(WifiAcknowledgment::DL_MU_BAR_BA_SEQUENCE));
        }
        else if (dlAckSeqType == "MU-BAR")
        {
            Config::SetDefault("ns3::WifiDefaultAckManager::DlMuAckSequenceType",
                            EnumValue(WifiAcknowledgment::DL_MU_TF_MU_BAR));
        }
        else if (dlAckSeqType == "AGGR-MU-BAR")
        {
            Config::SetDefault("ns3::WifiDefaultAckManager::DlMuAckSequenceType",
                            EnumValue(WifiAcknowledgment::DL_MU_AGGREGATE_TF));
        }
        else if (dlAckSeqType != "NO-OFDMA")
        {
            NS_ABORT_MSG("Invalid DL ack sequence type (must be NO-OFDMA, ACK-SU-FORMAT, MU-BAR or "
                        "AGGR-MU-BAR)");
        }

    
     int minMcs = 3;
     int maxMcs = 4;

     if (mcs >= 0 && mcs <= 11)
     {
        minMcs = mcs;
        maxMcs = mcs;
     }
     double prevThroughput[3];
     int gi = 1600;
     int channelWidth = 20;


     std::cout << "MCS value"
              << "\t\t"
              << "Channel width"
              << "\t\t"
              << "GI"
              << "\t\t\t"
              << "Throughput" << '\n';


     // Network creation
     for (int mcs = minMcs; mcs < maxMcs; mcs++)
     {
         uint8_t index = 0;         
         if(!udp)
          {
           Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));
          }

          //Nodes 
          NodeContainer wifiStaNodes;
          wifiStaNodes.Create(nStations);
          NodeContainer wifiApNode;
          wifiApNode.Create(1);


          // Netdevices creation
          NetDeviceContainer apDevice;
          NetDeviceContainer staDevices;
          WifiHelper wifi;


          // create the mac object 
         WifiMacHelper mac;



          // create some strings required for the installation of the standard file and now 
          wifi.SetStandard(WIFI_STANDARD_80211be);
          // Instead of creating a string called (channelStr) for each frequency, I will create an array for the 3 channels
          // I will create 2 arrays channelStr and freqRanges each of length 3 correpsonding to the 3 available links 
          // one for channel strings and one for the frequencies
          // std::array<std::string, 3> declares an array that can hold exactly 3 std::string elements.
          // FrequencyRange is astructure in ns3 type (most likely a class or struct) that is used to represent frequency ranges.
          std::array<std::string, 3> channelStr;
          std::array<FrequencyRange, 3> freqRanges;

          // This is the nonHtRefRateMbps non-HT reference rate in MPbs, you are calling the reference rate for a given mcs
          uint64_t nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs) / 1e6;
          uint8_t nLinks = 0;

          // Two strings also have to be created for the data and the control rates
           std::string dataModeStr = "EhtMcs" + std::to_string(mcs);
           std::string ctrlRateStr;


           // Now, assign the characteristics of the three links
            if (frequency2 == frequency || frequency3 == frequency ||
                    (frequency3 != 0 && frequency3 == frequency2))
            {
                   NS_FATAL_ERROR("Frequency values must be unique!");
            }
 
           //The {frequency, frequency2, frequency3} part is an initializer list.
           // It creates a temporary array containing the three variables frequency, frequency2, and frequency3.
           // freq will have the same datatype of frequecny double due to the auto 
           // usually here I am installing both the pathloss model using the config and the setremotestationmanager which configure
           // the transmission rate  based on various factors such as the quality of the wireless link and the error rate, you have to know that 
           // the data (payload) is transmitted with certain rate while the control frames are sent with different rates.
           //"DataMode": Specifies the data transmission rate. For example, "OfdmRate54Mbps" sets the data rate to 54 Mbps
           //"ControlMode": Specifies the control frame transmission rate. For example, "OfdmRate6Mbps" sets the control frame rate to 6 Mbps.

            for (auto freq : {frequency, frequency2, frequency3})
            {   
                //you have finished the links to had if they are less than 3
                if (nLinks > 0 && freq == 0)
                {
                        break;
                }
                // Assign in the channel string array the 1st colomn related to the first link (channel)
                channelStr[nLinks] = "{0, " + std::to_string(channelWidth) + ", ";
                if (freq == 6)
                {
                    channelStr[nLinks] += "BAND_6GHZ, 0}";
                    //check lsa what is the data type of 
                    freqRanges[nLinks] = WIFI_SPECTRUM_6_GHZ;
                    Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",
                                           DoubleValue(48));
                    wifi.SetRemoteStationManager(nLinks,
                                                     "ns3::ConstantRateWifiManager",
                                                     "DataMode",
                                                     StringValue(dataModeStr),
                                                     "ControlMode",
                                                     StringValue(dataModeStr));
                }
                else if (freq == 5)
                {
                    channelStr[nLinks] += "BAND_5GHZ, 0}";
                    freqRanges[nLinks] = WIFI_SPECTRUM_5_GHZ;
                    ctrlRateStr = "OfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
                    wifi.SetRemoteStationManager(nLinks,
                                                     "ns3::ConstantRateWifiManager",
                                                     "DataMode",
                                                     StringValue(dataModeStr),
                                                     "ControlMode",
                                                     StringValue(ctrlRateStr));
                }
                else if (freq == 2.4)
                {
                    channelStr[nLinks] += "BAND_2_4GHZ, 0}";
                    freqRanges[nLinks] = WIFI_SPECTRUM_2_4_GHZ;
                    Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",
                                           DoubleValue(40));
                    ctrlRateStr = "ErpOfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
                    wifi.SetRemoteStationManager(nLinks,
                                                     "ns3::ConstantRateWifiManager",
                                                     "DataMode",
                                                     StringValue(dataModeStr),
                                                     "ControlMode",
                                                     StringValue(ctrlRateStr));
                }
                else
                {
                   NS_FATAL_ERROR("Wrong frequency value!");
                }
                nLinks++;
            }

                    
                    // check that we are having more than one link and at the same time, we inserted emlsrLinks as a string parameter 
                    // .empty is a method as a part of the standard libraray in c++ which check its containers either string, vector or 
                    // list is empty or not
            //if (nLinks > 1 && !emlsrLinks.empty())
            if (nLinks > 1)
            {
                std::cout << "The EMLSRoptionisactivated "<< emlsrLinks.empty() << std::endl;
                wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
            }
            Ssid ssid = Ssid("ns3-80211be");


            // now for the PHY, MAC and channel objects (For PHY and channel), YANS cannot be used here beacuse we are using more than on link 
             // SO it is the spectrum channel which have to be used and there are two types the single and multiple, they mentioned
             // that the single one cannot be used so I will  use directly the multispectrum
                


             // Phyical layer object creation for each link so here you give it eithre link 1 / 2 /3
             SpectrumWifiPhyHelper phy(nLinks);
             phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
             phy.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));
             /* Set the PHY layer error model (switch to NistErrorRateModel / Yans for comparison and evaluation 
             if needed) */
             //spectrumWifiPhy.SetErrorRateModel ("ns3::TableBasedErrorRateModel");




             // Don't forget that the station is the one which have single radio so it is the one that its mac need to be characterized
             // by this options Helper function used to set the EMLSR Manager that can be installed on an EHT non-AP MLD.
            mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
            mac.SetEmlsrManager("ns3::DefaultEmlsrManager",
                                "EmlsrLinkSet",
                                 //AttributeContainerValue<UintegerValue>(emlsrLinks),
                                 StringValue(emlsrLinks),
                                 "EmlsrPaddingDelay",
                                 TimeValue(MicroSeconds(paddingDelayUsec)),
                                 "EmlsrTransitionDelay",
                                 TimeValue(MicroSeconds(transitionDelayUsec)),
                                 "SwitchAuxPhy",
                                 BooleanValue(switchAuxPhy),
                                 "AuxPhyTxCapable",
                                 BooleanValue(auxPhyTxCapable),
                                 "AuxPhyChannelWidth",
                                 UintegerValue(auxPhyChWidth));
            for (uint8_t linkId = 0; linkId < nLinks; linkId++)
            {
                phy.Set(linkId, "ChannelSettings", StringValue(channelStr[linkId]));
                
                auto spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
                auto lossModel = CreateObject<LogDistancePropagationLossModel>();
                spectrumChannel->AddPropagationLossModel(lossModel);
                phy.AddChannel(spectrumChannel, freqRanges[linkId]);
            }
            staDevices = wifi.Install(phy, mac, wifiStaNodes);
            





            // Now, lets configure the mac of the AP MLD, there are 3 phys and 3 links and thos are connecteed to both
            // the AP and the non-AP, so only mac will changes 
            // This for the case of OFDMA
            if (dlAckSeqType != "NO-OFDMA")
            {
               mac.SetMultiUserScheduler("ns3::RrMultiUserScheduler",
                                         "EnableUlOfdma",
                                         BooleanValue(enableUlOfdma),
                                         "EnableBsrp",
                                         BooleanValue(enableBsrp),
                                         "AccessReqInterval",
                                         TimeValue(accessReqInterval));
            }

            mac.SetType("ns3::ApWifiMac",
                            "EnableBeaconJitter",
                            BooleanValue(false),
                            "Ssid",
                            SsidValue(ssid));
            apDevice = wifi.Install(phy, mac, wifiApNode);
            

            // Retrieve the parameters of the phy layer 
            Ptr<WifiNetDevice> APdeviceaccessed = DynamicCast<WifiNetDevice>(apDevice.Get(0));
            Ptr<WifiPhy> AP_phy = DynamicCast<WifiPhy>(APdeviceaccessed->GetPhy());
            double CCA_threshold  =  AP_phy->GetCcaEdThreshold(); 
            double CCA_sensitivity  =  AP_phy->GetCcaSensitivityThreshold ();
            double  Num_antennas  =  AP_phy->GetNumberOfAntennas ();
            double  PhyId  =  AP_phy->GetPhyId ();
            Time slot_t =  AP_phy->GetSlot ();
            double Rx_gain = AP_phy->GetRxGain ();
            double Tx_gain = AP_phy->GetTxGain ();
            double channel_width = AP_phy->GetChannelWidth();
            double TxPowerEnd = AP_phy->GetTxPowerEnd ();
            double TxPowerStart = AP_phy->GetTxPowerStart ();
            std::cout << "CCA_threshold " << CCA_threshold << std::endl;
            std::cout << "CCA_sensitivity " << CCA_sensitivity << std::endl;
            std::cout << "Num_antennas " << Num_antennas << std::endl;
            std::cout << "PhyId " << PhyId << std::endl;
            std::cout << "slot_t " << slot_t << std::endl;
            std::cout << "Rx_gain in dB" << Rx_gain << std::endl;
            std::cout << "Tx_gain in dB" << Tx_gain << std::endl;
            std::cout << "channel_width " << channel_width << std::endl;
            std::cout << "TxPowerEnd " << TxPowerEnd << std::endl;
            std::cout << "TxPowerStart " << TxPowerStart << std::endl;
            
            UintegerValue  Txstreams;
            AP_phy->GetAttribute("MaxSupportedTxSpatialStreams", Txstreams);
            UintegerValue  Rxstreams;
            AP_phy->GetAttribute("MaxSupportedTxSpatialStreams", Rxstreams);
            std::cout << "Txstreams " << Txstreams.Get() << std::endl;
            std::cout << "Rxstreams " << Rxstreams.Get() << std::endl;

           
            PointerValue ptr;
            AP_phy->GetAttribute("ErrorRateModel", ptr);
            Ptr<ErrorRateModel> txQueue = ptr.Get<ErrorRateModel>();


   


            int64_t streamNumber = 100;
            streamNumber += wifi.AssignStreams(apDevice, streamNumber);
            streamNumber += wifi.AssignStreams(staDevices, streamNumber);


             // Set guard interval and MPDU buffer size
             // Again, Config::Set is used to set the value of a configuration attribute across all nodes and devices that match the provided path.
             //In summary, use Config::SetDefault to define default values for objects before they are created,
             // and use Config::Set to modify the configuration of objects that already exist in the simulation.
            Config::Set(
                   "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
                    TimeValue(NanoSeconds(gi)));
            Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize",
                            UintegerValue(mpduBufferSize));



             // Tracing 
            phy.EnablePcap("wifi_7_phy_AP", apDevice.Get(0));
            phy.EnablePcap("wifi_7_phy_STA", staDevices.Get(0));
            //phy.EnablePcapAll("second");
          

             // mobility.
            MobilityHelper mobility;
            Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

            positionAlloc->Add(Vector(0.0, 0.0, 0.0));
            positionAlloc->Add(Vector(distance, 0.0, 0.0));
            mobility.SetPositionAllocator(positionAlloc);

            mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

            mobility.Install(wifiApNode);
            mobility.Install(wifiStaNodes);

            /* Internet stack*/
            InternetStackHelper stack;
            stack.Install(wifiApNode);
            stack.Install(wifiStaNodes);
            streamNumber += stack.AssignStreams(wifiApNode, streamNumber);
            streamNumber += stack.AssignStreams(wifiStaNodes, streamNumber);

            Ipv4AddressHelper address;
            address.SetBase("192.168.1.0", "255.255.255.0");
            Ipv4InterfaceContainer staNodeInterfaces;
            Ipv4InterfaceContainer apNodeInterface;

            staNodeInterfaces = address.Assign(staDevices);
            apNodeInterface = address.Assign(apDevice);


            /* Setting applications */
            ApplicationContainer serverApp;
            auto serverNodes = downlink ? std::ref(wifiStaNodes) : std::ref(wifiApNode);
            Ipv4InterfaceContainer serverInterfaces;
            NodeContainer clientNodes;
            for (std::size_t i = 0; i < nStations; i++)
            {
                serverInterfaces.Add(downlink ? staNodeInterfaces.Get(i) : apNodeInterface.Get(0));
                clientNodes.Add(downlink ? wifiApNode.Get(0) : wifiStaNodes.Get(i));
            }

            const auto maxLoad =  nLinks * EhtPhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
            std::cout << "maxLoad " << maxLoad/1000000 << std::endl;

            if (udp)
            {
                    // UDP flow
                    uint16_t port = 9;
                    UdpServerHelper server(port);
                    serverApp = server.Install(serverNodes.get());
                    streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

                    serverApp.Start(Seconds(0.0));
                    serverApp.Stop(Seconds(simulationTime + 1));
                    const auto packetInterval = payloadSize * 8.0 / maxLoad;

                    for (std::size_t i = 0; i < nStations; i++)
                    {
                        UdpClientHelper client(serverInterfaces.GetAddress(i), port);
                        client.SetAttribute("MaxPackets", UintegerValue(200));
                        client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
                        client.SetAttribute("PacketSize", UintegerValue(payloadSize));
                        ApplicationContainer clientApp = client.Install(clientNodes.Get(i));
                        streamNumber += client.AssignStreams(clientNodes.Get(i), streamNumber);

                        clientApp.Start(Seconds(1.0));
                        clientApp.Stop(Seconds(simulationTime + 1));
                    }
            }
            else
            {
                    // TCP flow
                    uint16_t port = 50000;
                    Address localAddress(InetSocketAddress(Ipv4Address::GetAny(), port));
                    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", localAddress);
                    serverApp = packetSinkHelper.Install(serverNodes.get());
                    streamNumber += packetSinkHelper.AssignStreams(serverNodes.get(), streamNumber);

                    serverApp.Start(Seconds(0.0));
                    serverApp.Stop(Seconds(simulationTime + 1));

                    for (std::size_t i = 0; i < nStations; i++)
                    {
                        OnOffHelper onoff("ns3::TcpSocketFactory", Ipv4Address::GetAny());
                        onoff.SetAttribute("OnTime",
                                           StringValue("ns3::ConstantRandomVariable[Constant=1]"));
                        onoff.SetAttribute("OffTime",
                                           StringValue("ns3::ConstantRandomVariable[Constant=0]"));
                        onoff.SetAttribute("PacketSize", UintegerValue(payloadSize));
                        onoff.SetAttribute("DataRate", DataRateValue(maxLoad));
                        AddressValue remoteAddress(
                            InetSocketAddress(serverInterfaces.GetAddress(i), port));
                        onoff.SetAttribute("Remote", remoteAddress);
                        ApplicationContainer clientApp = onoff.Install(clientNodes.Get(i));
                        streamNumber += onoff.AssignStreams(clientNodes.Get(i), streamNumber);

                        clientApp.Start(Seconds(1.0));
                        clientApp.Stop(Seconds(simulationTime + 1));
                    }
            }

             // Install FlowMonitor 
             FlowMonitorHelper flowmon;
             Ptr<FlowMonitor> monitor = flowmon.InstallAll ();


            // Now for calculating the throughput. The client, AP is the one tranmitting
            // Usually in one link, you just need to schedule the time of distributing the 
            // routing tables. which will be as following but unfortunately here you cannot use that with more
            // than one link connected to the same netdevice 
            //Simulator::Schedule(Seconds(0), Ipv4GlobalRoutingHelper::PopulateRoutingTables);
            //Simulator::Schedule(Seconds(0)); // It needs another pointer to the event scheduled
            // When I use it without routing tables nothing changes

            // Save the current configuration to an XML file
            Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.txt"));
            Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText"));
            Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
            ConfigStore outputConfig2;
            outputConfig2.ConfigureDefaults();
            outputConfig2.ConfigureAttributes();





        // Trace PHY Tx start events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxBegin",
                    MakeCallback(&PhyTxTrace));
        // Trace PHY Tx end events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxEnd",
                    MakeCallback(&PhyTxDoneTrace));
        // Trace PHY Tx drops events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxDrop",
                    MakeCallback(&PhyTxDrop));


        // Trace PHY Rx start events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxBegin",
                    MakeCallback(&PhyRxTrace));
        // Trace PHY Rx payload start events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxPayloadBegin",
                    MakeCallback(&PhyRxPayloadTrace));
        // Trace PHY Rx drop events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxDrop",
                    MakeCallback(&PhyRxDropTrace));
        // Trace PHY Rx end events
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxEnd",
                    MakeCallback(&PhyRxDoneTrace));

         // Log packet receptions
            Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/MonitorSnifferRx",
                    MakeCallback(&TracePacketReception));


            Simulator::Stop(Seconds(simulationTime + 1));
            Simulator::Run();


            // When multiple stations are used, there are chances that association requests
            uint64_t rxBytes = 0;
            uint64_t lostpackets = 0;
            uint64_t packetsSentTXPcheck = 0;


            if (udp)
            {
                for (uint32_t i = 0; i < serverApp.GetN(); i++)
                    {
                        rxBytes +=  payloadSize * DynamicCast<UdpServer>(serverApp.Get(i))->GetReceived();
                        lostpackets +=   DynamicCast<UdpServer>(serverApp.Get(i))->GetLost();

                    }
            }
            else
            {
                for (uint32_t i = 0; i < serverApp.GetN(); i++)
                {
                    rxBytes += DynamicCast<PacketSink>(serverApp.Get(i))->GetTotalRx(); 
                    // packetsSentTXPcheck += DynamicCast<OnOffApplication>(clientApp.Get(i))->GetSent();
                }
                lostpackets = packetsSentTXPcheck - rxBytes; 

            }
            double throughput = (rxBytes * 8) / (simulationTime * 1000000.0); // Mbit/s
            std::cout <<"Received packets " <<  DynamicCast<UdpServer>(serverApp.Get(0))->GetReceived() << std::endl;
            std::cout <<"rxBytes"  << rxBytes << std::endl;

            Simulator::Destroy();
            std::cout << mcs << "\t\t\t" << channelWidth << " MHz\t\t\t" << gi << " ns\t\t\t"  << throughput << " Mbit/s" << std::endl;
            prevThroughput[index] = throughput;
            index++;
            
            
            //This line initializes a vector named cumulRxBytes with nStations elements, each initialized to 0.
            // This line shedules another event at 1 second + thput inetrval which is when the server receives
            // at that time we give him a pointer to the function to be executed at that time
            // Then all the attributes given to this function which are .. 
            //std::vector<uint64_t> cumulRxBytes(nStations, 0);
            //if (tputInterval.IsStrictlyPositive())
            //{
            //    Simulator::Schedule(Seconds(1) + tputInterval,
            //                           &PrintIntermediateTput,
            //                           cumulRxBytes,
            //                            udp,
            //                            serverApp,
            //                            payloadSize,
            //                            tputInterval,
            //                            simulationTime + 1);
            //}


                

             // I have to check that the three phys and links are tranmitting on not only one and you have to
             // make sure here that it is still one ata time but definetely it have to reduce the latency wala eh
             // check the exesting results for comparison between SL and EMLSR, also check 


             // Print per flow statistics
        
              monitor->CheckForLostPackets ();
             Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
             std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
             
              int aya = 1;
             for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
             {   
                 std::cout << "  Aya:   "<< aya << "\n";
                 Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
                 std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
                 std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
                 std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
                 std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024  << " Mbps\n";
                 
                 std::cout << "Time first packet received " << i->second.timeFirstTxPacket.GetSeconds () << "\n";
                 std::cout << "Time last packet received " << i->second.timeLastRxPacket.GetSeconds () << "\n";
                 std::cout << "Time taken " << (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ())  << "\n";
                 
                 std::cout << "  Delay:      " << (i->second.delaySum.GetSeconds())/(i->second.rxPackets)<< " s\n";
                 std::cout << "  txed packets: " << (i->second.txPackets)<< "\n";
                 std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
                 std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";

                 aya +=aya;
              }


        
            

        }
        std::cout << "throughput Array: ";
        for (size_t i = 0; i < 3; i++) 
        {
            std::cout << prevThroughput[i] << " ";
        }
        std::cout << std::endl;   
        return 0;
}   