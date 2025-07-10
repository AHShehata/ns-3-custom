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


uint32_t pkt_Size = 1200; ///< packet size used for the simulation (in bytes)
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


                
mac.SetType("ns3::StaWifiMac",
            "Ssid",
            SsidValue(ssid),
            "MpduBufferSize",
            UintegerValue(useExtendedBlockAck ? 256 : 64));
staDevices = wifi.Install(spectrumPhy, mac, wifiStaNodes);

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




auto maxRate = HePhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
double  maxRate_lower = maxRate/1000000;
std::cout << "maxRate " << maxRate/1000000 << std::endl;
//maxRate_lower =  maxRate_lower * 0.1;
std::ostringstream dataRateStream;
dataRateStream <<  maxRate_lower << "Mbps";
std::string dataRateStr = dataRateStream.str(); 

 
// Install PacketSink (server) application on STAs to receive traffic
uint16_t port = 9;
PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
ApplicationContainer serverApp = sinkHelper.Install (wifiStaNodes);
serverApp.Start (Seconds (0.0));
serverApp.Stop (Seconds (simulationTime + 1.0));

// Install OnOff application (client) on AP to send traffic to STAs
OnOffHelper onOff ("ns3::UdpSocketFactory", Address ());
onOff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
onOff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));  // No off time for continuous traffic
onOff.SetAttribute ("DataRate", StringValue (dataRateStr)); // Traffic data rate
onOff.SetAttribute ("PacketSize", UintegerValue (payloadSize)); // Size of each packet

ApplicationContainer clientApps;
NodeContainer clientNodes;
clientNodes.Add(WifiApNode.Get(0));
  // Send traffic from AP to each STA
for (uint32_t i = 0; i < nStations; ++i)
{
    AddressValue remoteAddress (InetSocketAddress (staNodeInterfaces.GetAddress (i), port));
    onOff.SetAttribute ("Remote", remoteAddress);

    // Install the OnOff application on the AP node
    clientApps.Add (onOff.Install (clientNodes.Get(0)));
}

clientApps.Start (Seconds (1.0));   // Start the client application at 1 second
clientApps.Stop (Seconds (simulationTime + 1.0));  // Stop it after the simulation time



/*
  // Create a PacketSink (Receiver) application on each station
Ipv4InterfaceContainer serverInterfaces;
ApplicationContainer serverApp;
uint16_t port = 9;
for (std::size_t i = 0; i < nStations; i++)
{
    serverInterfaces.Add(staNodeInterfaces.Get(i));
}
  for (uint32_t i = 0; i < nStations; i++)
  {
    Address sinkAddress (InetSocketAddress (staNodeInterfaces.GetAddress (i), port));
    PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory", sinkAddress);
    serverApp  = packetSinkHelper.Install (wifiStaNodes.Get (i));
    serverApp.Start (Seconds (0.0)); // Start receiving at 1s
    serverApp.Stop (Seconds (simulationTime+1)); // Stop at the end of the simulation
  }


// Create the OnOffApplication (Client) 
NodeContainer clientNodes;
clientNodes.Add(WifiApNode.Get(0));
std::ostringstream dataRateStream;
dataRateStream << maxRate << "Mbps";
std::string dataRateStr = dataRateStream.str(); 
for (std::size_t i = 0; i < nStations; i++) 
{
    Address sinkAddress (InetSocketAddress (serverInterfaces.GetAddress (i), port));
    OnOffHelper onOff ("ns3::UdpSocketFactory", sinkAddress) ;
    onOff.SetAttribute ("DataRate", StringValue (dataRateStr)); // Set data rate
    onOff.SetAttribute ("PacketSize", UintegerValue (payloadSize)); // Packet size
    onOff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));
    onOff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));


    ApplicationContainer clientApp = onOff.Install(clientNodes.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime + 1));
}
*/

// Install FlowMonitor 
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll ();


 if (enablePcap)
{
    spectrumPhy.EnablePcap("wifi_6_DL_on_off", apDevice);
}



Time dataStartTime =
        Seconds(1.2); // leaving enough time for beacon and association procedure
//Time dataDuration =
    MicroSeconds(300); // leaving enough time for data transfer (+ acknowledgment)
Simulator::Schedule(dataStartTime,
                    &SendPacket,
                    apDevice.Get(0),
                    staDevices.Get(0)->GetAddress());
Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);
Simulator::Stop(Seconds(simulationTime + 1));
Simulator::Run();




uint64_t totalPacketsThrough = 0;
std::vector<double> Sta_Throughput(nStations);
for (uint32_t i = 0; i < serverApp.GetN(); i++)
{
    totalPacketsThrough =  DynamicCast<PacketSink>(serverApp.Get(i))->GetTotalRx();
    Sta_Throughput[i] = totalPacketsThrough * 8 / (simulationTime * 1000000.0); // Mbit/s
}
std::cout << "Throughput of stations" << " ";
for (uint32_t count = 0; count <  serverApp.GetN(); count++) 
{
    std::cout <<  Sta_Throughput[count]  << ", " << " ";
}
 std::cout << std::endl;   


double rxBytes = 0;
for (uint32_t i = 0; i < serverApp.GetN(); i++)
{
    rxBytes += DynamicCast<PacketSink>(serverApp.Get(i))->GetTotalRx();
}
double tot_throughput = (rxBytes * 8) / (simulationTime * 1000000.0); // Mbit/s
std::cout << "Total throughput " <<  tot_throughput  << std::endl;



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
              << std::setw(12) << "lost packets " << std::setw(10) << "Throughput " << std::endl;
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
    //std::cout << "  Delay:      " << (i->second.delaySum.GetSeconds())/(i->second.rxPackets)<< " s\n";
    txed_packets = (i->second.txPackets);
    //std::cout << "  txed packets: " << (i->second.txPackets)<< "\n";
    rxed_packets = (i->second.rxPackets);
    //std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
    lost_packets = (i->second.lostPackets);
    //std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";
    std::cout << std::setw(5) << delay << std::setw(10) << txed_packets << std::setw(13) << rxed_packets
              << std::setw(12) << lost_packets << std::setw(15) << through << std::endl;

}

          
             

return 0;
}