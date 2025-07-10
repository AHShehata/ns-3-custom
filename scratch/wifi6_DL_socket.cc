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
#include "ns3/ampdu-subframe-header.h"

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


uint32_t pktSize = 700; ///< packet size used for the simulation (in bytes)
bool tracing = true;

std::map<Mac48Address, uint64_t> packetsReceived; ///< Map that stores the total packets received
                                                  ///< per STA (and addressed to that STA)
std::map<Mac48Address, uint64_t>
    bytesReceived; ///< Map that stores the total bytes received per STA (and addressed to that STA)
std::map<Mac48Address, uint64_t>
    packetsTransmitted; ///< Map that stores the total packets transmitted per STA
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


std::ofstream cwTraceFile;      ///< File that traces CW over time
std::ofstream backoffTraceFile; ///< File that traces backoff over time
std::ofstream phyTxTraceFile;   ///< File that traces PHY transmissions  over time
std::ofstream macTxTraceFile;   ///< File that traces MAC transmissions  over time
std::ofstream macRxTraceFile;   ///< File that traces MAC receptions  over time
std::ofstream
    socketSendTraceFile; ///< File that traces packets transmitted by the application  over time



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

// Functions for tracing.

/**
 * Incremement the counter for a given address.
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
 * PHY TX trace
 *
 * \param context The context.
 * \param p The packet.
 * \param txPowerW The TX power.
 */
void
PhyTxTrace(std::string context, Ptr<const Packet> p, double txPowerW)
{
    std::cout << Simulator::Now() << " node=" << ContextToNodeId(context)
                                     << " size=" << p->GetSize() << " " << txPowerW << std::endl;
   
    if (p->GetSize() >= pktSize) // ignore non-data frames
    {
        Mac48Address addr = ContextToMac(context);
        IncrementCounter(packetsTransmitted, addr);
    }
}

uint32_t pkt_Size = 800; ///< packet size used for the simulation (in bytes)
void
SendPacket(Ptr<NetDevice> sourceDevice, Address& destination)
{
    Ptr<Packet> pkt = Create<Packet>(pkt_Size); // dummy bytes of data
    sourceDevice->Send(pkt, destination, 0);
}

int
main(int argc, char* argv[])
{

// Define all the parameters to be used in our network and parse it by the terminal
bool udp{true};   // UDP/TCP application and I need to know which one of them I am expecting higher throughput
bool downlink{true}; // Whether we are working in uplink or downlink
bool useRts{false}; // use the RTS or not in connection, I will check this will affect the performance due to increasing the overhead or not
bool useExtendedBlockAck{false}; // Illustrate more it for me
bool enableUlOfdma{false}; // enable ulOFDMA or not
bool enableBsrp{false}; // enable or not the buffer status report polling

double distance{1.0};      // meters
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
int number_packets = 100;
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


cmd.Parse(argc, argv);



if (verbose)
{
    WifiHelper::EnableLogComponents(LOG_LEVEL_INFO);
}




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



// create the nodes for the stations and the AP 
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
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(48));

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
    channelStr += "BAND_2_4GHZ, 0}";
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",  DoubleValue(40));
}
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

// AP MAC configuration
mac.SetType("ns3::ApWifiMac",
            "EnableBeaconJitter",
            BooleanValue(false),
            "Ssid",
            SsidValue(ssid));
apDevice = wifi.Install(spectrumPhy, mac, WifiApNode);
        


                
mac.SetType("ns3::StaWifiMac",
            "Ssid",
            SsidValue(ssid),
            "MpduBufferSize",
            UintegerValue(useExtendedBlockAck ? 256 : 64));
staDevices = wifi.Install(spectrumPhy, mac, wifiStaNodes);




MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
positionAlloc->Add(Vector(0.0, 0.0, 0.0));
positionAlloc->Add(Vector(distance, 0.0, 0.0));
mobility.SetPositionAllocator(positionAlloc);
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.Install(WifiApNode);
mobility.Install(wifiStaNodes);


int64_t streamNumber = 150;
streamNumber += wifi.AssignStreams(apDevice, streamNumber);
streamNumber += wifi.AssignStreams(staDevices, streamNumber);


Ptr<WifiNetDevice> ap_device = DynamicCast<WifiNetDevice>(apDevice.Get(0));
Ptr<WifiNetDevice> sta_device = DynamicCast<WifiNetDevice>(staDevices.Get(0));


// give packet socket powers to nodes.
PacketSocketHelper packetSocket;
packetSocket.Install(wifiStaNodes);
packetSocket.Install(WifiApNode);

PacketSocketAddress socket;
socket.SetSingleDevice(ap_device->GetIfIndex());
socket.SetPhysicalAddress(sta_device->GetAddress());
socket.SetProtocol(1);

const auto maxRate = HePhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
std::cout << "maxRate " << maxRate/1000000 << std::endl;
const auto packetInterval = payloadSize * 8.0 / maxRate;  // For calculating the duration of the packet
Ptr<PacketSocketClient> client = CreateObject<PacketSocketClient>();
client->SetAttribute("MaxPackets", UintegerValue(number_packets));
client->SetAttribute("PacketSize", UintegerValue(payloadSize));
client->SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
client->SetRemote(socket);
WifiApNode.Get(0)->AddApplication(client);
client->SetStartTime(Seconds(1.0));
client->SetStopTime(Seconds(simulationTime+1));



Ptr<PacketSocketServer> server = CreateObject<PacketSocketServer>();
server->SetLocal(socket);
wifiStaNodes.Get(0)->AddApplication(server);
server->SetStartTime(Seconds(0.0));
server->SetStopTime(Seconds(simulationTime+1));


// Log packet receptions
    Config::Connect(
        "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/MonitorSnifferRx",
        MakeCallback(&TracePacketReception));
// Trace PHY Tx start events
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxBegin",
            MakeCallback(&PhyTxTrace));


 if (enablePcap)
{
    spectrumPhy.EnablePcap("wifi_6_DL_socket", apDevice);
}

/* Need to send data packet because beacon and association frames shall be sent using lowest
    * rate */
// Send one data packet (this packet is sent using data rate / MCS defined above) once
// association is done (otherwise dropped)
Time dataStartTime =
        Seconds(1.2); // leaving enough time for beacon and association procedure
//Time dataDuration =   MicroSeconds(300); // leaving enough time for data transfer (+ acknowledgment)
Simulator::Schedule(dataStartTime,
                    &SendPacket,
                    apDevice.Get(0),
                    staDevices.Get(0)->GetAddress());
//Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);
Simulator::Stop(Seconds(simulationTime + 1));
Simulator::Run();



double nodeThroughput = 0; 
for (auto it = bytesReceived.begin(); it != bytesReceived.end(); it++)
{
    Time first = timeFirstReceived.find(it->first)->second;
    Time last = timeLastReceived.find(it->first)->second;
    Time dataTransferDuration = last - first;
    nodeThroughput = (it->second * 8 / static_cast<double>(dataTransferDuration.GetMicroSeconds()));
    //throughput += nodeThroughput;
    uint64_t nodeTxPackets = GetCount(packetsTransmitted, it->first);
    uint64_t nodeRxPackets = GetCount(packetsReceived, it->first);
    std::cout << "Node " << it->first << ": TX packets " << nodeTxPackets
            << "; RX packets " << nodeRxPackets << "; time first RX "
            << first << "; time last RX " << last << "; dataTransferDuration "
            << dataTransferDuration << "; throughput " << nodeThroughput << " Mbps"
            << std::endl;
}


return 0;
}