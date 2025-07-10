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
#include "ns3/he-configuration.h"

#include <functional>

// This is a simple example in order to show how to configure an IEEE 802.11ax Wi-Fi network.
//
// It outputs the UDP or TCP goodput for every HE MCS value, which depends on the MCS value (0 to
// 11), the channel width (20, 40, 80 or 160 MHz) and the guard interval (800ns, 1600ns or 3200ns).
// The PHY bitrate is constant over all the simulation run. The user can also specify the distance
// between the access point and the station: the larger the distance the smaller the goodput.
//

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

uint32_t
ContextToNodeId(std::string context)
{
    std::string sub = context.substr(10);
    uint32_t pos = sub.find("/Device");
    return std::stoi(sub.substr(0, pos));
}

int nBSSs = 2;
std::vector<uint32_t> bytesReceived(nBSSs);
std::vector<uint32_t> packetsReceived(nBSSs);

void
SocketRx(std::string context, Ptr<const Packet> p, const Address& addr)
{
    uint32_t nodeId = ContextToNodeId(context);
    bytesReceived[nodeId] += p->GetSize();
    packetsReceived[nodeId] += 1; 
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
double simulationTime{1}; // seconds


std::string dlAckSeqType{"NO-OFDMA"};  // 
Time accessReqInterval{0};   // Time is a class used in ns3 to represent the time durations 
std::string phyModel{"Spectrum"};  // std::string is a string tye in c++, it uses the brace initializer to set the phyModel to the string yans so phyModel is a variable of type strind and initiated to yans
bool verbose{false};
bool enablePcap{true};
uint32_t nBSSs = 2;
uint32_t channelWidth = 20;
uint32_t gi = 800;
uint32_t number_packets = 100;
double d1 = 30.0;            // meters
double d2 = 30.0;            // meters
double d3 = 150.0;           // meters
bool enableObssPd = false;
double obssPdThreshold = -72.0; // dBm
double powSta1 = 10.0;       // dBm
double powSta2 = 10.0;       // dBm
double powAp1 = 21.0;        // dBm
double powAp2 = 21.0;        // dBm
double ccaEdTrSta1 = -62;    // dBm
double ccaEdTrSta2 = -62;    // dBm
double ccaEdTrAp1 = -62;     // dBm
double ccaEdTrAp2 = -62;     // dBm
double minimumRssi = -82;    // dBm

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
cmd.AddValue("nBSSs", "Number of Basic service sets", nBSSs);
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
cmd.AddValue("enableObssPd", "Enable/disable OBSS_PD", enableObssPd);
cmd.AddValue("d1", "Distance between STA1 and AP1 (m)", d1);
cmd.AddValue("d2", "Distance between STA2 and AP2 (m)", d2);
cmd.AddValue("d3", "Distance between AP1 and AP2 (m)", d3);
cmd.AddValue("powSta1", "Power of STA1 (dBm)", powSta1);
cmd.AddValue("powSta2", "Power of STA2 (dBm)", powSta2);
cmd.AddValue("powAp1", "Power of AP1 (dBm)", powAp1);
cmd.AddValue("powAp2", "Power of AP2 (dBm)", powAp2);
cmd.AddValue("ccaEdTrSta1", "CCA-ED Threshold of STA1 (dBm)", ccaEdTrSta1);
cmd.AddValue("ccaEdTrSta2", "CCA-ED Threshold of STA2 (dBm)", ccaEdTrSta2);
cmd.AddValue("ccaEdTrAp1", "CCA-ED Threshold of AP1 (dBm)", ccaEdTrAp1);
cmd.AddValue("ccaEdTrAp2", "CCA-ED Threshold of AP2 (dBm)", ccaEdTrAp2);
cmd.AddValue("minimumRssi",
                "Minimum RSSI for the ThresholdPreambleDetectionModel",
                minimumRssi);
cmd.AddValue("obssPdThreshold", "Threshold for the OBSS PD Algorithm", obssPdThreshold);

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
wifiStaNodes.Create(nStations*nBSSs);
NodeContainer WifiApNodes;
WifiApNodes.Create(nBSSs);


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
 if (enableObssPd)
{
    wifi.SetObssPdAlgorithm("ns3::ConstantObssPdAlgorithm",
                            "ObssPdLevel",
                            DoubleValue(obssPdThreshold));
}

wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(ossDataMode.str()),
                               "ControlMode", ctrlRate);
wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));


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


// Configuration of each AP and its stations
spectrumPhy.Set("TxPowerStart", DoubleValue(powSta1));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powSta1));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta1));
spectrumPhy.Set("RxSensitivity", DoubleValue(-92.0));
Ssid ssidA = Ssid("A");           
mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidA), 
            "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevicesA  = wifi.Install(spectrumPhy, mac, wifiStaNodes.Get(0));


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


spectrumPhy.Set("TxPowerStart", DoubleValue(powAp1));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powAp1));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp1));
spectrumPhy.Set("RxSensitivity", DoubleValue(-92.0));
mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidA),
            "EnableBeaconJitter",  BooleanValue(false));
NetDeviceContainer apDeviceA = wifi.Install(spectrumPhy, mac, WifiApNodes.Get(0));

Ptr<WifiNetDevice> apDevice = apDeviceA.Get(0)->GetObject<WifiNetDevice>();
Ptr<ApWifiMac> apWifiMac = apDevice->GetMac()->GetObject<ApWifiMac>();
if (enableObssPd)
{
    apDevice->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(1));
}

spectrumPhy.Set("TxPowerStart", DoubleValue(powSta2));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powSta2));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta2));
spectrumPhy.Set("RxSensitivity", DoubleValue(-92.0));

Ssid ssidB = Ssid("B");
mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidB), 
            "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDeviceB = wifi.Install(spectrumPhy, mac, wifiStaNodes.Get(1));

spectrumPhy.Set("TxPowerStart", DoubleValue(powAp2));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powAp2));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp2));
spectrumPhy.Set("RxSensitivity", DoubleValue(-92.0));

mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidB),
            "EnableBeaconJitter",  BooleanValue(false));
NetDeviceContainer apDeviceB = wifi.Install(spectrumPhy, mac, WifiApNodes.Get(1));

Ptr<WifiNetDevice> ap2Device = apDeviceB.Get(0)->GetObject<WifiNetDevice>();
//Ptr<ApWifiMac> apWifiMac = ap2Device->GetMac()->GetObject<ApWifiMac>();
if (enableObssPd)
{
    ap2Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(2));
}




// Mobility model
MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // AP1
positionAlloc->Add(Vector(d3, 0.0, 0.0));  // AP2
positionAlloc->Add(Vector(0.0, d1, 0.0));  // STA1
positionAlloc->Add(Vector(d3, d2, 0.0));   // STA2
mobility.SetPositionAllocator(positionAlloc);
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.Install(WifiApNodes);
mobility.Install(wifiStaNodes);






/* Socket*/
const auto maxRate = HePhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
std::cout << "maxRate " << maxRate/1000000 << std::endl;
//double  maxRate_lower = maxRate * 0.1;
const auto packetInterval = payloadSize * 8.0 / maxRate;  // For calculating the duration of the packet
//4294967295U
//number_packets = 5000 * 1.0;
PacketSocketHelper packetSocket;
packetSocket.Install(WifiApNodes);
packetSocket.Install(wifiStaNodes);

// BSS 1
{
    PacketSocketAddress socketAddr;
    socketAddr.SetSingleDevice(apDeviceA.Get(0)->GetIfIndex());
    socketAddr.SetPhysicalAddress(staDevicesA.Get(0)->GetAddress());
    socketAddr.SetProtocol(1);
    
    Ptr<PacketSocketClient> client = CreateObject<PacketSocketClient>();
    client->SetRemote(socketAddr);
    WifiApNodes.Get(0)->AddApplication(client);
    client->SetAttribute("PacketSize", UintegerValue(payloadSize));
    client->SetAttribute("MaxPackets", UintegerValue(number_packets));
    client->SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
    client->SetStartTime(Seconds(1.0));
    client->SetStopTime(Seconds(simulationTime+1));

    Ptr<PacketSocketServer> server = CreateObject<PacketSocketServer>();
    server->SetLocal(socketAddr);
    wifiStaNodes.Get(0)->AddApplication(server);
    server->SetStartTime(Seconds(0.0));
    server->SetStopTime(Seconds(simulationTime+1));
}

// BSS 2
{
    PacketSocketAddress socketAddr;
    socketAddr.SetSingleDevice(apDeviceB.Get(0)->GetIfIndex());
    socketAddr.SetPhysicalAddress(staDeviceB.Get(0)->GetAddress());
    socketAddr.SetProtocol(1);

    Ptr<PacketSocketClient> client = CreateObject<PacketSocketClient>();
    client->SetRemote(socketAddr);
    WifiApNodes.Get(1)->AddApplication(client);
    client->SetAttribute("PacketSize", UintegerValue(payloadSize));
    client->SetAttribute("MaxPackets", UintegerValue(number_packets));
    client->SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
    client->SetStartTime(Seconds(1.0));
    client->SetStopTime(Seconds(simulationTime+1));

    
    Ptr<PacketSocketServer> server = CreateObject<PacketSocketServer>();
    server->SetLocal(socketAddr);
    wifiStaNodes.Get(1)->AddApplication(server);
    server->SetStartTime(Seconds(0.0));
    server->SetStopTime(Seconds(simulationTime+1));
}


for (uint32_t i = 0; i < WifiApNodes.GetN(); ++i) 
{
     std::string path = "/NodeList/" + std::to_string(i) + "/ApplicationList/*/$ns3::PacketSocketServer/Rx";
    Config::Connect(path, MakeCallback(&SocketRx));
} 






// Install FlowMonitor 
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

if (enablePcap)
{
    spectrumPhy.EnablePcap("OBSS_A", apDeviceA);
    spectrumPhy.EnablePcap("OBSS_B", apDeviceB);
}



 
Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);
Simulator::Stop(Seconds(simulationTime + 1));
Simulator::Run();

Simulator::Destroy();


for (uint32_t i = 0; i < nBSSs; i++)
{
    const auto throughput = bytesReceived[i] * 8.0 / 1000 / 1000 / simulationTime;
    const auto packets = packetsReceived[i];
    std::cout << "Number of Rxed packets for BSS" <<  i + 1 << ": " << packets << " Throughput " <<  throughput << " Mbit/s" << std::endl;
}

return 0;
}