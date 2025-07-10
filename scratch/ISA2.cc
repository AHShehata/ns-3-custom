/*
 * Copyright (c) 2016 SEBASTIEN DERONNE
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
#include "ns3/enum.h"
#include "ns3/he-phy.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
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
#include "ns3/onoff-application.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/core-module.h"


#include "ns3/wifi-net-device.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/queue.h"
#include "ns3/propagation-loss-model.h"

#include <iomanip>
#include "ns3/string.h"
#include "ns3/packet-socket-client.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/packet-socket-server.h"
#include "ns3/socket.h"
#include "ns3/node-list.h"
#include "ns3/ap-wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-mac.h"
#include "ns3/ampdu-subframe-header.h"
#include "ns3/qos-txop.h"
#include "ns3/eht-phy.h"


#include <functional>

// This is a simple example in order to show how to configure an IEEE 802.11ax Wi-Fi network.
//
// It outputs the UDP or TCP goodput for every HE MCS value, which depends on the MCS value (0 to
// 11), the channel width (20, 40, 80 or 160 MHz) and the guard interval (800ns, 1600ns or 3200ns).
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


// An infrastructure network in Wi-Fi refers to a wireless network configuration where devices (clients or stations) communicate with each other via an access point (AP). 
// This is the most common mode of operation for Wi-Fi networks, such as those found in homes, offices, and public spaces.

// In ns-3, an infrastructure Wi-Fi network can be simulated by setting up access points and stations. 
using namespace ns3;
NS_LOG_COMPONENT_DEFINE("he-wifi-network");


// Global variables for use in callbacks.
double g_signalDbmAvg; //!< Average signal power [dBm]
double g_noiseDbmAvg;  //!< Average noise power [dBm]
uint32_t g_samples;    //!< Number of samples
bool tracing = true;    ///< Flag to enable/disable generation of tracing files
uint32_t payloadSize =700; // must fit in the max TX duration when transmitting at MCS 0 over an RU of 26 tones


// Initialized maps for tracing
std::ofstream phyTxTraceFile;   ///< File that traces PHY transmissions  over time
std::ofstream macTxTraceFile;   ///< File that traces MAC transmissions  over time
std::ofstream macRxTraceFile;   ///< File that traces MAC receptions  over time
std::ofstream DropMacTraceFile; ///< File that traces the number of packets dropped before queueing.
std::ofstream DropMpduTraceFile; ///< File that traces the number of mpdus dropped before queueing.
std::ofstream EnqueueTraceFile;   ///< File that traces the enqueueing time of packets.
std::ofstream DequeueTraceFile;   ///< File that traces the dequeueing time of packets.
std::ofstream NumberofqueuedpacketsFile;  ///< File that traces the number of packets enqueued.
std::ofstream DropEnqueueTraceFile;   ///< File that traces drop before enqueueing in the mac queue
std::ofstream DropDequeueTraceFile;   ///< File that traces drop after dequeueing in the mac queue
std::ofstream cwTraceFile;      ///< File that traces CW over time
std::ofstream backoffTraceFile; ///< File that traces backoff over time

std::ofstream LatecnyCdfFile;


std::map<Mac48Address, uint64_t> packetsTransmitted; ///< Map that stores (keeps track) of  the total packets transmitted per AP
std::map<Mac48Address, uint64_t> packetsReceived; ///< Map that stores the total packets received
                                                  ///< per STA (and addressed to that STA)
std::map<Mac48Address, uint64_t> bytesReceived; ///< Map that stores the total bytes received per STA (and addressed to that STA)
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
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/Device");
    uint32_t nodeId = std::stoi(sub.substr(0, pos));
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


Ipv4Address
ContextToIp(std::string context)
{
    // Retrieve the node and its IP address
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/Device");
    uint32_t nodeId = std::stoi(sub.substr(0, pos));

    // Retrieve the node and its IP address
    Ptr<Node> node = NodeList::GetNode(nodeId);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    Ipv4Address serverIpAddress = ipv4->GetAddress(1, 0).GetLocal();  // Adjust interface index as needed
    return serverIpAddress;
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

void
SendPacket(Ptr<NetDevice> sourceDevice, Address& destination)
{
    Ptr<Packet> pkt = Create<Packet>(payloadSize); // dummy bytes of data
    sourceDevice->Send(pkt, destination, 0);
}



int numTxedPackets_data = 0;
int numTxedPackets_other = 0;
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
    std::cout << "phytx" << std::endl;
    NS_LOG_INFO("PHY-TX-START time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                     << " size=" << p->GetSize() << " " << txPowerW);
    if (tracing)
    {
        phyTxTraceFile << "Packet transmitted "<< Simulator::Now().GetSeconds() << " " << ContextToNodeId(context)
                       << " size=" << p->GetSize() << " " << txPowerW << std::endl;
    }
    if (p->GetSize() >= payloadSize) // ignore non-data frames
    {
        Mac48Address addr = ContextToMac(context);
        IncrementCounter(packetsTransmitted, addr);
        numTxedPackets_data++;
    }
    else{
          numTxedPackets_other++;
    }
    //std::cout<< "Number of txed data packets started" << numTxedPackets_data  << std::endl;
    //std::cout<< "Number of txed packets started" << numTxedPackets_other  << std::endl;
}




int numTxedPackets_ended = 0;
void PhyTxDoneTrace(std::string context, Ptr<const Packet> pkt)
{
     if (tracing)
     {
         phyTxTraceFile << "PHY-TX-END time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                   << " " << pkt->GetSize() << std::endl;
     }
      numTxedPackets_ended++;
      //std::cout<< "Number of txed packets" << numTxedPackets_ended  << std::endl;
}



int Txed_dropped = 0;
void PhyTxDrop(std::string context, Ptr<const Packet> pkt){
      Txed_dropped++;
      //std::cout<< "Number of dropped packets during transmission" << Txed_dropped  << std::endl;
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
     if (tracing)
     {
         phyTxTraceFile << "PHY-RX-START time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                     << " size=" << p->GetSize() << std::endl;
     }
                                     
}


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
    if (tracing)
    {
        phyTxTraceFile << "PHY-RX-PAYLOAD-START time=" << Simulator::Now()
                                             << " node=" << ContextToNodeId(context)
                                             << " psduDuration=" << psduDuration << std::endl;

    }
}


int numPackets_rxed = 0;
int numPackets_rxed_ctr = 0;

/**
 * PHY RX end trace
 *
 * \param context The context.
 * \param p The packet.
 */
void
PhyRxDoneTrace(std::string context, Ptr<const Packet> p)
{
    if (tracing)
    {
        phyTxTraceFile << "PHY-RX-END time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                   << " size=" << p->GetSize() << std::endl;
    }
    if (p->GetSize() >= payloadSize) // ignore non-data frames
    {
        numPackets_rxed++;
    }
    else
    {
          numPackets_rxed_ctr++;
    }
     //std::cout<< "Number of rxed packets" << numrxedPackets_rxed  << std::endl;
}

int numrxedPackets_dropped_phy = 0;
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
    numrxedPackets_dropped_phy ++;
}




//MonitorSnifferRx is a tracesource which could sniff all the received packets 
// so forexample if you trace at the AP and you had multiple stations
// each with different mac address.
// you can creat map includes the time where the first and the last packet received from
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
    if (packet->GetSize() >= payloadSize) // ignore non-data frames
    {
        IncrementCounter(packetsReceived, hdr.GetAddr2());
        IncrementCounter(bytesReceived, hdr.GetAddr2(), payloadSize);
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
    g_samples++;
    g_signalDbmAvg += ((signalNoise.signal - g_signalDbmAvg) / g_samples);
    g_noiseDbmAvg += ((signalNoise.noise - g_noiseDbmAvg) / g_samples);
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
    rxEventWhileDecodingPreamble.clear();
    rxEventWhileTxing.clear();
    rxEventWhileRxing.clear();
    rxEventAbortedByTx.clear();
    timeFirstTransmitted.clear();
    timeLastTransmitted.clear();
}


/*MAC tracing*/

/**
 * MAC TX trace.
 *
 * \param context The context.
 * \param p The packet.
 */
int Number_packets_txed_mac = 0;
void
MacTxTrace(std::string context, Ptr<const Packet> p)
{
    std::cout << "mac Tx" << std::endl;
    Ptr<Packet> packet = p->Copy();
    if (tracing)
    {
        macTxTraceFile << "Packet transmitted in MAC " <<Simulator::Now().GetSeconds()
                        << " " << ContextToNodeId(context) << " "
                        << p->GetSize() << std::endl;
      
    }
    Number_packets_txed_mac++;

}

/**
 * MAC RX trace.
 *
 * \param context The context.
 * \param p The packet.
 */
int Number_packets_rxed_mac = 0;
void
MacRxTrace(std::string context, Ptr<const Packet> p)
{
    if (tracing)
    {
        macRxTraceFile <<  "Packet received in MAC " << Simulator::Now().GetSeconds() << " " << ContextToNodeId(context) << " "
                       << p->GetSize() << std::endl;
    }
    Number_packets_rxed_mac++;
}

/**
 *The trace source fired when packets coming into the "top" of the device
     * are dropped at the MAC layer before being queued for transmission.

 *
 * \param context The context.
 * \param p The packet.
 */
int Number_dropped_packets_at_mac = 0;
void DropMacTrace(std::string context, Ptr<const Packet> p)
{
    if (tracing)
    {
        DropMacTraceFile <<
           "At time: "  << Simulator::Now() << " Packet of size " << p->GetSize() << " dropped at MAC!" << std::endl;         
    }
     Number_dropped_packets_at_mac ++;
}


/**
 * mpdu tx Drop trace.
 *
 * \param context The context.
 * \param mpdu The dropped mpdu.
 * \param reason The drop reason.
 */
int Dropped_mpdus = 0;
void
mpduDropTrace(std::string context, WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu)
{
    if (tracing)
    { 
       DropMpduTraceFile << "mpdu-TX-DROP time=" << Simulator::Now() << " node=" << ContextToNodeId(context)
                                    << " size=" << mpdu->GetSize() << " reason=" << reason << std::endl;
    }
    switch (reason)
    {
    case WIFI_MAC_DROP_FAILED_ENQUEUE  :
        NS_FATAL_ERROR(" The packet could not be enqueued into the MAC layer's transmission queue!");
        break;
    case WIFI_MAC_DROP_EXPIRED_LIFETIME :
        NS_FATAL_ERROR("The packet’s lifetime has expired before it could be successfully transmitted!");
        break;
    case WIFI_MAC_DROP_REACHED_RETRY_LIMIT :
        NS_FATAL_ERROR("The maximum number of allowed times  to transmit the packet without success is reached!");
        break;
    case WIFI_MAC_DROP_QOS_OLD_PACKET :
        NS_FATAL_ERROR(" packet with QoS classification is dropped because it is considered old!");
        break;
    }
    Dropped_mpdus ++;
}


void TracepacketsinQueue(uint32_t oldValue, uint32_t newValue)
{
    if (tracing)
    {
        NumberofqueuedpacketsFile << "At " << Simulator::Now()  << " packets in the queue " << oldValue << " to " << newValue << std::endl;
    }
}


// you can use this time in calculating the queuing latency of the packets if you take its difference
void EnqueuTrace(Ptr<const WifiMpdu> mpdu)
{        
    std::cout << "packet is enquueud" << std::endl;
     const WifiMacHeader* hdr  = &mpdu->GetOriginal()->GetHeader();
    Mac48Address dstMac = hdr->GetAddr1();
    Mac48Address srcMac = hdr->GetAddr2();
   
     if (dstMac == Mac48Address(" 00:00:00:00:00:01")) // Assuming Station 1 IP
     {
        std::cout << ("Packet to Station 1") << std::endl;
     }
     else if (dstMac == Mac48Address(" 00:00:00:00:00:02")) // Assuming Station 2 IP
     {
         std::cout << ("Packet to Station 2") << std::endl;
     }
     else
     {
        std::cout << "A3mel eh" << std::endl;
     }


    
    if (tracing)
    {
       EnqueueTraceFile<< "At time: "  << Simulator::Now() << " Packet of size " << mpdu->GetSize() << " enqueued!" <<std::endl;
    }
}


void DequeueTrace(Ptr<const WifiMpdu> mpdu)
{
    std::cout << "packet is dequeued" << std::endl;
    if (tracing)
    {
       DequeueTraceFile<< "At time: "  << Simulator::Now() << " Packet of size " << mpdu->GetSize() << " enqueued!" <<std::endl;
    }
}

int Number_dropped_packets_mac_after_dequeue = 0;
void DropDequeuTrace(Ptr<const WifiMpdu> mpdu)
{
    if (tracing)
    {
        DropDequeueTraceFile<<
           "At time: "  << Simulator::Now() << " Packet of size " << mpdu->GetSize() << " dropped!" << std::endl;
         
    }
     Number_dropped_packets_mac_after_dequeue ++;    
}


int Number_dropped_packets_mac_before_Enqueue = 0;
void DropEnqueueTrace(Ptr<const WifiMpdu> mpdu)
{
    if (tracing)
    {
        DropEnqueueTraceFile <<
            "At time: "  << Simulator::Now() << " Packet of size " << mpdu->GetSize() << " dropped!" << std::endl;
            
    }
    Number_dropped_packets_mac_before_Enqueue ++;    
}


/**
 * Contention window trace.
 *
 * \param context The context.
 * \param cw The contention window.
 */
void
CwTrace(std::string context, uint32_t cw, uint8_t linkId)
{
    if (tracing)
    {
        cwTraceFile << Simulator::Now().GetSeconds() << " " << ContextToNodeId(context) << " " << cw
                    << std::endl;
    }
}


/**
 * Backoff trace.
 *
 * \param context The context.
 * \param newVal The backoff value.
 */
void BackoffTrace(std::string context, uint32_t newVal, uint8_t linkId)
{
   //std::cout << "backoff is changing" << std::endl;
    if (tracing)
    {
        if (ContextToNodeId(context) == 2)
        {
           backoffTraceFile << Simulator::Now().GetSeconds() << " " << ContextToNodeId(context) << " "
                            << newVal << std::endl;
        }
    }
}

int udp_packets = 0;
void
ClientTx(std::string context, Ptr<const Packet> p)
{
     udp_packets++;
}

int Tx_udp_packets = 0;
std::map<Ipv4Address, std::map<uint64_t, Time>> sendTimestamps;  // Address -> (PacketID -> Send Time)
void
ClientTxAdd(std::string context, Ptr<const Packet> p, const Address &srcAddress, const Address &destAddress)
{   
    Ipv4Address srcAddress_corr = ContextToIp(context);
    //std::cout << InetSocketAddress::ConvertFrom(srcAddress_corr).GetIpv4() << std::endl;
    //std::cout << InetSocketAddress::ConvertFrom(destAddress).GetIpv4() << std::endl;
    Ipv4Address destAddressIp = InetSocketAddress::ConvertFrom(destAddress).GetIpv4();
    uint64_t packetId = p->GetUid();
    Time sendTime = Simulator::Now();
    sendTimestamps[destAddressIp][packetId] = sendTime;
    Tx_udp_packets++;
}

int Rx_udp_packets = 0;
std::map<Ipv4Address, std::map<uint64_t, Time>> receiveTimestamps;  // Address -> (PacketID -> Receive Time)
void
ServerRxAdd(std::string context, Ptr<const Packet> p, const Address &srcAddress, const Address &destAddress)
{   
    //std::cout << InetSocketAddress::ConvertFrom(srcAddress).GetIpv4() << std::endl;
    Ipv4Address destAddress_corr = ContextToIp(context);
    //std::cout << destAddress_corr << std::endl;
    uint64_t packetId = p->GetUid();
    Time receiveTime  = Simulator::Now();
    receiveTimestamps[destAddress_corr][packetId] = receiveTime;
    Rx_udp_packets++;
}




// Function to print send time stamps
void PrintSendTimestamps(const std::map<Ipv4Address, std::map<uint64_t, Time>>& sendTimestamps) {
    // Iterate over each address in the outer map
    for (const auto& addressEntry : sendTimestamps) {
        Ipv4Address address = addressEntry.first;
        const auto& packetsMap = addressEntry.second;

        // Print the address (using InetSocketAddress to convert from Address to IPv4 address)
        std::cout << "Address: " << address << std::endl;

        // Print each packet ID and its send time within this address
        for (const auto& packetEntry : packetsMap) {
            uint64_t packetId = packetEntry.first;
            Time sendTime = packetEntry.second;

            std::cout << "  Packet ID: " << packetId << ", Send Time: " << sendTime.GetSeconds() << " seconds" << std::endl;
        }

        // Print the count of packets associated with the address
        std::cout << "  Total packets for this address: " << packetsMap.size() << std::endl;
    }

    // Print the total number of unique addresses in sendTimestamps
    std::cout << "Total unique addresses: " << sendTimestamps.size() << std::endl;
}

// Function to print receive time stamps
void PrintReceiveTimestamps(const std::map<Ipv4Address, std::map<uint64_t, Time>>& receiveTimestamps) {
    // Iterate over each address in the outer map
    for (const auto& addressEntry : receiveTimestamps) {
        Ipv4Address address = addressEntry.first;
        const auto& packetsMap = addressEntry.second;

        // Print the address (using InetSocketAddress to convert from Address to IPv4 address)
        std::cout << "Address: " << address << std::endl;

        // Print each packet ID and its send time within this address
        for (const auto& packetEntry : packetsMap) {
            uint64_t packetId = packetEntry.first;
            Time sendTime = packetEntry.second;

            std::cout << "  Packet ID: " << packetId << ", Send Time: " << sendTime.GetSeconds() << " seconds" << std::endl;
        }

        // Print the count of packets associated with the address
        std::cout << "  Total packets for this address: " << packetsMap.size() << std::endl;
    }

    // Print the total number of unique addresses in sendTimestamps
    std::cout << "Total unique addresses: " << sendTimestamps.size() << std::endl;
}

std::map<Ipv4Address, std::vector<double>> packetLatencies; // Address -> [List of Latencies in ms]
std::map<Ipv4Address, double> Averagelatencies; // Address -> [List of Latencies in ms]

// Function to calculate latencies for each packet
void CalculateLatencies() {
    for (const auto &destEntry : receiveTimestamps) {
        Ipv4Address destAddress = destEntry.first;
        const auto &packets = destEntry.second;
        //std::cout << destAddress << std::endl;
        double avg_latency = 0;
        for (const auto &packetEntry : packets) {
            uint64_t packetId = packetEntry.first;
            Time receiveTime = packetEntry.second;

            // Look up the corresponding send time
            auto sendIt = sendTimestamps[destAddress].find(packetId);
            if (sendIt != sendTimestamps[destAddress].end()) {
                Time sendTime = sendIt->second;

                // Calculate the latency in milliseconds
                double latency = (receiveTime - sendTime).GetSeconds();

                // Store the latency in the packetLatencies map
                packetLatencies[destAddress].push_back(latency);
                avg_latency += latency;

            } 
            else {
                std::cerr << "Send timestamp not found for Packet ID: " << packetId << " at destination address.\n";
            }
        }
        double packets_number = (double)(packets.size());
        Averagelatencies[destAddress]=avg_latency/packets_number;
    }
}

// print latencies for each address
void PrintEachLatency() 
{
    for (const auto &latencyEntry : packetLatencies) {
        Ipv4Address destAddress = latencyEntry.first;
        const std::vector<double> &latencies = latencyEntry.second;

        std::cout << "Latencies for address " << destAddress << ":\n";
        for (double latency : latencies) {
            std::cout << latency << " ms, ";
        }
        std::cout << std::endl;
    }
}
// print total average latency
void PrintAverageLatency() 
{
    for (const auto &latencyEntry : Averagelatencies) {
    Ipv4Address destAddress = latencyEntry.first;
    double latencies = latencyEntry.second;

    std::cout << "Average latencies for  address " << destAddress << " is: "
                << latencies << " s" << std::endl;
    }
}


int
main(int argc, char* argv[])
{

// Define all the parameters to be used in our network and parse it by the terminal
bool udp{true};   // UDP/TCP application and I need to know which one of them I am expecting higher throughput
bool downlink{true}; // Whether we are working in uplink or downlink
bool useRts{true}; // use the RTS or not in connection, I will check this will affect the performance due to increasing the overhead or not
bool useExtendedBlockAck{false}; // Illustrate more it for me
bool enableUlOfdma{false}; // enable ulOFDMA or not
bool enableBsrp{false}; // enable or not the buffer status report polling

double distance{10.0};      // meters
double frequency{2.4};       // whether 2.4, 5 or 6 GHz
int mcs{4}; // -1 indicates an unset value
uint32_t payloadSize = 700; //  must fit in the max TX duration when transmitting at MCS 0 over an RU of 26 tones
std::size_t nStations{1};   // number of stations 
double simulationTime{10}; // seconds


std::string dlAckSeqType{"NO-OFDMA"};  // 
Time accessReqInterval{0};   // Time is a class used in ns3 to represent the time durations 
std::string phyModel{"Spectrum"};  // std::string is a string tye in c++, it uses the brace initializer to set the phyModel to the string yans so phyModel is a variable of type strind and initiated to yans
bool verbose{false};
bool enablePcap{true};
int channelWidth = 20;
int gi = 800;
uint32_t number_packets = 4294967295U;
//4294967295U

// Parsing using the commandline 
CommandLine cmd(__FILE__);
cmd.AddValue("frequency",
                 "Whether working in the 2.4, 5 or 6 GHz band (other values gets rejected)",
                 frequency);
cmd.AddValue("distance",
                 "Distance in meters between the station and the access point",
                 distance);
cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
cmd.AddValue("udp", "UDP if set to 1, TCP otherwise", udp);
cmd.AddValue("downlink",
                 "Generate downlink flows if set to 1, uplink flows otherwise",
                 downlink);
cmd.AddValue("useRts", "Enable/disable RTS/CTS", useRts);
cmd.AddValue("useExtendedBlockAck", "Enable/disable use of extended BACK", useExtendedBlockAck);
cmd.AddValue("nStations", "Number of non-AP HE stations", nStations);
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
cmd.AddValue("phyModel",
                 "PHY model to use when OFDMA is disabled (Yans or Spectrum). If OFDMA is enabled "
                 "then Spectrum is automatically selected",
                 phyModel);

cmd.AddValue("channelWidth", "channelWidth used",  channelWidth);
cmd.AddValue("number_packets", "number of packets transmitted",  number_packets);
cmd.AddValue("gi", "Guard interval",  gi);
cmd.AddValue("tracing", "Generate trace files", tracing);

cmd.Parse(argc, argv);


if (verbose)
{
    WifiHelper::EnableLogComponents(LOG_LEVEL_INFO);
}
if (tracing)
{
    phyTxTraceFile.open("wifi-6-phy-tx-trace.txt");

    macTxTraceFile.open("wifi-mac-tx-trace.txt");
    macRxTraceFile.open("wifi-mac-rx-trace.txt");
    DropMacTraceFile.open("wifi-mac-drop-trace.txt");
    DropMpduTraceFile.open("wifi-mac-drop-mpdu.txt");

    EnqueueTraceFile.open("wifi-enqueue-trace.txt");
    DequeueTraceFile.open("wifi-dequeue-trace.txt");
    NumberofqueuedpacketsFile.open("wifi-packets-in-queue-trace.txt");

    DropEnqueueTraceFile.open("wifi-drop-before-enqueue.txt");
    DropDequeueTraceFile.open("wifi-drop-after-dequeue.txt");
   
    cwTraceFile.open("wifi-cw-trace.txt");
    backoffTraceFile.open("wifi-backoff-trace.txt");
}
LatecnyCdfFile.open("LatecnyCdf.txt");


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



/* create the nodes for the stations and the AP  */
NodeContainer wifiStaNodes;
wifiStaNodes.Create(nStations);
NodeContainer WifiApNode;
WifiApNode.Create(1);


NetDeviceContainer apDevice;
NetDeviceContainer staDevices;
WifiHelper wifi;
WifiMacHelper mac;


std::string channelStr("{0, " + std::to_string(channelWidth) + ", ");
StringValue ctrlRate;
auto nonHtRefRateMbps = HePhy::GetNonHtReferenceRate(mcs) / 1e6;
std::ostringstream ossDataMode;
ossDataMode << "HeMcs" << mcs;


if (frequency == 6)
{
    // Sets the Wi-Fi standard to 802.11ax. //Appends BAND_6GHZ to channelStr. //Sets ctrlRate using the value from ossDataMode.str().
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    channelStr += "BAND_6GHZ, 0}";
    ctrlRate = StringValue(ossDataMode.str());
}
else if (frequency == 5)
{   
    //HE used the default propoagation loss model in WIFI_STANDARD_80211ax which is LogDistancePropagationLossModel by the way
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlMode;
    ossControlMode << "OfdmRate" << nonHtRefRateMbps << "Mbps";
    ctrlRate = StringValue(ossControlMode.str());
    channelStr += "BAND_5GHZ, 0}";
}
else if (frequency == 2.4)
{
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlMode;
    ossControlMode << "ErpOfdmRate" << nonHtRefRateMbps << "Mbps";
    ctrlRate = StringValue(ossControlMode.str());
    channelStr += "BAND_2_4GHZ, 0}";}
else 
{
// NS_FATAL_ERROR is different than the NS_ABort_msg thaat besides aborting the simulation immediately it includes the message, the line file and the line information
    NS_FATAL_ERROR("Wrong frequency value!");
}


wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                             "DataMode", StringValue(ossDataMode.str()),
                             "ControlMode", ctrlRate);
wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
Ssid ssid = Ssid("ns3-80211ax");


SpectrumWifiPhyHelper spectrumPhy;
spectrumPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
spectrumPhy.Set("ChannelSettings", StringValue(channelStr));


Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();

if (frequency == 2.4){
    lossModel->SetFrequency(2.4e9);
}
else if (frequency == 5){
    lossModel->SetFrequency(5.180e9);
}
else if (frequency == 6){
    lossModel->SetFrequency(6e9);
}
else 
{
    NS_FATAL_ERROR("Wrong frequency value!");
}
spectrumChannel->AddPropagationLossModel(lossModel);
Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
spectrumChannel->SetPropagationDelayModel(delayModel);

spectrumPhy.SetChannel(spectrumChannel);
spectrumPhy.SetErrorRateModel("ns3::TableBasedErrorRateModel");

mac.SetType("ns3::StaWifiMac",
            "Ssid",
            SsidValue(ssid),
            "MpduBufferSize",
            UintegerValue(useExtendedBlockAck ? 256 : 64));
staDevices = wifi.Install(spectrumPhy, mac, wifiStaNodes);


/*AP MAC configuration*/ 
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
apDevice = wifi.Install(spectrumPhy, mac, WifiApNode);
        

/*Retrieve parameters of AP only now*/
Ptr<WifiNetDevice> APdeviceaccessed = DynamicCast<WifiNetDevice>(apDevice.Get(0));
Ptr<WifiPhy> AP_phy = DynamicCast<WifiPhy>(APdeviceaccessed->GetPhy());
double CCA_threshold  =  AP_phy->GetCcaEdThreshold(); 
double CCA_sensitivity  =  AP_phy->GetCcaSensitivityThreshold ();
double Rx_Sensitivity =  AP_phy->GetRxSensitivity ();
double  Num_antennas  =  AP_phy->GetNumberOfAntennas ();
//double  PhyId  =  AP_phy->GetPhyId ();
//Time slot_t =  AP_phy->GetSlot ();
double Rx_gain = AP_phy->GetRxGain ();
double Tx_gain = AP_phy->GetTxGain ();
double TxPowerEnd = AP_phy->GetTxPowerEnd ();
double TxPowerStart = AP_phy->GetTxPowerStart ();
UintegerValue  Txstreams;
UintegerValue  Rxstreams;
AP_phy->GetAttribute("MaxSupportedTxSpatialStreams", Txstreams);
AP_phy->GetAttribute("MaxSupportedTxSpatialStreams", Rxstreams);


//std::cout << "PhyId " << PhyId << std::endl;
//std::cout << "slot_t " << slot_t << std::endl;
std::cout << "TxPowerEnd in dBm " << TxPowerEnd << std::endl;
std::cout << "TxPowerStart in dBm " << TxPowerStart << std::endl;
std::cout << "Rx_gain in dB " << Rx_gain << std::endl;
std::cout << "Tx_gain in dB " << Tx_gain << std::endl;
std::cout << "Num_antennas " << Num_antennas << std::endl;
std::cout << "Txstreams " << Txstreams.Get() << std::endl;
std::cout << "Rxstreams " << Rxstreams.Get() << std::endl;
std::cout << "CCA_threshold " << CCA_threshold << std::endl;
std::cout << "CCA_sensitivity " << CCA_sensitivity << std::endl;
std::cout << "Rx_Sensitivity " << Rx_Sensitivity << std::endl;

// Retrieve the parameters of the MAC layer
Ptr<ApWifiMac> AP_mac = DynamicCast<ApWifiMac>(APdeviceaccessed->GetMac());
bool check_QOS_supp = AP_mac->GetQosSupported ();
std::cout << "Is_QOS_Supp " << check_QOS_supp << std::endl;
//uint8_t Num_links = AP_mac->GetNLinks() ;
//std::cout << "Number of links " << static_cast<int>(Num_links)  << std::endl;

    
//When a packet is received by the MAC, to be sent to the PHY, it is queued in the internal queue after being tagged by the current time.
//so it saves the packets and tag it with the current time until 
Ptr< WifiMacQueue > Mac_queue_BE = DynamicCast<WifiMacQueue>(AP_mac->GetTxopQueue (AC_BE));
Ptr< WifiMacQueue > Mac_queue_BK = DynamicCast<WifiMacQueue>(AP_mac->GetTxopQueue (AC_BK));
Ptr< WifiMacQueue > Mac_queue_VI = DynamicCast<WifiMacQueue>(AP_mac->GetTxopQueue (AC_VI));
Ptr< WifiMacQueue > Mac_queue_VO = DynamicCast<WifiMacQueue>(AP_mac->GetTxopQueue (AC_VO));
auto maxSize = Mac_queue_BE->GetMaxSize();
std::cout << "MaxSize_queue : " << maxSize << std::endl;
uint32_t NumPackets_BE = Mac_queue_BE->GetNPackets();
std::cout << "NumPackets_BE " << NumPackets_BE << std::endl;
uint32_t NumPackets_VO = Mac_queue_VO->GetNPackets();
std::cout << "NumPackets_VO " << NumPackets_VO << std::endl;

/*
// Trace packets evalution in the mac layer for different Txops without using config paths
Mac_queue_BE->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&TracepacketsinQueue));
Mac_queue_BE->TraceConnectWithoutContext("Enqueue", MakeCallback(&EnqueuTrace));
Mac_queue_BE->TraceConnectWithoutContext("Dequeue", MakeCallback(&DequeueTrace));
Mac_queue_BE->TraceConnectWithoutContext("DropAfterDequeue", MakeCallback(&DropDequeuTrace));
Mac_queue_BE->TraceConnectWithoutContext("DropBeforeEnqueue", MakeCallback(&DropEnqueueTrace));
*/

Ptr<QosTxop> QOS_EDCA = DynamicCast<QosTxop>(AP_mac->GetQosTxop(AC_BE)); 
Time TXOPLimit = QOS_EDCA->GetTxopLimit (0);
uint32_t MinCW = QOS_EDCA->GetMinCw (0);
uint32_t MaxCW = QOS_EDCA->GetMaxCw(0);
std::cout << "TXOPLimit " << TXOPLimit << std::endl;
std::cout << "MinCW " << MinCW << std::endl;
std::cout << "MaxCW " << MaxCW << std::endl;

/*Mobility*/
MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
positionAlloc->Add(Vector(0.0, 0.0, 0.0));
positionAlloc->Add(Vector(distance, 0.0, 0.0));
positionAlloc->Add(Vector(distance, distance, 0.0));
mobility.SetPositionAllocator(positionAlloc);
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.Install(WifiApNode);
mobility.Install(wifiStaNodes);



/*Internet stack*/
int64_t streamNumber = 150;
streamNumber += wifi.AssignStreams(apDevice, streamNumber);
streamNumber += wifi.AssignStreams(staDevices, streamNumber);


InternetStackHelper stack;
stack.Install(WifiApNode);
stack.Install(wifiStaNodes);
streamNumber += stack.AssignStreams(WifiApNode, streamNumber);
streamNumber += stack.AssignStreams(wifiStaNodes, streamNumber);

Ipv4AddressHelper address;
address.SetBase("192.168.1.0", "255.255.255.0");
Ipv4InterfaceContainer staNodeInterfaces;
Ipv4InterfaceContainer apNodeInterface;

staNodeInterfaces = address.Assign(staDevices);
apNodeInterface = address.Assign(apDevice);




/*Applications*/

const auto maxRate = HePhy::GetDataRate(mcs, channelWidth, gi, 1)/nStations;
double  maxRate_mbps = maxRate/1000000;
double  maxRate_lower = maxRate_mbps * 0.1;

std::ostringstream oss;
oss << std::fixed << std::setprecision(0) << maxRate_lower << "Mbps"; // Convert to string with 0 decimal places
std::string rateString = oss.str();
std::cout << rateString << std::endl; // Output: "51Mbps"

/* Setting applications */
uint16_t port = 9; // Base port for applications
ApplicationContainer serverApp;

// Configure packet sinks for all stations
for (uint32_t i = 0; i < nStations; ++i)
{
    Address sinkAddress(InetSocketAddress(staNodeInterfaces.GetAddress(i), port));
    PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory", sinkAddress);
    serverApp.Add(packetSinkHelper.Install(wifiStaNodes.Get(i)));
}
serverApp.Start(Seconds(0.0));
serverApp.Stop(Seconds(simulationTime + 1));

NodeContainer clientNodes;
clientNodes.Add(WifiApNode.Get(0));
// Configure OnOff applications to send traffic to all sinks
ApplicationContainer clientApp;
for (uint32_t i = 0; i < nStations; ++i) {
    Address sinkAddress(InetSocketAddress(staNodeInterfaces.GetAddress(i), port));
    OnOffHelper onOff("ns3::UdpSocketFactory", sinkAddress);
    onOff.SetAttribute("DataRate", DataRateValue(DataRate(rateString)));
    onOff.SetAttribute("PacketSize", UintegerValue(payloadSize));
    onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    clientApp.Add(onOff.Install(clientNodes.Get(0)));

    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime + 1));
}



// Install FlowMonitor 
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll ();


 if (enablePcap)
{
    spectrumPhy.EnablePcap("wifi_6_DL", apDevice);
}


/*Trace sources*/

// Trace PHY Tx start events (AP node)
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxBegin",
//       MakeCallback(&PhyTxTrace));
// Trace PHY Tx end events
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxEnd",
//        MakeCallback(&PhyTxDoneTrace));
// Trace PHY Tx drops events
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxDrop",
//        MakeCallback(&PhyTxDrop));


/* Trace PHY Rx start events (STA node)*/
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxBegin",
//        MakeCallback(&PhyRxTrace));
// Trace PHY Rx payload start events
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxPayloadBegin",
//        MakeCallback(&PhyRxPayloadTrace));
// Trace PHY Rx drop events
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxDrop",
//        MakeCallback(&PhyRxDropTrace));
// Trace PHY Rx end events
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxEnd",
//        MakeCallback(&PhyRxDoneTrace));
// Log packet receptions
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/MonitorSnifferRx",
//        MakeCallback(&TracePacketReception));
//Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpClient/Tx",
//        MakeCallback(&ClientTx));
//Config::Connect("/NodeList/*/ApplicationList/*/$ns3:::UdpServer/Rx",
//        MakeCallback(&ServerRx));

Config::Connect("/NodeList/*/ApplicationList/*/$ns3::OnOffApplication/TxWithAddresses",
        MakeCallback(&ClientTxAdd));

Config::Connect("/NodeList/*/ApplicationList/*/$ns3::PacketSink/RxWithAddresses",
    MakeCallback(&ServerRxAdd));




/* Mac trace */ 
//Trace CW evolution
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::WifiMac/BE_Txop/CwTrace",
//                MakeCallback(&CwTrace));
// Trace backoff evolution
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::WifiMac/BE_Txop/BackoffTrace",
//                    MakeCallback(&BackoffTrace));
// Trace packet transmission by the device (DL) 
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::ApWifiMac/MacTx",
//        MakeCallback(&MacTxTrace));
// Trace packet receptions to the device(UL)
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::ApWifiMac/MacRx",
//        MakeCallback(&MacRxTrace));
// Trace packet dropped at mac before transmission (DL)
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::ApWifiMac/MacTxDrop",
//        MakeCallback(&DropMacTrace));
// Trace mpdus dropped at mac before transmission (DL)
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::ApWifiMac/DroppedMpdu",
//        MakeCallback(&mpduDropTrace));

      
g_signalDbmAvg = 0;
g_noiseDbmAvg = 0;
g_samples = 0;


/*Runnig simulation*/
/*
Time dataStartTime = Seconds(1.2); // leaving enough time for beacon and association procedure
Time dataDuration = MicroSeconds(300); // leaving enough time for data transfer (+ acknowledgment)
Simulator::Schedule(dataStartTime,
                    &SendPacket,
                    apDevice.Get(0),
                    staDevices.Get(0)->GetAddress());
*/

Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);
Simulator::Stop(Seconds(simulationTime + 1));
Simulator::Run();


if (tracing)
{
    cwTraceFile.flush();
    backoffTraceFile.flush();
    phyTxTraceFile.flush();
    macTxTraceFile.flush();
    macRxTraceFile.flush();
    DropMacTraceFile.flush();
    DropMpduTraceFile.flush();
    EnqueueTraceFile.flush();
    DequeueTraceFile.flush();
    NumberofqueuedpacketsFile.flush();
    DropEnqueueTraceFile.flush();
    DropDequeueTraceFile.flush();
}

double throughput = 0;
// Tracing output 
for (auto it = bytesReceived.begin(); it != bytesReceived.end(); it++)
{
    Time first = timeFirstReceived.find(it->first)->second;
    Time last = timeLastReceived.find(it->first)->second;
    Time dataTransferDuration = (last - first);
    uint64_t Phy_delay = (last - first).GetSeconds();
    double nodeThroughput =
        (it->second * 8 / static_cast<double>(dataTransferDuration.GetMicroSeconds()));
    throughput += nodeThroughput;
    uint64_t nodeTxPackets = GetCount(packetsTransmitted, it->first);
    uint64_t nodeRxPackets = GetCount(packetsReceived, it->first);
    uint64_t nodePhyHeaderFailures = GetCount(phyHeaderFailed, it->first);
    uint64_t nodeRxEventWhileDecodingPreamble =
        GetCount(rxEventWhileDecodingPreamble, it->first);
    uint64_t nodeRxEventWhileRxing = GetCount(rxEventWhileRxing, it->first);
    uint64_t nodeRxEventWhileTxing = GetCount(rxEventWhileTxing, it->first);
    uint64_t nodeRxEventAbortedByTx = GetCount(rxEventAbortedByTx, it->first);
    //uint64_t nodeRxEvents = nodePhyHeaderFailures + 
    //                        nodeRxEventWhileDecodingPreamble + nodeRxEventWhileRxing +
    //                       nodeRxEventWhileTxing + nodeRxEventAbortedByTx;
    std::cout << "Node " << it->first << ": TX packets " << nodeTxPackets
                << "; RX packets " << nodeRxPackets << "; PHY header failures "
                << nodePhyHeaderFailures << "; RX events while decoding preamble "
                << nodeRxEventWhileDecodingPreamble << "; RX events while RXing "
                << nodeRxEventWhileRxing << "; RX events while TXing "
                << nodeRxEventWhileTxing << "; RX events aborted by TX "
                << nodeRxEventAbortedByTx  << "; time first RX "
                << first << "; time last RX " << last << "; dataTransferDuration "
                << Phy_delay << "; throughput " << nodeThroughput << " Mbps"
                << std::endl;
}
std::cout << "Total throughput: " << throughput << " Mbps" << std::endl;
// Traced Arrays
std::cout << "Number_packets_txed_mac " << Number_packets_txed_mac << std::endl;
std::cout << "Number_packets_rxed_mac " << Number_packets_rxed_mac << std::endl;
std::cout << "Number_dropped_packets_at_mac " << Number_dropped_packets_at_mac << std::endl;
std::cout << "Dropped_mpdus " << Dropped_mpdus << std::endl;
std::cout << "Number_dropped_packets_mac_before_Enqueue " << Number_dropped_packets_mac_before_Enqueue << std::endl;
std::cout << "Number_dropped_packets_mac_after_dequeue " << Number_dropped_packets_mac_after_dequeue << std::endl;

std::cout << "numTxedPackets_data " << numTxedPackets_data << std::endl;
std::cout << "numTxedPackets_other " << numTxedPackets_other << std::endl;
std::cout << "numTxedPackets_ended " << numTxedPackets_ended << std::endl;
std::cout << "Txed_dropped " << Txed_dropped << std::endl;
std::cout << "numPackets_rxed " << numPackets_rxed << std::endl;
std::cout << "numPackets_rxed_ctr " << numPackets_rxed_ctr << std::endl;
std::cout << "numrxedPackets_dropped_phy " << numrxedPackets_dropped_phy << std::endl;
std::cout << "udp_packets " << udp_packets << std::endl;


         
// Print per flow statistics
monitor->CheckForLostPackets ();
Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

double through = 0;
double delay = 0;
double txed_packets = 0;
double rxed_packets = 0;
double lost_packets = 0;
std::cout << std::setw(5) << "Delay(s) " << std::setw(15) << "txed packets " << std::setw(13) << "rxed packets "
              << std::setw(12) << "lost packets " <<  std::setw(10) << "Datarate" << std::setw(15) << "Throughput " 
              <<  std::setw(12) << "Signal(dBm)" << std::setw(12) << "Noise(dBm)"
              << std::setw(12) << "SNR(dB)" << std::endl;
for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
{   
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
    //std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
    //std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
    through = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024;
    //std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024  << " Mbps\n";
    //std::cout << "Time last packet received " << i->second.timeFirstTxPacket.GetSeconds () << "\n";*
    delay = (i->second.delaySum.GetSeconds())/(i->second.rxPackets);
    std::cout << "  Delay sum:  " << (i->second.delaySum.GetSeconds()) << " s\n";
    txed_packets = (i->second.txPackets);
    //std::cout << "  txed packets: " << (i->second.txPackets)<< "\n";
    rxed_packets = (i->second.rxPackets);
    //std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
    lost_packets = (i->second.lostPackets);
    //std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";
    std::cout << std::setw(5) << delay << std::setw(10) << txed_packets << std::setw(13) << rxed_packets
              << std::setw(12) << lost_packets  << std::setw(12) << (maxRate/1000000) << std::setw(15) << through <<  std::setw(14)
              << g_signalDbmAvg << std::setw(13) << g_noiseDbmAvg
              << std::setw(15) << (g_signalDbmAvg - g_noiseDbmAvg) << std::endl;

}

std::cout << "Tx_udp_packets " << Tx_udp_packets << std::endl;
std::cout << "Rx_udp_packets " << Rx_udp_packets << std::endl;
// Print the send and receive time stamps maps 
//PrintSendTimestamps(sendTimestamps);
//PrintReceiveTimestamps(receiveTimestamps);
CalculateLatencies();
//PrintEachLatency();
PrintAverageLatency();

LatecnyCdfFile.flush();
for (const auto& entry : packetLatencies) {
    // Write the latencies as a comma-separated list
    for (size_t i = 0; i < entry.second.size(); ++i) {
        LatecnyCdfFile << entry.second[i];
        if (i != entry.second.size() - 1) {
            LatecnyCdfFile << ",";
        }
    }
    LatecnyCdfFile << std::endl;
}
LatecnyCdfFile.close();



if (tracing)
    {
        cwTraceFile.close();
        backoffTraceFile.close();
        phyTxTraceFile.close();
        macTxTraceFile.close();
        macRxTraceFile.close();
        DropDequeueTraceFile.close();
        DropEnqueueTraceFile.close();
        NumberofqueuedpacketsFile.close();
        DequeueTraceFile.close();
        EnqueueTraceFile.close();
        DropMpduTraceFile.close();
        DropMacTraceFile.close();
    }

Simulator::Destroy();
return 0;
}