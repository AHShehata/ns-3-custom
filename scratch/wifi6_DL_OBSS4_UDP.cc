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


uint32_t pkt_Size = 700; ///< packet size used for the simulation (in bytes)
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

int nBSSs = 4;
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
double simulationTime{10}; // seconds


std::string dlAckSeqType{"NO-OFDMA"};  // 
Time accessReqInterval{0};   // Time is a class used in ns3 to represent the time durations 
std::string phyModel{"Spectrum"};  // std::string is a string tye in c++, it uses the brace initializer to set the phyModel to the string yans so phyModel is a variable of type strind and initiated to yans
bool verbose{false};
bool enablePcap{true};
uint32_t nBSSs = 4;
uint32_t channelWidth = 20;
uint32_t gi = 800;
uint32_t number_packets = 100;
int mcs_OBSS = 0;
double traffic_load_BBS1{1.0};  // Factor determin the chanel congestion on the band of different BSSs
double traffic_load_BBS2{1.0};  // Factor determin the chanel congestion on the band of different BSSs
double traffic_load_BBS3{1.0};  // Factor determin the chanel congestion on the band of different BSSs
double traffic_load_BBS4{1.0};  // Factor determin the chanel congestion on the band of different BSSs

/*
double d1 = 30.0;            // meters
double d2 = 30.0;            // meters
double d3 = 100.0;           // meters
double d4 = 100.0;           //meters
double d5 = 30.0;            // meters
double d6 = 100.0;
double d7 = 30.0;
*/
double dSta = 20; //meters
double dAP = 100;  //meters


bool enableObssPd = false;
double obssPdThreshold = -72.0; // dBm

double powSta1 = 10.0;       // dBm
double powSta2 = 10.0;       // dBm
double powSta3 = 10.0;       // dBm
double powSta4 = 10.0;       // dBm

double powAp1 = 16.02;        // dBm
double powAp2 = 16.02;        // dBm
double powAp3 = 16.02;        // dBm
double powAp4 = 16.02;        // dBm

double ccaEdTrSta1 = -62;    // dBm
double ccaEdTrSta2 = -62;    // dBm
double ccaEdTrSta3 = -62;    // dBm
double ccaEdTrSta4 = -62;    // dBm

double ccaEdTrAp1 = -62;     // dBm
double ccaEdTrAp2 = -62;     // dBm
double ccaEdTrAp3 = -62;     // dBm
double ccaEdTrAp4 = -62;     // dBm

double minimumRssi = -82;    // dBm
double rxSensitivity = -101;  // dBm (-92)

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
cmd.AddValue("traffic_load_BBS1", "OBSS 1 congestion level", traffic_load_BBS1);
cmd.AddValue("traffic_load_BBS2", "OBSS 2 congestion level", traffic_load_BBS2);
cmd.AddValue("traffic_load_BBS3", "OBSS 3 congestion level", traffic_load_BBS3);
cmd.AddValue("traffic_load_BBS4", "OBSS 3 congestion level", traffic_load_BBS4);

cmd.AddValue("mcs_OBSS", "Used mcs for OBSS", mcs_OBSS);
/*
cmd.AddValue("d1", "Distance between STA1 and AP1 (m)", d1);
cmd.AddValue("d2", "Distance between STA2 and AP2 (m)", d2);
cmd.AddValue("d3", "Distance between AP1 and AP2 (m)", d3);
*/
cmd.AddValue("dAP", "Distance between access points (m)", dAP);
cmd.AddValue("dSta", "Distance between AP and STA (m)", dSta);
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
NodeContainer wifiStaNodesA;
wifiStaNodesA.Create(nStations);
NodeContainer wifiStaNodesB;
wifiStaNodesB.Create(nStations);
NodeContainer wifiStaNodesC;
wifiStaNodesC.Create(nStations);
NodeContainer wifiStaNodesD;
wifiStaNodesD.Create(nStations);
NodeContainer WifiApNodes;
WifiApNodes.Create(nBSSs);


WifiHelper wifi;
WifiMacHelper mac;


 if (enableObssPd)
{
    wifi.SetObssPdAlgorithm("ns3::ConstantObssPdAlgorithm",
                            "ObssPdLevel",
                            DoubleValue(obssPdThreshold));
}
wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));


SpectrumWifiPhyHelper spectrumPhy;
spectrumPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);


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


/*Configuration of each AP and its stations*/ 
// BSSA
std::string channelStrA("{0, " + std::to_string(channelWidth) + ", ");
StringValue ctrlRateA;
auto nonHtRefRateMbpsA = HePhy::GetNonHtReferenceRate(mcs) / 1e6;
std::ostringstream ossDataModeA;
ossDataModeA << "HeMcs" << mcs;


if (frequency == 6)
{
    // Sets the Wi-Fi standard to 802.11ax. //Appends BAND_6GHZ to channelStr. //Sets ctrlRate using the value from ossDataMode.str().
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    channelStrA += "BAND_6GHZ, 0}";
    ctrlRateA = StringValue(ossDataModeA.str());
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(48));

}
else if (frequency == 5)
{   
    //HE used the default propoagation loss model in WIFI_STANDARD_80211ax which is LogDistancePropagationLossModel by the way
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeA;
    ossControlModeA << "OfdmRate" << nonHtRefRateMbpsA << "Mbps";
    ctrlRateA = StringValue(ossControlModeA.str());
    channelStrA += "BAND_5GHZ, 0}";
}
else if (frequency == 2.4)
{
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeA;
    ossControlModeA << "ErpOfdmRate" << nonHtRefRateMbpsA << "Mbps";
    ctrlRateA = StringValue(ossControlModeA.str());
    channelStrA += "BAND_2_4GHZ, 0}";
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",  DoubleValue(40));
}
else 
{
// NS_FATAL_ERROR is different than the NS_ABort_msg thaat besides aborting the simulation immediately it includes the message, the line file and the line information
    NS_FATAL_ERROR("Wrong frequency value!");
}

wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(ossDataModeA.str()),
                               "ControlMode", ctrlRateA);
spectrumPhy.Set("ChannelSettings", StringValue(channelStrA));

spectrumPhy.Set("TxPowerStart", DoubleValue(powSta1));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powSta1));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta1));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
Ssid ssidA = Ssid("A");           
mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidA), 
            "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevicesA  = wifi.Install(spectrumPhy, mac, wifiStaNodesA);


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
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidA),
            "EnableBeaconJitter",  BooleanValue(false));
NetDeviceContainer apDeviceA = wifi.Install(spectrumPhy, mac, WifiApNodes.Get(0));
Ptr<WifiNetDevice> ap1Device = apDeviceA.Get(0)->GetObject<WifiNetDevice>();
if (enableObssPd)
{
    ap1Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(1));
}


// BSS B
std::string channelStrB("{0, " + std::to_string(channelWidth) + ", ");
StringValue ctrlRateB;
auto nonHtRefRateMbpsB = HePhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
std::ostringstream ossDataModeB;
ossDataModeB << "HeMcs" << mcs_OBSS;


if (frequency == 6)
{
    // Sets the Wi-Fi standard to 802.11ax. //Appends BAND_6GHZ to channelStr. //Sets ctrlRate using the value from ossDataMode.str().
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    channelStrB += "BAND_6GHZ, 0}";
    ctrlRateB = StringValue(ossDataModeB.str());
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(48));

}
else if (frequency == 5)
{   
    //HE used the default propoagation loss model in WIFI_STANDARD_80211ax which is LogDistancePropagationLossModel by the way
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeB;
    ossControlModeB << "OfdmRate" << nonHtRefRateMbpsB << "Mbps";
    ctrlRateB = StringValue(ossControlModeB.str());
    channelStrB += "BAND_5GHZ, 0}";
}
else if (frequency == 2.4)
{
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeB;
    ossControlModeB << "ErpOfdmRate" << nonHtRefRateMbpsB << "Mbps";
    ctrlRateB = StringValue(ossControlModeB.str());
    channelStrB += "BAND_2_4GHZ, 0}";
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",  DoubleValue(40));
}
else 
{
// NS_FATAL_ERROR is different than the NS_ABort_msg thaat besides aborting the simulation immediately it includes the message, the line file and the line information
    NS_FATAL_ERROR("Wrong frequency value!");
}

wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(ossDataModeB.str()),
                               "ControlMode", ctrlRateB);
spectrumPhy.Set("ChannelSettings", StringValue(channelStrB));


spectrumPhy.Set("TxPowerStart", DoubleValue(powSta2));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powSta2));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta2));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
Ssid ssidB = Ssid("B");
mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidB), 
            "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevicesB = wifi.Install(spectrumPhy, mac, wifiStaNodesB);

spectrumPhy.Set("TxPowerStart", DoubleValue(powAp2));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powAp2));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp2));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));

mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidB),
            "EnableBeaconJitter",  BooleanValue(false));
NetDeviceContainer apDeviceB = wifi.Install(spectrumPhy, mac, WifiApNodes.Get(1));

Ptr<WifiNetDevice> ap2Device = apDeviceB.Get(0)->GetObject<WifiNetDevice>();
if (enableObssPd)
{
    ap2Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(2));
}


// BSS C
std::string channelStrC("{0, " + std::to_string(channelWidth) + ", ");
StringValue ctrlRateC;
auto nonHtRefRateMbpsC = HePhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
std::ostringstream ossDataModeC;
ossDataModeC << "HeMcs" << mcs_OBSS;


if (frequency == 6)
{
    // Sets the Wi-Fi standard to 802.11ax. //Appends BAND_6GHZ to channelStr. //Sets ctrlRate using the value from ossDataMode.str().
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    channelStrC += "BAND_6GHZ, 0}";
    ctrlRateC = StringValue(ossDataModeC.str());
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(48));

}
else if (frequency == 5)
{   
    //HE used the default propoagation loss model in WIFI_STANDARD_80211ax which is LogDistancePropagationLossModel by the way
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeC;
    ossControlModeC << "OfdmRate" << nonHtRefRateMbpsC << "Mbps";
    ctrlRateC = StringValue(ossControlModeC.str());
    channelStrC += "BAND_5GHZ, 0}";
}
else if (frequency == 2.4)
{
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeC;
    ossControlModeC << "ErpOfdmRate" << nonHtRefRateMbpsC << "Mbps";
    ctrlRateC = StringValue(ossControlModeC.str());
    channelStrC += "BAND_2_4GHZ, 0}";
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",  DoubleValue(40));
}
else 
{
// NS_FATAL_ERROR is different than the NS_ABort_msg thaat besides aborting the simulation immediately it includes the message, the line file and the line information
    NS_FATAL_ERROR("Wrong frequency value!");
}

wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(ossDataModeC.str()),
                               "ControlMode", ctrlRateC);
spectrumPhy.Set("ChannelSettings", StringValue(channelStrC));


spectrumPhy.Set("TxPowerStart", DoubleValue(powSta3));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powSta3));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta3));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
Ssid ssidC = Ssid("C");
mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidC), 
            "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevicesC = wifi.Install(spectrumPhy, mac, wifiStaNodesC);

spectrumPhy.Set("TxPowerStart", DoubleValue(powAp3));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powAp3));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp3));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));

mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidC),
            "EnableBeaconJitter",  BooleanValue(false));
NetDeviceContainer apDeviceC = wifi.Install(spectrumPhy, mac, WifiApNodes.Get(2));
Ptr<WifiNetDevice> ap3Device = apDeviceC.Get(0)->GetObject<WifiNetDevice>();
if (enableObssPd)
{
    ap3Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(3));
}


// BSS D
std::string channelStrD("{0, " + std::to_string(channelWidth) + ", ");
StringValue ctrlRateD;
auto nonHtRefRateMbpsD = HePhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
std::ostringstream ossDataModeD;
ossDataModeD << "HeMcs" << mcs_OBSS;


if (frequency == 6)
{
    // Sets the Wi-Fi standard to 802.11ax. //Appends BAND_6GHZ to channelStr. //Sets ctrlRate using the value from ossDataMode.str().
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    channelStrD += "BAND_6GHZ, 0}";
    ctrlRateD = StringValue(ossDataModeD.str());
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(48));

}
else if (frequency == 5)
{   
    //HE used the default propoagation loss model in WIFI_STANDARD_80211ax which is LogDistancePropagationLossModel by the way
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeD;
    ossControlModeD << "OfdmRate" << nonHtRefRateMbpsD << "Mbps";
    ctrlRateD = StringValue(ossControlModeD.str());
    channelStrD += "BAND_5GHZ, 0}";
}
else if (frequency == 2.4)
{
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    std::ostringstream ossControlModeD;
    ossControlModeD << "ErpOfdmRate" << nonHtRefRateMbpsD << "Mbps";
    ctrlRateD = StringValue(ossControlModeD.str());
    channelStrD += "BAND_2_4GHZ, 0}";
    //Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss",  DoubleValue(40));
}
else 
{
// NS_FATAL_ERROR is different than the NS_ABort_msg thaat besides aborting the simulation immediately it includes the message, the line file and the line information
    NS_FATAL_ERROR("Wrong frequency value!");
}

wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                               "DataMode", StringValue(ossDataModeD.str()),
                               "ControlMode", ctrlRateD);
spectrumPhy.Set("ChannelSettings", StringValue(channelStrD));


spectrumPhy.Set("TxPowerStart", DoubleValue(powSta4));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powSta4));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta4));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
Ssid ssidD = Ssid("D");
mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidD), 
            "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
NetDeviceContainer staDevicesD = wifi.Install(spectrumPhy, mac, wifiStaNodesD);

spectrumPhy.Set("TxPowerStart", DoubleValue(powAp4));
spectrumPhy.Set("TxPowerEnd", DoubleValue(powAp4));
spectrumPhy.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp4));
spectrumPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));

mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssidD),
            "EnableBeaconJitter",  BooleanValue(false));
NetDeviceContainer apDeviceD = wifi.Install(spectrumPhy, mac, WifiApNodes.Get(3));
Ptr<WifiNetDevice> ap4Device = apDeviceC.Get(0)->GetObject<WifiNetDevice>();
if (enableObssPd)
{
    ap4Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(4));
}



// Mobility model
MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
/*
positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // AP1
positionAlloc->Add(Vector(d3, 0.0, 0.0));  // AP2
positionAlloc->Add(Vector(-d4, 0.0, 0.0));  // AP3
positionAlloc->Add(Vector(0.0, -d6, 0.0));  // AP4


positionAlloc->Add(Vector(0.0, d1, 0.0));  // STA1
positionAlloc->Add(Vector(d3, d2, 0.0));   // STA2
positionAlloc->Add(Vector(-d4, d5, 0.0));   // STA3
positionAlloc->Add(Vector(-d7, -d6, 0.0));   // STA4
*/

/*Rectangular grid*/
positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // AP1
positionAlloc->Add(Vector(dAP, 0.0, 0.0));  // AP2
positionAlloc->Add(Vector(0.0, dAP, 0.0));  // AP3
positionAlloc->Add(Vector(dAP, dAP, 0.0)); // AP4

positionAlloc->Add(Vector(dSta, 0.0, 0.0)); // STA1
positionAlloc->Add(Vector(dAP+dSta,0.0, 0.0));  // STA2
positionAlloc->Add(Vector(dSta, dAP, 0.0));  // STA3
positionAlloc->Add(Vector(dAP+dSta, dAP, 0.0)); // STA4


mobility.SetPositionAllocator(positionAlloc);
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.Install(WifiApNodes);
mobility.Install(wifiStaNodesA);
mobility.Install(wifiStaNodesB);
mobility.Install(wifiStaNodesC);
mobility.Install(wifiStaNodesD);


/*Internet Stack*/
int64_t streamNumber = 150;
streamNumber += wifi.AssignStreams(apDeviceA, streamNumber);
streamNumber += wifi.AssignStreams(apDeviceB, streamNumber);
streamNumber += wifi.AssignStreams(apDeviceC, streamNumber);
streamNumber += wifi.AssignStreams(apDeviceD, streamNumber);
streamNumber += wifi.AssignStreams(staDevicesA, streamNumber);
streamNumber += wifi.AssignStreams(staDevicesB, streamNumber);
streamNumber += wifi.AssignStreams(staDevicesC, streamNumber);
streamNumber += wifi.AssignStreams(staDevicesD, streamNumber);


InternetStackHelper stack;
stack.Install(WifiApNodes);
stack.Install(wifiStaNodesA);
stack.Install(wifiStaNodesB);
stack.Install(wifiStaNodesC);
stack.Install(wifiStaNodesD);
streamNumber += stack.AssignStreams(WifiApNodes, streamNumber);
streamNumber += stack.AssignStreams(wifiStaNodesA, streamNumber);
streamNumber += stack.AssignStreams(wifiStaNodesB, streamNumber);
streamNumber += stack.AssignStreams(wifiStaNodesC, streamNumber);
streamNumber += stack.AssignStreams(wifiStaNodesD, streamNumber);

Ipv4AddressHelper address;
address.SetBase("192.168.1.0", "255.255.255.0");
Ipv4InterfaceContainer staNodeInterfacesA;
Ipv4InterfaceContainer staNodeInterfacesB;
Ipv4InterfaceContainer staNodeInterfacesC;
Ipv4InterfaceContainer staNodeInterfacesD;
Ipv4InterfaceContainer apNodeInterfaceA;
Ipv4InterfaceContainer apNodeInterfaceB;
Ipv4InterfaceContainer apNodeInterfaceC;
Ipv4InterfaceContainer apNodeInterfaceD;


staNodeInterfacesA = address.Assign(staDevicesA);
staNodeInterfacesB = address.Assign(staDevicesB);
staNodeInterfacesC = address.Assign(staDevicesC);
staNodeInterfacesD = address.Assign(staDevicesD);

apNodeInterfaceA = address.Assign(apDeviceA);
apNodeInterfaceB = address.Assign(apDeviceB);
apNodeInterfaceC = address.Assign(apDeviceC);
apNodeInterfaceD = address.Assign(apDeviceD);


/*Application*/

/* Setting applications */
// BSS1
std::vector<ApplicationContainer> serverSet;
{
const auto maxRate = HePhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
std::cout << "maxRate " << maxRate/1000000 << std::endl;

ApplicationContainer serverAppA;
Ipv4InterfaceContainer serverInterfaces;
auto serverNodes =  std::ref(wifiStaNodesA);
for (std::size_t i = 0; i < nStations; i++)
{
    serverInterfaces.Add(staNodeInterfacesA.Get(i));
}
uint16_t port = 9;
UdpServerHelper server(port);
serverAppA = server.Install(serverNodes.get());         
streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

serverAppA.Start(Seconds(0.0));
serverAppA.Stop(Seconds(simulationTime + 1));
serverSet.push_back(serverAppA);

NodeContainer clientNodes;
clientNodes.Add(WifiApNodes.Get(0));
double  maxRate_lower_BSS1 = maxRate * traffic_load_BBS1;
const auto packetInterval1 = payloadSize * 8.0 / maxRate_lower_BSS1;  // For calculating the duration of the packet
//4294967295U
//number_packets = 5000 * 0.1;

// UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
for (std::size_t i = 0; i < nStations; i++)
{   
    // Ip address and the port of the receiver 
    UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
    client.SetAttribute("MaxPackets", UintegerValue(4294967295U));
    client.SetAttribute("Interval", TimeValue(Seconds(packetInterval1)));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    ApplicationContainer clientAppA = client.Install(clientNodes.Get(0));
    streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

    clientAppA.Start(Seconds(1.0));
    clientAppA.Stop(Seconds(simulationTime + 1));
}
}

// BSS2
{
const auto maxRate_OBSS = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStations;

ApplicationContainer serverAppB;
Ipv4InterfaceContainer serverInterfaces;
auto serverNodes =  std::ref(wifiStaNodesB);
for (std::size_t i = 0; i < nStations; i++)
{
    serverInterfaces.Add(staNodeInterfacesB.Get(i));
}
uint16_t port = 9;
UdpServerHelper server(port);
serverAppB = server.Install(serverNodes.get());         
streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

serverAppB.Start(Seconds(0.0));
serverAppB.Stop(Seconds(simulationTime + 1));
serverSet.push_back(serverAppB);

NodeContainer clientNodes;
clientNodes.Add(WifiApNodes.Get(1));
double  maxRate_lower_BSS2 = maxRate_OBSS * traffic_load_BBS2;
const auto packetInterval2 = payloadSize * 8.0 / maxRate_lower_BSS2;  // For calculating the duration of the packet
//4294967295U
//number_packets = 5000 * 0.1;

// UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
for (std::size_t i = 0; i < nStations; i++)
{   
    // Ip address and the port of the receiver 
    UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
    client.SetAttribute("MaxPackets", UintegerValue(4294967295U));
    client.SetAttribute("Interval", TimeValue(Seconds(packetInterval2)));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    ApplicationContainer clientAppB = client.Install(clientNodes.Get(0));
    streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

    clientAppB.Start(Seconds(1.0));
    clientAppB.Stop(Seconds(simulationTime + 1));
}
}

// BSS3
{
const auto maxRate_OBSS3 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStations;

ApplicationContainer serverAppC;
Ipv4InterfaceContainer serverInterfaces;
auto serverNodes =  std::ref(wifiStaNodesC);
for (std::size_t i = 0; i < nStations; i++)
{
    serverInterfaces.Add(staNodeInterfacesC.Get(i));
}
uint16_t port = 9;
UdpServerHelper server(port);
serverAppC = server.Install(serverNodes.get());         
streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

serverAppC.Start(Seconds(0.0));
serverAppC.Stop(Seconds(simulationTime + 1));
serverSet.push_back(serverAppC);

NodeContainer clientNodes;
clientNodes.Add(WifiApNodes.Get(2));
double  maxRate_lower_BSS3 = maxRate_OBSS3 * traffic_load_BBS3;
const auto packetInterval3 = payloadSize * 8.0 / maxRate_lower_BSS3;  // For calculating the duration of the packet
//4294967295U
//number_packets = 5000 * 0.1;

// UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
for (std::size_t i = 0; i < nStations; i++)
{   
    // Ip address and the port of the receiver 
    UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
    client.SetAttribute("MaxPackets", UintegerValue(4294967295U));
    client.SetAttribute("Interval", TimeValue(Seconds(packetInterval3)));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    ApplicationContainer clientAppC = client.Install(clientNodes.Get(0));
    streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

    clientAppC.Start(Seconds(1.0));
    clientAppC.Stop(Seconds(simulationTime + 1));
}
}
// BSS4
{
const auto maxRate_OBSS4 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStations;

ApplicationContainer serverAppD;
Ipv4InterfaceContainer serverInterfaces;
auto serverNodes =  std::ref(wifiStaNodesD);

for (std::size_t i = 0; i < nStations; i++)
{    
    serverInterfaces.Add(staNodeInterfacesD.Get(i));
}

uint16_t port = 9;
UdpServerHelper server(port);
serverAppD = server.Install(serverNodes.get());         
streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

serverAppD.Start(Seconds(0.0));
serverAppD.Stop(Seconds(simulationTime + 1));
serverSet.push_back(serverAppD);

NodeContainer clientNodes;
clientNodes.Add(WifiApNodes.Get(3));
double  maxRate_lower_BSS4 = maxRate_OBSS4 * traffic_load_BBS4;
const auto packetInterval4 = payloadSize * 8.0 / maxRate_lower_BSS4;  // For calculating the duration of the packet
//4294967295U
//number_packets = 5000 * 0.1;

// UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
for (std::size_t i = 0; i < nStations; i++)
{   
    // Ip address and the port of the receiver 
    UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
    client.SetAttribute("MaxPackets", UintegerValue(4294967295U));
    client.SetAttribute("Interval", TimeValue(Seconds(packetInterval4)));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    ApplicationContainer clientAppD = client.Install(clientNodes.Get(0));
    streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

    clientAppD.Start(Seconds(1.0));
    clientAppD.Stop(Seconds(simulationTime + 1));
}
}

// Install FlowMonitor 
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

if (enablePcap)
{
    spectrumPhy.EnablePcap("OBSS_A", apDeviceA);
    spectrumPhy.EnablePcap("OBSS_B", apDeviceB);
    spectrumPhy.EnablePcap("OBSS_C", apDeviceC);
}

 
Simulator::Schedule(Seconds(0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);
Simulator::Stop(Seconds(simulationTime + 1));
Simulator::Run();


/*Calculating throughput*/
uint64_t totalPacketsThrough = 0;
std::vector<double> Sta_Throughput(nStations);

for (uint32_t j = 0; j <  serverSet.size(); j++)
{ 
  auto server =  serverSet[j];
  for (uint32_t i = 0; i < server.GetN(); i++)
  {
     totalPacketsThrough =  DynamicCast<UdpServer>(server.Get(i))->GetReceived();
     std::cout << "BSS " << j+1 << ": Received packets by sta " << i+1 << ": " << totalPacketsThrough << std::endl;
     Sta_Throughput[i] = totalPacketsThrough * payloadSize * 8 / (simulationTime * 1000000.0); // Mbit/s
  }

   std::cout << "Throughput of stations" << " ";
   for (uint32_t count = 0; count <  server.GetN(); count++) 
   {
      std::cout <<  Sta_Throughput[count]  << ", " << " ";
   }
   std::cout << std::endl;   
}

             
                
// Print per flow statistics
monitor->CheckForLostPackets ();
Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();


double through = 0;
double delay = 0;
double txed_packets = 0;
double rxed_packets = 0;
double lost_packets = 0;
double Time_between_first_last_packets = 0;
std::cout << std::setw(5) << "Delay(s) " << std::setw(15) << "txed packets " << std::setw(13) << "rxed packets "
              << std::setw(12) << "lost packets " << std::setw(10) << "Throughput " << std::endl;
for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
{   
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
    //std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
    //std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
    through = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024;
    Time_between_first_last_packets = (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ());
    //std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024  << " Mbps\n";
    std::cout << "Time_between_first_last_packets " << Time_between_first_last_packets << "\n";
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