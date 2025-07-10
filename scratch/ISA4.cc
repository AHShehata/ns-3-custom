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
#include "ns3/ap-wifi-mac.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/wifi-mac.h"
#include "ns3/eht-phy.h"
#include <iomanip>
#include "ns3/attribute-container.h"
#include <iostream>
#include <sstream>
#include <set>
#include <cstdint>  // For uint8_t



// This is an example of  how to configure an IEEE 802.11be Wi-Fi network.
// The simulation assumes a configurable number of stations in an infrastructure network:
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

/**
 * \param serverApp a container of server applications
 * \param payloadSize the size in bytes of the packets
 * \return the bytes received by each server application
 */
std::vector<uint64_t>
GetRxBytes(const ApplicationContainer& serverApp, uint32_t payloadSize)
{
    std::vector<uint64_t> rxBytes(serverApp.GetN(), 0);
    for (uint32_t i = 0; i < serverApp.GetN(); i++)
    {
        rxBytes[i] = payloadSize * DynamicCast<UdpServer>(serverApp.Get(i))->GetReceived();
    }
    return rxBytes;
}

/**
 * Print average throughput over an intermediate time interval.
 * \param rxBytes a vector of the amount of bytes received by each server application
 * \param serverApp a container of server applications
 * \param payloadSize the size in bytes of the packets
 * \param tputInterval the duration of an intermediate time interval
 * \param simulationTime the simulation time in seconds
 */
void
PrintIntermediateTput(std::vector<uint64_t>& rxBytes,
                      const ApplicationContainer& serverApp,
                      uint32_t payloadSize,
                      Time tputInterval,
                      Time simulationTime)
{
    auto newRxBytes = GetRxBytes(serverApp, payloadSize);
    Time now = Simulator::Now();
 
    std::cout << "[" << (now - tputInterval).As(Time::S) << " - " << now.As(Time::S)
              << "] Per-STA Throughput (Mbit/s):";
 
    for (std::size_t i = 0; i < newRxBytes.size(); i++)
    {
        std::cout << "\t\t(" << i << ") "
                  << (newRxBytes[i] - rxBytes[i]) * 8. / tputInterval.GetMicroSeconds(); // Mbit/s
    }
    std::cout << std::endl;
 
    rxBytes.swap(newRxBytes);
 
    if (now < (simulationTime - NanoSeconds(1)))
    {
        Simulator::Schedule(Min(tputInterval, simulationTime - now - NanoSeconds(1)),
                            &PrintIntermediateTput,
                            rxBytes,
                            serverApp,
                            payloadSize,
                            tputInterval,
                            simulationTime);
    }
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

int Tx_udp_packets = 0;
std::map<Ipv4Address, std::map<uint64_t, Time>> sendTimestamps;  // Address -> (PacketID -> Send Time)
void
ClientTxAdd(std::string context, Ptr<const Packet> p, const Address &srcAddress, const Address &destAddress)
{   
    //Ipv4Address srcAddress_corr = ContextToIp(context);
    //std::cout << InetSocketAddress::ConvertFrom(srcAddress_corr).GetIpv4() << std::endl;
    //std::cout << InetSocketAddress::ConvertFrom(destAddress).GetIpv4() << std::endl;
    Ipv4Address destAddressIp = InetSocketAddress::ConvertFrom(destAddress).GetIpv4();
    uint64_t packetId = p->GetUid();
    Time sendTime = Simulator::Now();
    sendTimestamps[destAddressIp][packetId] = sendTime;
    Tx_udp_packets++;
}

uint32_t targetPacketCount = 50000;
uint32_t Rx_udp_packets = 0;
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

    if (Rx_udp_packets >= targetPacketCount)
    {
        NS_LOG_INFO("Target packet count reached! Stopping simulation.");
        Simulator::Stop(); // Stop the simulation
    }
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

// Parameters 
bool udp{true}; // Application used either TCP/UDP
bool downlink{true}; // Downlink/ UL
bool useRts{true}; //Use RTS/CTS or not
Time simulationTime{"10s"}; //seconds
double distance{10.0};      // meters
std::size_t nStations{1};
double frequency{2.4};       // whether the first link operates in the 2.4, 5 or 6 GHz
double frequency2{0}; // whether the second link operates in the 2.4, 5 or 6 GHz (0 means no second link exists)
double frequency3{0}; // whether the third link operates in the 2.4, 5 or 6 GHz (0 means no third link exists)
int mcs{4}; // -1 indicates an unset value
uint16_t mpduBufferSize{64};
Time accessReqInterval{"1s"};
std::string dlAckSeqType{"NO-OFDMA"};
bool enableUlOfdma{false};
bool enableBsrp{false};
uint32_t number_packets = 1000000;
uint16_t channelWidth = 20;
uint16_t gi = 800;
uint32_t payloadSize = 700;

bool verbose{false};
bool tracing{false};
bool enablePcap{true};
Time tputInterval{0}; // interval for detailed throughput measurement
int nLinks = 2;

/* EMLSR Parameters */ 
//std::string emlsrLinks;  
//std::string emlsrLinks = "0";
//std::set<uint8_t> emlsrLinks={0, 1, 2};
std::string emlsrLinks = "0 1";
//std::string emlsrLinks = "0, 1, 2";



//std::set<uint8_t> emlsrLinks = {"0", "1", "2"};    //Not working sedfined as a set of strings
uint16_t paddingDelayUsec{32};
uint16_t transitionDelayUsec{128};
uint16_t channelSwitchDelayUsec{100};
bool switchAuxPhy{true};
bool auxPhyTxCapable{true};
uint16_t auxPhyChWidth{20};


// Parsing using the commandline 
CommandLine cmd(__FILE__);
cmd.AddValue("number_packets",
             "Max number of transmitted packets",
              number_packets);
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
cmd.AddValue("mcs", "if set, limit testing to a specific MCS (0-11)", mcs);
cmd.AddValue("payloadSize", "The application payload size in bytes", payloadSize);
cmd.AddValue("tputInterval", "duration of intervals for throughput measurement", tputInterval);
cmd.AddValue("gi", "Guard interval",  gi);
cmd.AddValue("tracing", "Generate trace files", tracing);
cmd.AddValue("channelWidth", "channelWidth used",  channelWidth);

cmd.Parse(argc, argv);

if (verbose)
{
    LogComponentEnableAll(LOG_PREFIX_ALL);
    WifiHelper::EnableLogComponents(LOG_ALL); // Turn on all Wifi logging
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


/* Create the station and AP nodes */ 
NodeContainer wifiStaNodes;
wifiStaNodes.Create(nStations);
NodeContainer wifiApNode;
wifiApNode.Create(1);

NetDeviceContainer apDevice;
NetDeviceContainer staDevices;
WifiHelper wifi;
WifiMacHelper mac;


wifi.SetStandard(WIFI_STANDARD_80211be);
wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));

                
if (nLinks > 1)
{
    std::cout << "EMLSR option is activated "<< emlsrLinks.empty() << std::endl;
    wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
}
Ssid ssid = Ssid("ns3-80211be");


SpectrumWifiPhyHelper spectrumWifiPhy(nLinks);
spectrumWifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
spectrumWifiPhy.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));


Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                        DoubleValue(2.4e9));
spectrumWifiPhy.SetErrorRateModel ("ns3::TableBasedErrorRateModel");


// Link 1
std::string channelStr1 =  "{1, " + std::to_string(channelWidth) + ", " + "BAND_2_4GHZ, 0}";
FrequencyRange freqRanges1 = WIFI_SPECTRUM_2_4_GHZ;
uint64_t nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs) / 1e6;
std::string dataModeStr = "EhtMcs" + std::to_string(mcs);
std::string ctrlRateStr = "ErpOfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
uint8_t id_link1 = 0;
wifi.SetRemoteStationManager(id_link1,"ns3::ConstantRateWifiManager",
                            "DataMode", StringValue(dataModeStr),
                            "ControlMode", StringValue(ctrlRateStr));

spectrumWifiPhy.Set(0, "ChannelSettings", StringValue(channelStr1));
Ptr<MultiModelSpectrumChannel> spectrumChannel1 = CreateObject<MultiModelSpectrumChannel>();
Ptr<FriisPropagationLossModel> lossModel1 = CreateObject<FriisPropagationLossModel>();
spectrumChannel1->AddPropagationLossModel(lossModel1);
Ptr<ConstantSpeedPropagationDelayModel> delayModel1 = CreateObject<ConstantSpeedPropagationDelayModel>();
spectrumChannel1->SetPropagationDelayModel(delayModel1);
spectrumWifiPhy.AddChannel(spectrumChannel1, freqRanges1);

//spectrumWifiPhy.Set("Antennas", UintegerValue(2)); // At least 2 antennas for 2 RF chains
//spectrumWifiPhy.Set("MaxSupportedRxSpatialStreams", UintegerValue(2)); // Support 2 Rx streams
//spectrumWifiPhy.Set("MaxSupportedTxSpatialStreams", UintegerValue(2)); // Support 2 Tx streams


if (nLinks > 1)
{
    std::string channelStr2 =  "{6, " + std::to_string(channelWidth) + ", " + "BAND_2_4GHZ, 0}";
    FrequencyRange freqRanges2 = WIFI_SPECTRUM_2_4_GHZ;
    uint64_t nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(mcs) / 1e6;
    std::string dataModeStr = "EhtMcs" + std::to_string(mcs);
    std::string ctrlRateStr = "ErpOfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
    uint8_t id_link2 = 1;
    wifi.SetRemoteStationManager(id_link2,
                                "ns3::ConstantRateWifiManager",
                                "DataMode", StringValue(dataModeStr),
                                "ControlMode", StringValue(ctrlRateStr));
    spectrumWifiPhy.Set(1, "ChannelSettings", StringValue(channelStr2));

    Ptr<MultiModelSpectrumChannel> spectrumChannel2 = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel2 = CreateObject<FriisPropagationLossModel>();
    spectrumChannel2->AddPropagationLossModel(lossModel2);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel2 = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel2->SetPropagationDelayModel(delayModel2);
    spectrumWifiPhy.AddChannel(spectrumChannel2, freqRanges2);
}




mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
mac.SetEmlsrManager("ns3::DefaultEmlsrManager",
                   "EmlsrLinkSet",
                    //StringValue(emlsrLinks),
                    AttributeContainerValue<UintegerValue>(emlsrLinks),
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
staDevices = wifi.Install(spectrumWifiPhy, mac, wifiStaNodes);


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
            "EnableBeaconJitter", BooleanValue(false),
            "Ssid", SsidValue(ssid));
apDevice = wifi.Install(spectrumWifiPhy, mac, wifiApNode);

//Config::Set( "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
//            TimeValue(NanoSeconds(gi)));
Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize",
            UintegerValue(mpduBufferSize));
int64_t streamNumber = 100;
streamNumber += wifi.AssignStreams(apDevice, streamNumber);
streamNumber += wifi.AssignStreams(staDevices, streamNumber);


/*Retrieve parameters of AP only now*/ 
Ptr<WifiNetDevice> APdeviceaccessed = DynamicCast<WifiNetDevice>(apDevice.Get(0));
for (uint8_t  i = 0; i < APdeviceaccessed->GetNPhys(); i++)
{
    Ptr<WifiPhy> AP_phy = DynamicCast<WifiPhy>(APdeviceaccessed->GetPhy(i));
    double CCA_threshold  =  AP_phy->GetCcaEdThreshold(); 
    double CCA_sensitivity  =  AP_phy->GetCcaSensitivityThreshold ();
    double Rx_Sensitivity =  AP_phy->GetRxSensitivity ();
    double Num_antennas  =  AP_phy->GetNumberOfAntennas ();
    double PhyId  =  AP_phy->GetPhyId ();
    double Rx_gain = AP_phy->GetRxGain ();
    double Tx_gain = AP_phy->GetTxGain ();
    double TxPowerEnd = AP_phy->GetTxPowerEnd ();
    double TxPowerStart = AP_phy->GetTxPowerStart ();
    UintegerValue  Txstreams;
    UintegerValue  Rxstreams;
    AP_phy->GetAttribute("MaxSupportedTxSpatialStreams", Txstreams);
    AP_phy->GetAttribute("MaxSupportedTxSpatialStreams", Rxstreams);

    std::cout << "PhyId " << PhyId << std::endl;
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
}

// Retrieve the parameters of the MAC layer
Ptr<ApWifiMac> AP_mac = DynamicCast<ApWifiMac>(APdeviceaccessed->GetMac());

uint8_t Num_links = AP_mac->GetNLinks() ;
std::cout << "Number of links " << static_cast<int>(Num_links)  << std::endl;

bool check_QOS_supp = AP_mac->GetQosSupported ();
std::cout << "Is_QOS_Supp " << check_QOS_supp << std::endl;

//When a packet is received by the MAC, to be sent to the PHY, it is queued in the internal queue after being tagged by the current time.
//so it saves the packets and tag it with the current time until 
Ptr< WifiMacQueue > Mac_queue_BE = DynamicCast<WifiMacQueue>(AP_mac->GetTxopQueue (AC_BE));
Ptr< WifiMacQueue > Mac_queue_VO = DynamicCast<WifiMacQueue>(AP_mac->GetTxopQueue (AC_VO));
auto maxSize = Mac_queue_BE->GetMaxSize();
std::cout << "MaxSize_queue : " << maxSize << std::endl;
uint32_t NumPackets_BE = Mac_queue_BE->GetNPackets();
std::cout << "NumPackets_BE " << NumPackets_BE << std::endl;
uint32_t NumPackets_VO = Mac_queue_VO->GetNPackets();
std::cout << "NumPackets_VO " << NumPackets_VO << std::endl;

for (uint8_t  linkId = 0; linkId < Num_links; linkId++)
{   
    std::cout << "For link " << static_cast<unsigned int>(linkId) << std::endl; 
    Ptr<QosTxop> QOS_EDCA = DynamicCast<QosTxop>(AP_mac->GetQosTxop(AC_BE)); 
    Time TXOPLimit = QOS_EDCA->GetTxopLimit(linkId);
    uint32_t MinCW = QOS_EDCA->GetMinCw(linkId);
    uint32_t MaxCW = QOS_EDCA->GetMaxCw(linkId);
    std::cout << "TXOPLimit " << TXOPLimit << std::endl;
    std::cout << "MinCW " << MinCW << std::endl;
    std::cout << "MaxCW " << MaxCW << std::endl;
}



if (nLinks > 1)
{
    Ptr<WifiNetDevice> AP_device_check = DynamicCast<WifiNetDevice>(apDevice.Get(1));
}

for (uint8_t linkId = 0; linkId < AP_mac->GetNLinks(); linkId++)
{    
    Ptr< ChannelAccessManager > CEM = DynamicCast<ChannelAccessManager>(AP_mac->GetChannelAccessManager (linkId));
} 
// Recall the  (MLD) address associated with a Wi-Fi MAC entity that is part of an MLD. 
//The MLD address is a unique MAC address that identifies the entire MLD entity, which may consist of multiple links (interfaces).
Mac48Address mac48Address = Mac48Address::ConvertFrom (APdeviceaccessed->GetAddress());
std::cout << "MLD address : " << mac48Address << std::endl;

for (uint8_t linkId = 0; linkId < AP_mac->GetNLinks(); linkId++)
{    
    Ptr< FrameExchangeManager > FEM = DynamicCast<FrameExchangeManager>(AP_mac->GetFrameExchangeManager (linkId)); 
    std::cout << "Mac address of Link no: " << static_cast<int>(linkId) << " is " << FEM->GetAddress() << std::endl;
} 

for (uint8_t linkId = 0; linkId < AP_mac->GetNLinks(); linkId++)
{    
    Ptr< WifiRemoteStationManager > WSM = 
        DynamicCast<WifiRemoteStationManager>(AP_mac->GetWifiRemoteStationManager (linkId));
} 


/* Mobility */
MobilityHelper mobility;
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
positionAlloc->Add(Vector(0.0, 0.0, 0.0));
positionAlloc->Add(Vector(distance, 0.0, 0.0));
mobility.SetPositionAllocator(positionAlloc);
mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
mobility.Install(wifiApNode);
mobility.Install(wifiStaNodes);


/* Internet stack*/
streamNumber += wifi.AssignStreams(apDevice, streamNumber);
streamNumber += wifi.AssignStreams(staDevices, streamNumber);

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



/* Applications */
const auto maxRate =   EhtPhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
std::cout << "maxRate " << maxRate/1000000 << std::endl;

/*Setting applications*/
ApplicationContainer serverApp;
Ipv4InterfaceContainer serverInterfaces;
auto serverNodes = std::ref(wifiStaNodes); 
for (std::size_t i = 0; i < nStations; i++)
{
    serverInterfaces.Add(staNodeInterfaces.Get(i));
}
 uint16_t port = 9;
UdpServerHelper server(port);
serverApp = server.Install(serverNodes.get());
streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);
serverApp.Start(Seconds(0.0));
serverApp.Stop(simulationTime + Seconds(1.0));


NodeContainer clientNodes;
clientNodes.Add(wifiApNode.Get(0));
//double  maxRate_lower = maxRate * 0.1;
const auto packetInterval = payloadSize * 8.0 / maxRate;  // For calculating the duration of the packet
//4294967295U
//number_packets = 5000 * 1.0;

for (std::size_t i = 0; i < nStations; i++)
{
    UdpClientHelper client(serverInterfaces.GetAddress(i), port);
    client.SetAttribute("MaxPackets", UintegerValue(number_packets));
    client.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
    client.SetAttribute("PacketSize", UintegerValue(payloadSize));
    ApplicationContainer clientApp = client.Install(clientNodes.Get(0));
    streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

    clientApp.Start(Seconds(1.0));
    clientApp.Stop(simulationTime + Seconds(1.0));
}

    

// Install FlowMonitor 
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

if (enablePcap)
{
    spectrumWifiPhy.EnablePcap("wifi_7_DL", apDevice);
}

/*Trace sources*/
Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpClient/TxWithAddresses",
        MakeCallback(&ClientTxAdd));

Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpServer/RxWithAddresses",
    MakeCallback(&ServerRxAdd));






/* Save the current configuration to an XML file
Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.txt"));
Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText"));
Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
ConfigStore outputConfig2;
outputConfig2.ConfigureDefaults();
outputConfig2.ConfigureAttributes();
*/


/* Now for calculating the throughput. The client, AP is the one tranmitting
Usually in one link, you just need to schedule the time of distributing the 
routing tables. which will be as following but unfortunately here you cannot use that with more
than one link connected to the same netdevice 
Simulator::Schedule(Seconds(0), Ipv4GlobalRoutingHelper::PopulateRoutingTables);
*/


std::vector<uint64_t> cumulRxBytes(nStations, 0);
if (tputInterval.IsStrictlyPositive())
{
    Simulator::Schedule(Seconds(1) + tputInterval, &PrintIntermediateTput,
                        cumulRxBytes, serverApp, payloadSize, tputInterval,
                        simulationTime + Seconds(1.0));
}
Simulator::Stop(simulationTime + Seconds(1.0));
Simulator::Run();


/* Caclulate the throughput from the server at the application layer */
uint64_t totalPacketsThrough = 0;
std::vector<double> Sta_Throughput(nStations);

for (uint32_t i = 0; i < serverApp.GetN(); i++)
{
    totalPacketsThrough =  DynamicCast<UdpServer>(serverApp.Get(i))->GetReceived();
    std::cout << "Received packets by sta: " << i+1 << " is " << DynamicCast<UdpServer>(serverApp.Get(i))->GetReceived() << std::endl;
    Sta_Throughput[i] = (totalPacketsThrough * payloadSize * 8) / (simulationTime.GetSeconds() * 1000000.0); // Mbit/s
    std::cout << "check "<< simulationTime.GetSeconds() << std::endl;
}

std::cout << "Throughput of stations are " << " ";
for (uint32_t count = 0; count <  serverApp.GetN(); count++) 
{
    std::cout <<  Sta_Throughput[count]  << ", " << " ";
}
std::cout << std::endl;   
double rxBytes = 0;
for (uint32_t i = 0; i < serverApp.GetN(); i++)
{
    rxBytes +=
          payloadSize * DynamicCast<UdpServer>(serverApp.Get(i))->GetReceived();
}
double tot_throughput = (rxBytes * 8) / (simulationTime.GetSeconds() * 1000000.0); // Mbit/s
std::cout << "Total throughput " <<  tot_throughput  << std::endl;


/* Print per flow statistics */
monitor->CheckForLostPackets ();
Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

double through = 0;
double delay = 0;
double txed_packets = 0;
double rxed_packets = 0;
double lost_packets = 0;
std::cout << std::setw(5) << "Delay(s) " << std::setw(15) << "Txed packets " << std::setw(13) << "Rxed packets "
          << std::setw(12) << "Lost packets " <<  std::setw(10) << "Datarate" << std::setw(15) << "Throughput " 
          << std::endl;
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
    std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
    lost_packets = (i->second.lostPackets);
    //std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";
    std::cout << std::setw(5) << delay << std::setw(10) << txed_packets << std::setw(13) << rxed_packets
              << std::setw(12) << lost_packets  << std::setw(12) << (maxRate/1000000) << std::setw(15)
              << through << std::endl;
}

std::cout << "Tx_udp_packets " << Tx_udp_packets << std::endl;
std::cout << "Rx_udp_packets " << Rx_udp_packets << std::endl;
// Print the send and receive time stamps maps 
//PrintSendTimestamps(sendTimestamps);
//PrintReceiveTimestamps(receiveTimestamps);
CalculateLatencies();
//PrintEachLatency();
PrintAverageLatency();


Simulator::Destroy();
return 0;
}   