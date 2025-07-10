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

double distance{30.0};      // meters
double frequency{2.4};       // whether 2.4, 5 or 6 GHz
double frequency2{5};       // whether 2.4, 5 or 6 GHz
double frequency3{0};       // whether 2.4, 5 or 6 GHz
std::size_t nStations{2};   // number of stations 

int mcs{4}; // -1 indicates an unset value
uint32_t payloadSize = 700; //  must fit in the max TX duration when transmitting at MCS 0 over an RU of 26 tones
double simulationTime{10}; // seconds


std::string dlAckSeqType{"NO-OFDMA"};  // 
Time accessReqInterval{0};   // Time is a class used in ns3 to represent the time durations 
std::string phyModel{"Spectrum"};  // std::string is a string tye in c++, it uses the brace initializer to set the phyModel to the string yans so phyModel is a variable of type strind and initiated to yans
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
cmd.AddValue("frequency",
              "Whether the first link operates in the 2.4, 5 or 6 GHz band (other values gets rejected)",
              frequency);
cmd.AddValue("frequency2",
            "Whether the second link operates in the 2.4, 5 or 6 GHz band (0 means the device has one "
            "link, otherwise the band must be different than first link and third link)",
            frequency2);
cmd.AddValue("frequency3",
            "Whether the third link operates in the 2.4, 5 or 6 GHz band (0 means the device has up to "
            "two links, otherwise the band must be different than first link and second link)",
            frequency3);


cmd.Parse(argc, argv);



/* create the nodes for the stations and the AP  */
NodeContainer wifiStaNodes;
wifiStaNodes.Create(nStations);
NodeContainer WifiApNode;
WifiApNode.Create(2);

WifiMacHelper mac1;
WifiHelper wifi1;



// First AP
SpectrumWifiPhyHelper spectrumWifiPhy1;
spectrumWifiPhy1.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
spectrumWifiPhy1.SetErrorRateModel ("ns3::TableBasedErrorRateModel");

Ptr<MultiModelSpectrumChannel> spectrumChannel1 = CreateObject<MultiModelSpectrumChannel>();
Ptr<FriisPropagationLossModel> lossModel1 = CreateObject<FriisPropagationLossModel>();
lossModel1->SetFrequency(2.4e9);
spectrumChannel1->AddPropagationLossModel(lossModel1);

Ptr<ConstantSpeedPropagationDelayModel> delayModel1 = CreateObject<ConstantSpeedPropagationDelayModel>();
spectrumChannel1->SetPropagationDelayModel(delayModel1);

std::string channelStr1 = "{0, " + std::to_string(channelWidth) + ", " + "BAND_2_4GHZ, 0}";
uint64_t nonHtRefRateMbpsB = HePhy::GetNonHtReferenceRate(mcs) / 1e6;
std::string dataModeStrB = "HeMcs" + std::to_string(mcs);
std::string ctrlRateStrB = "ErpOfdmRate" + std::to_string(nonHtRefRateMbpsB) + "Mbps"; 

wifi1.SetStandard(WIFI_STANDARD_80211ax);
wifi1.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
wifi1.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                        "DataMode", StringValue(dataModeStrB),
                        "ControlMode", StringValue(ctrlRateStrB));
spectrumWifiPhy1.Set("ChannelSettings", StringValue(channelStr1));
spectrumWifiPhy1.AddChannel(spectrumChannel1);

Ssid ssidA = Ssid("ns3-80211ax-A");
mac1.SetType("ns3::StaWifiMac",
        "Ssid",
        SsidValue(ssidA),
        "MpduBufferSize",
        UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevices1 = wifi1.Install(spectrumWifiPhy1, mac1, wifiStaNodes.Get(0));

mac1.SetType("ns3::ApWifiMac",
        "EnableBeaconJitter",
        BooleanValue(false),
        "Ssid",
        SsidValue(ssidA));
NetDeviceContainer apDevices1 = wifi1.Install(spectrumWifiPhy1, mac1, WifiApNode.Get(0));


// Second AP
WifiMacHelper mac2;
WifiHelper wifi2;

SpectrumWifiPhyHelper spectrumWifiPhy2;
spectrumWifiPhy2.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
spectrumWifiPhy2.SetErrorRateModel ("ns3::TableBasedErrorRateModel");


Ptr<MultiModelSpectrumChannel> spectrumChannel2 = CreateObject<MultiModelSpectrumChannel>();
Ptr<FriisPropagationLossModel> lossModel2 = CreateObject<FriisPropagationLossModel>();
lossModel2->SetFrequency(5e9);
spectrumChannel2->AddPropagationLossModel(lossModel2);

Ptr<ConstantSpeedPropagationDelayModel> delayModel2 = CreateObject<ConstantSpeedPropagationDelayModel>();
spectrumChannel2->SetPropagationDelayModel(delayModel2);

std::string channelStr2 = "{0, " + std::to_string(channelWidth) + ", " + "BAND_5GHZ, 0}";
std::string dataModeStrB2 = "HeMcs" + std::to_string(mcs);
std::string  ctrlRateStrB2 = "OfdmRate" + std::to_string(nonHtRefRateMbpsB) + "Mbps";

wifi2.SetStandard(WIFI_STANDARD_80211ax);
wifi2.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
wifi2.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                        "DataMode", StringValue(dataModeStrB2),
                        "ControlMode", StringValue(ctrlRateStrB2));
spectrumWifiPhy2.Set("ChannelSettings", StringValue(channelStr2));
spectrumWifiPhy2.AddChannel(spectrumChannel2);

Ssid ssidB = Ssid("ns3-80211ax-B");
mac2.SetType("ns3::StaWifiMac",
        "Ssid",
        SsidValue(ssidB),
        "MpduBufferSize",
        UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevices2 = wifi2.Install(spectrumWifiPhy2, mac2, wifiStaNodes.Get(1));

mac2.SetType("ns3::ApWifiMac",
        "EnableBeaconJitter",
        BooleanValue(false),
        "Ssid",
        SsidValue(ssidB));
NetDeviceContainer apDevices2 = wifi2.Install(spectrumWifiPhy2, mac2, WifiApNode.Get(1));



/*Mobility*/
MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
positionAlloc->Add(Vector(0.0, 0.0, 0.0));   //AP 1
positionAlloc->Add(Vector(0.0, 150.0, 0.0));   //AP 2
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.SetPositionAllocator(positionAlloc);
mobility.Install(WifiApNode);


positionAlloc->Add(Vector(30.0, 0.0, 0.0));  // STA 1 at (10, 0, 0)
positionAlloc->Add(Vector(30.0, 150.0, 0.0)); // STA 2
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.SetPositionAllocator(positionAlloc);
mobility.Install(wifiStaNodes);



/*Internet stack*/

InternetStackHelper stack;
stack.Install(WifiApNode);
stack.Install(wifiStaNodes);

Ipv4AddressHelper address;
address.SetBase("192.168.1.0", "255.255.255.0");
Ipv4InterfaceContainer staInterfaces1  = address.Assign(staDevices1);
Ipv4InterfaceContainer apInterfaces1 = address.Assign(apDevices1);

address.SetBase("192.168.2.0", "255.255.255.0");
Ipv4InterfaceContainer staInterfaces2 = address.Assign(staDevices2);
Ipv4InterfaceContainer apInterfaces2 = address.Assign(apDevices2);




/*Applications*/
/* Setting applications */
const auto maxRate =  HePhy::GetDataRate(mcs, channelWidth, gi, 1);
std::cout << "maxRate " << maxRate/1000000 << std::endl;
const auto packetInterval = payloadSize * 8.0 / maxRate;  // For calculating the duration of the packet


uint16_t port = 9;
// Install a UDP server on STA 1
UdpServerHelper udpServer1(port);
ApplicationContainer serverApp1 = udpServer1.Install(wifiStaNodes.Get(0));
serverApp1.Start(Seconds(0.0));
serverApp1.Stop(Seconds(simulationTime+1));

UdpClientHelper udpClient1(staInterfaces1.GetAddress(0), port);
udpClient1.SetAttribute("MaxPackets", UintegerValue(4294967295U));
udpClient1.SetAttribute("Interval", TimeValue(Seconds(packetInterval))); // Packet interval
udpClient1.SetAttribute("PacketSize", UintegerValue(payloadSize));    // Packet size
ApplicationContainer clientApp1 = udpClient1.Install(WifiApNode.Get(0)); // Install on AP
clientApp1.Start(Seconds(1.0));
clientApp1.Stop(Seconds(simulationTime+1));



// Install a UDP server on STA 2
UdpServerHelper udpServer2(port);
ApplicationContainer serverApp2 = udpServer2.Install(wifiStaNodes.Get(1));
serverApp2.Start(Seconds(0.0));
serverApp2.Stop(Seconds(simulationTime+1));

UdpClientHelper udpClient2(staInterfaces2.GetAddress(0), port);
udpClient2.SetAttribute("MaxPackets", UintegerValue(4294967295U));
udpClient2.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
udpClient2.SetAttribute("PacketSize", UintegerValue(payloadSize));
ApplicationContainer clientApp2 = udpClient2.Install(WifiApNode.Get(1)); // Install on AP
clientApp2.Start(Seconds(1.0));
clientApp2.Stop(Seconds(simulationTime+1));



// Install FlowMonitor 
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll ();


if (enablePcap)
{
    spectrumWifiPhy1.EnablePcap("wifi_check", apDevices1, true);
    spectrumWifiPhy2.EnablePcap("wifi_check", apDevices2, true);
}


//Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);
Simulator::Stop(Seconds(simulationTime + 1));
Simulator::Run();

    
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
              <<  std::endl;
for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
{   
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
    //std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
    //std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
    through = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024;
    //std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024  << " Mbps\n";
    std::cout << "Time last packet received " <<  (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) << "\n";
    delay = (i->second.delaySum.GetSeconds())/(i->second.rxPackets);
    std::cout << "  Delay sum:  " << (i->second.delaySum.GetSeconds()) << " s\n";
    txed_packets = (i->second.txPackets);
    //std::cout << "  txed packets: " << (i->second.txPackets)<< "\n";
    rxed_packets = (i->second.rxPackets);
    //std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
    lost_packets = (i->second.lostPackets);
    //std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";
    std::cout << std::setw(5) << delay << std::setw(10) << txed_packets << std::setw(13) << rxed_packets
              << std::setw(12) << lost_packets  << std::setw(12) << (maxRate/1000000) << std::setw(15) << through << std::endl;

}

Simulator::Destroy();
return 0;
}