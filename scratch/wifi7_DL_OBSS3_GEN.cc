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
#include "ns3/rng-seed-manager.h"
#include <chrono>


using namespace ns3;
NS_LOG_COMPONENT_DEFINE("eht-wifi-networktrial");


// Open files to write the matrices
std::ofstream throughputOBSS2File;
std::ofstream latencyOBSS2File;


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

uint32_t targetPacketCount = 70000;
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

    /*
    Ipv4Address target_add("192.168.1.1");
    if (destAddress_corr == target_add)
    {
        Rx_udp_packets++;
        if (Rx_udp_packets >= targetPacketCount)
        {
            NS_LOG_INFO("Target packet count reached! Stopping simulation.");
            Simulator::Stop(); // Stop the simulation
        }
    } 
    */ 
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
double distance{1.0};      // meters
std::size_t nStations{1};
double frequency{2.4};       // whether the first link operates in the 2.4, 5 or 6 GHz
double frequency2{0}; // whether the second link operates in the 2.4, 5 or 6 GHz (0 means no second link exists)
double frequency3{0}; // whether the third link operates in the 2.4, 5 or 6 GHz (0 means no third link exists)
int mcs{7}; // -1 indicates an unset value
uint16_t mpduBufferSize{512};
Time accessReqInterval{"1s"};
std::string dlAckSeqType{"NO-OFDMA"};
bool enableUlOfdma{false};
bool enableBsrp{false};
uint32_t number_packets = 4294967295U;
uint16_t channelWidth = 20;
uint16_t gi = 800;
uint32_t payloadSize = 1500;

bool verbose{false};
bool tracing{false};
bool enablePcap{true};
Time tputInterval{0}; // interval for detailed throughput measurement


/* EMLSR Parameters */ 
//std::string emlsrLinks;  
//std::set<uint8_t> emlsrLinks={0, 1, 2};
//std::string emlsrLinks = "0";
//std::string emlsrLinks = "0 1";
std::string emlsrLinks = "0 1 2";
uint16_t Links_number{3};


uint16_t paddingDelayUsec{32};
uint16_t transitionDelayUsec{128};
uint16_t channelSwitchDelayUsec{100};
bool switchAuxPhy{true};
bool auxPhyTxCapable{true};
uint16_t auxPhyChWidth{20};

/*OBSS Parameters*/
uint32_t nBSSs = 4;
int mcs_OBSS = 0;
double traffic_load_BBS1{1.0};  // Factor determin the chanel congestion on the band of different BSSs
double traffic_load_BBS2{-1};  // Factor determin the chanel congestion on the band of different BSSs
double traffic_load_BBS3{-1};  // Factor determin the chanel congestion on the band of different BSSs
double traffic_load_BBS4{-1};  // Factor determin the chanel congestion on the band of different BSSs

/*
double d1 = 30.0;            // meters
double d2 = 30.0;            // meters
double d3 = 100.0;           // meters
double d4 = 100.0;           //meters
double d5 = 30.0;            // meters
double d6 = 100.0;
double d7 = 30.0;
*/

double dSta = 10; //meters
double dAP = 150;  //meters

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
uint32_t Number_runs = 1;


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
cmd.AddValue("enableObssPd", "Enable/disable OBSS_PD", enableObssPd);
//cmd.AddValue("d1", "Distance between STA1 and AP1 (m)", d1);
//cmd.AddValue("d2", "Distance between STA2 and AP2 (m)", d2);
//cmd.AddValue("d3", "Distance between AP1 and AP2 (m)", d3);
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
cmd.AddValue("Links_number", "Number_links", Links_number);
cmd.AddValue("dSta", "Distance between STA and AP (m)", dSta);
cmd.AddValue("dAP", "Distance between Access points (m)", dAP);
cmd.AddValue("Number_runs", "Number of trials", Number_runs);

cmd.Parse(argc, argv);
auto start = std::chrono::high_resolution_clock::now();


// Generate 500 unique random seeds
std::vector<std::size_t> seedVector(Number_runs);
//std::srand(std::time(nullptr));
for (uint32_t i = 0; i < Number_runs; ++i) 
{
    seedVector[i] = std::rand() % 100000; // Generate a random number in the range [0, 99999]
}

//double ObssLoads[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9}; // Array with specific values
double ObssLoads[] = {0.4}; // Array with specific values
const int loads_size = sizeof(ObssLoads) / sizeof(ObssLoads[0]);

std::vector<std::vector<double>> Throughput_analysis(loads_size, std::vector<double>(Number_runs));
std::vector<std::vector<double>> Latency_analysis(loads_size, std::vector<double>(Number_runs));
throughputOBSS2File.open("throughput_mtx.txt");
latencyOBSS2File.open("latency_mtx.txt");


for (std::size_t run = 0; run < Number_runs; ++run) 
{
    // Set the random seed and run number
    std::cout << "Seed value is " << seedVector[run] << std::endl;
    RngSeedManager::SetSeed(12345); // Set a specific seed
    RngSeedManager::SetRun(1);       // Set the run number
    std::cout << "Run number " << run +1 << std::endl;


    for (std::size_t load = 0; load <loads_size ; load++) 
    {  
        traffic_load_BBS2 = ObssLoads[load];  // Factor determine the chanel congestion on the band of different BSSs
        traffic_load_BBS3 = ObssLoads[load];
        traffic_load_BBS4 = ObssLoads[load];
        std::cout << "OBSS load " << ObssLoads[load] << std::endl;

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


        /* Create the station and AP nodes */ 
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
        wifi.SetStandard(WIFI_STANDARD_80211be);

        if (enableObssPd)
        {
            wifi.SetObssPdAlgorithm("ns3::ConstantObssPdAlgorithm",
                                    "ObssPdLevel",
                                    DoubleValue(obssPdThreshold));
        }

        std::vector<Ptr<MultiModelSpectrumChannel>> ChannelSet;
        for (uint8_t linkId = 0; linkId < Links_number; linkId++)
        {
            Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
            Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
            spectrumChannel->AddPropagationLossModel(lossModel);
            Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
            spectrumChannel->SetPropagationDelayModel(delayModel);
            ChannelSet.push_back(spectrumChannel);
        }
        


        /*Configuration of each AP and its stations*/ 
        // BSS A
        std::array<std::string, 3> channelStrA;
        std::array<FrequencyRange, 3> freqRangesA;
        uint64_t nonHtRefRateMbpsA = EhtPhy::GetNonHtReferenceRate(mcs) / 1e6;
        std::string dataModeStrA = "EhtMcs" + std::to_string(mcs);
        std::string ctrlRateStrA;           
        uint8_t nLinksA = 0;
        Ssid ssidA = Ssid("ns3-80211be-A");

        if (frequency2 == frequency || frequency3 == frequency ||
                (frequency3 != 0 && frequency3 == frequency2))
        {
                NS_FATAL_ERROR("Frequency values must be unique!");
        }
        
        for (auto freq : {frequency, frequency2, frequency3})
        {   
            if (nLinksA > 0 && freq == 0)
            {
                    break;
            }
            channelStrA[nLinksA] = "{0, " + std::to_string(channelWidth) + ", ";
            if (freq == 6)
            {
                channelStrA[nLinksA] += "BAND_6GHZ, 0}";
                freqRangesA[nLinksA] = WIFI_SPECTRUM_6_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(6e9));
                wifi.SetRemoteStationManager(nLinksA,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrA),
                                            "ControlMode", StringValue(dataModeStrA));
            }
            else if (freq == 5)
            {
                channelStrA[nLinksA] += "BAND_5GHZ, 0}";
                freqRangesA[nLinksA] = WIFI_SPECTRUM_5_GHZ;
                ctrlRateStrA = "OfdmRate" + std::to_string(nonHtRefRateMbpsA) + "Mbps";
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                    DoubleValue(5.180e9));
                wifi.SetRemoteStationManager(nLinksA,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrA),
                                            "ControlMode", StringValue(ctrlRateStrA));
            }
            else if (freq == 2.4)
            {
                channelStrA[nLinksA] += "BAND_2_4GHZ, 0}";
                freqRangesA[nLinksA] = WIFI_SPECTRUM_2_4_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(2.4e9));
                ctrlRateStrA = "ErpOfdmRate" + std::to_string(nonHtRefRateMbpsA) + "Mbps";
                wifi.SetRemoteStationManager(nLinksA,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrA),
                                            "ControlMode", StringValue(ctrlRateStrA));
            }
            else
            {
                NS_FATAL_ERROR("Wrong frequency value!");
            }
            nLinksA++;
        }
            
        SpectrumWifiPhyHelper spectrumWifiPhyA(nLinksA);
        spectrumWifiPhyA.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        spectrumWifiPhyA.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));

        for (uint8_t linkId = 0; linkId < nLinksA; linkId++)
        {
            spectrumWifiPhyA.Set(linkId, "ChannelSettings", StringValue(channelStrA[linkId]));
            spectrumWifiPhyA.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
            spectrumWifiPhyA.AddChannel(ChannelSet[linkId], freqRangesA[linkId]);
        }

        if (nLinksA > 1)
        {
            //std::cout << "EMLSR option is activated "<< emlsrLinks.empty() << std::endl;
            wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
        }

        spectrumWifiPhyA.Set("TxPowerStart", DoubleValue(powSta1));
        spectrumWifiPhyA.Set("TxPowerEnd", DoubleValue(powSta1));
        spectrumWifiPhyA.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta1));
        spectrumWifiPhyA.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidA));
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
        NetDeviceContainer staDevicesA = wifi.Install(spectrumWifiPhyA, mac, wifiStaNodesA);

        spectrumWifiPhyA.Set("TxPowerStart", DoubleValue(powAp1));
        spectrumWifiPhyA.Set("TxPowerEnd", DoubleValue(powAp1));
        spectrumWifiPhyA.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp1));
        spectrumWifiPhyA.Set("RxSensitivity", DoubleValue(rxSensitivity));
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
                    "Ssid", SsidValue(ssidA));
        NetDeviceContainer apDeviceA = wifi.Install(spectrumWifiPhyA, mac, WifiApNodes.Get(0));

        Ptr<WifiNetDevice> ap1Device = apDeviceA.Get(0)->GetObject<WifiNetDevice>();
        if (enableObssPd)
        {
            ap1Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(1));
        }


        // BSS B
        std::array<std::string, 3> channelStrB;
        std::array<FrequencyRange, 3> freqRangesB;
        uint64_t nonHtRefRateMbpsB = EhtPhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
        std::string dataModeStrB = "EhtMcs" + std::to_string(mcs_OBSS);
        std::string ctrlRateStrB;           
        uint8_t nLinksB = 0;
        Ssid ssidB = Ssid("ns3-80211be-B");

        if (frequency2 == frequency || frequency3 == frequency ||
                (frequency3 != 0 && frequency3 == frequency2))
        {
                NS_FATAL_ERROR("Frequency values must be unique!");
        }
        
        for (auto freq : {frequency, frequency2, frequency3})
        {   
            if (nLinksB > 0 && freq == 0)
            {
                    break;
            }
            channelStrB[nLinksB] = "{0, " + std::to_string(channelWidth) + ", ";
            if (freq == 6)
            {
                channelStrB[nLinksB] += "BAND_6GHZ, 0}";
                freqRangesB[nLinksB] = WIFI_SPECTRUM_6_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(6e9));
                wifi.SetRemoteStationManager(nLinksB,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrB),
                                            "ControlMode", StringValue(dataModeStrB));
            }
            else if (freq == 5)
            {
                channelStrB[nLinksB] += "BAND_5GHZ, 0}";
                freqRangesB[nLinksB] = WIFI_SPECTRUM_5_GHZ;
                ctrlRateStrB = "OfdmRate" + std::to_string(nonHtRefRateMbpsB) + "Mbps";
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                    DoubleValue(5.180e9));
                wifi.SetRemoteStationManager(nLinksB,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrB),
                                            "ControlMode", StringValue(ctrlRateStrB));
            }
            else if (freq == 2.4)
            {
                channelStrB[nLinksB] += "BAND_2_4GHZ, 0}";
                freqRangesB[nLinksB] = WIFI_SPECTRUM_2_4_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(2.4e9));
                ctrlRateStrB = "ErpOfdmRate" + std::to_string(nonHtRefRateMbpsB) + "Mbps";
                wifi.SetRemoteStationManager(nLinksB,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrB),
                                            "ControlMode", StringValue(ctrlRateStrB));
            }
            else
            {
                NS_FATAL_ERROR("Wrong frequency value!");
            }
            nLinksB++;
        }
            
        SpectrumWifiPhyHelper spectrumWifiPhyB(nLinksB);
        spectrumWifiPhyB.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        spectrumWifiPhyB.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));

        for (uint8_t linkId = 0; linkId < nLinksB; linkId++)
        {
            spectrumWifiPhyB.Set(linkId, "ChannelSettings", StringValue(channelStrB[linkId]));
            spectrumWifiPhyB.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
            spectrumWifiPhyB.AddChannel(ChannelSet[linkId], freqRangesB[linkId]);
        }

        if (nLinksB > 1)
        {
            //std::cout << "EMLSR option is activated "<< emlsrLinks.empty() << std::endl;
            wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
        }

        spectrumWifiPhyB.Set("TxPowerStart", DoubleValue(powSta2));
        spectrumWifiPhyB.Set("TxPowerEnd", DoubleValue(powSta2));
        spectrumWifiPhyB.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta2));
        spectrumWifiPhyB.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidB));
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
        NetDeviceContainer staDevicesB = wifi.Install(spectrumWifiPhyB, mac, wifiStaNodesB);

        spectrumWifiPhyB.Set("TxPowerStart", DoubleValue(powAp2));
        spectrumWifiPhyB.Set("TxPowerEnd", DoubleValue(powAp2));
        spectrumWifiPhyB.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp2));
        spectrumWifiPhyB.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::ApWifiMac",
                    "EnableBeaconJitter", BooleanValue(false),
                    "Ssid", SsidValue(ssidB));
        NetDeviceContainer apDeviceB = wifi.Install(spectrumWifiPhyB, mac, WifiApNodes.Get(1));

        Ptr<WifiNetDevice> ap2Device = apDeviceB.Get(0)->GetObject<WifiNetDevice>();
        if (enableObssPd)
        {
            ap2Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(2));
        }


        // BSS C
        std::array<std::string, 3> channelStrC;
        std::array<FrequencyRange, 3> freqRangesC;
        uint64_t nonHtRefRateMbpsC = EhtPhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
        std::string dataModeStrC = "EhtMcs" + std::to_string(mcs_OBSS);
        std::string ctrlRateStrC;           
        uint8_t nLinksC = 0;
        Ssid ssidC = Ssid("ns3-80211be-C");

        if (frequency2 == frequency || frequency3 == frequency ||
                (frequency3 != 0 && frequency3 == frequency2))
        {
                NS_FATAL_ERROR("Frequency values must be unique!");
        }
        
        for (auto freq : {frequency, frequency2, frequency3})
        {   
            if (nLinksC > 0 && freq == 0)
            {
                    break;
            }
            channelStrC[nLinksC] = "{0, " + std::to_string(channelWidth) + ", ";
            if (freq == 6)
            {
                channelStrC[nLinksC] += "BAND_6GHZ, 0}";
                freqRangesC[nLinksC] = WIFI_SPECTRUM_6_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(6e9));
                wifi.SetRemoteStationManager(nLinksC,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrC),
                                            "ControlMode", StringValue(dataModeStrC));
            }
            else if (freq == 5)
            {
                channelStrC[nLinksC] += "BAND_5GHZ, 0}";
                freqRangesC[nLinksC] = WIFI_SPECTRUM_5_GHZ;
                ctrlRateStrC = "OfdmRate" + std::to_string(nonHtRefRateMbpsC) + "Mbps";
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                    DoubleValue(5.180e9));
                wifi.SetRemoteStationManager(nLinksC,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrC),
                                            "ControlMode", StringValue(ctrlRateStrC));
            }
            else if (freq == 2.4)
            {
                channelStrC[nLinksC] += "BAND_2_4GHZ, 0}";
                freqRangesC[nLinksC] = WIFI_SPECTRUM_2_4_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(2.4e9));
                ctrlRateStrC = "ErpOfdmRate" + std::to_string(nonHtRefRateMbpsC) + "Mbps";
                wifi.SetRemoteStationManager(nLinksC,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrC),
                                            "ControlMode", StringValue(ctrlRateStrC));
            }
            else
            {
                NS_FATAL_ERROR("Wrong frequency value!");
            }
            nLinksC++;
        }
            
        SpectrumWifiPhyHelper spectrumWifiPhyC(nLinksC);
        spectrumWifiPhyC.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        spectrumWifiPhyC.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));

        for (uint8_t linkId = 0; linkId < nLinksC; linkId++)
        {
            spectrumWifiPhyC.Set(linkId, "ChannelSettings", StringValue(channelStrC[linkId]));
            spectrumWifiPhyC.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
            spectrumWifiPhyC.AddChannel(ChannelSet[linkId], freqRangesC[linkId]);
        }

        if (nLinksC > 1)
        {
            //std::cout << "EMLSR option is activated "<< emlsrLinks.empty() << std::endl;
            wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
        }

        spectrumWifiPhyC.Set("TxPowerStart", DoubleValue(powSta3));
        spectrumWifiPhyC.Set("TxPowerEnd", DoubleValue(powSta3));
        spectrumWifiPhyC.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta3));
        spectrumWifiPhyC.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidC));
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
        NetDeviceContainer staDevicesC = wifi.Install(spectrumWifiPhyC, mac, wifiStaNodesC);

        spectrumWifiPhyC.Set("TxPowerStart", DoubleValue(powAp3));
        spectrumWifiPhyC.Set("TxPowerEnd", DoubleValue(powAp3));
        spectrumWifiPhyC.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp3));
        spectrumWifiPhyC.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::ApWifiMac",
                    "EnableBeaconJitter", BooleanValue(false),
                    "Ssid", SsidValue(ssidC));
        NetDeviceContainer apDeviceC = wifi.Install(spectrumWifiPhyC, mac, WifiApNodes.Get(2));

        Ptr<WifiNetDevice> ap3Device = apDeviceC.Get(0)->GetObject<WifiNetDevice>();
        if (enableObssPd)
        {
            ap3Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(3));
        }

        // BSS D
        std::array<std::string, 3> channelStrD;
        std::array<FrequencyRange, 3> freqRangesD;
        uint64_t nonHtRefRateMbpsD = EhtPhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
        std::string dataModeStrD = "EhtMcs" + std::to_string(mcs_OBSS);
        std::string ctrlRateStrD;           
        uint8_t nLinksD = 0;
        Ssid ssidD = Ssid("ns3-80211be-D");

        if (frequency2 == frequency || frequency3 == frequency ||
                (frequency3 != 0 && frequency3 == frequency2))
        {
                NS_FATAL_ERROR("Frequency values must be unique!");
        }
        
        for (auto freq : {frequency, frequency2, frequency3})
        {   
            if (nLinksD > 0 && freq == 0)
            {
                    break;
            }
            channelStrD[nLinksD] = "{0, " + std::to_string(channelWidth) + ", ";
            if (freq == 6)
            {
                channelStrD[nLinksD] += "BAND_6GHZ, 0}";
                freqRangesD[nLinksD] = WIFI_SPECTRUM_6_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(6e9));
                wifi.SetRemoteStationManager(nLinksD,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrD),
                                            "ControlMode", StringValue(dataModeStrD));
            }
            else if (freq == 5)
            {
                channelStrD[nLinksD] += "BAND_5GHZ, 0}";
                freqRangesD[nLinksD] = WIFI_SPECTRUM_5_GHZ;
                ctrlRateStrD = "OfdmRate" + std::to_string(nonHtRefRateMbpsD) + "Mbps";
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                    DoubleValue(5.180e9));
                wifi.SetRemoteStationManager(nLinksD,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrD),
                                            "ControlMode", StringValue(ctrlRateStrD));
            }
            else if (freq == 2.4)
            {
                channelStrD[nLinksD] += "BAND_2_4GHZ, 0}";
                freqRangesD[nLinksD] = WIFI_SPECTRUM_2_4_GHZ;
                Config::SetDefault("ns3::FriisPropagationLossModel::Frequency",
                                        DoubleValue(2.4e9));
                ctrlRateStrD = "ErpOfdmRate" + std::to_string(nonHtRefRateMbpsD) + "Mbps";
                wifi.SetRemoteStationManager(nLinksD,
                                            "ns3::ConstantRateWifiManager",
                                            "DataMode", StringValue(dataModeStrD),
                                            "ControlMode", StringValue(ctrlRateStrD));
            }
            else
            {
                NS_FATAL_ERROR("Wrong frequency value!");
            }
            nLinksD++;
        }
            
        SpectrumWifiPhyHelper spectrumWifiPhyD(nLinksD);
        spectrumWifiPhyD.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        spectrumWifiPhyD.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));

        for (uint8_t linkId = 0; linkId < nLinksD; linkId++)
        {
            spectrumWifiPhyD.Set(linkId, "ChannelSettings", StringValue(channelStrD[linkId]));
            spectrumWifiPhyD.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
            spectrumWifiPhyD.AddChannel(ChannelSet[linkId], freqRangesD[linkId]);
        }

        if (nLinksD > 1)
        {
            //std::cout << "EMLSR option is activated "<< emlsrLinks.empty() << std::endl;
            wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
        }

        spectrumWifiPhyD.Set("TxPowerStart", DoubleValue(powSta4));
        spectrumWifiPhyD.Set("TxPowerEnd", DoubleValue(powSta4));
        spectrumWifiPhyD.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta4));
        spectrumWifiPhyD.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidD));
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
        NetDeviceContainer staDevicesD = wifi.Install(spectrumWifiPhyD, mac, wifiStaNodesD);

        spectrumWifiPhyD.Set("TxPowerStart", DoubleValue(powAp4));
        spectrumWifiPhyD.Set("TxPowerEnd", DoubleValue(powAp4));
        spectrumWifiPhyD.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp4));
        spectrumWifiPhyD.Set("RxSensitivity", DoubleValue(rxSensitivity));

        mac.SetType("ns3::ApWifiMac",
                    "EnableBeaconJitter", BooleanValue(false),
                    "Ssid", SsidValue(ssidD));
        NetDeviceContainer apDeviceD = wifi.Install(spectrumWifiPhyD, mac, WifiApNodes.Get(3));

        Ptr<WifiNetDevice> ap4Device = apDeviceC.Get(0)->GetObject<WifiNetDevice>();
        if (enableObssPd)
        {
            ap4Device->GetHeConfiguration()->SetAttribute("BssColor", UintegerValue(4));
        }



        Config::Set( "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval",
                    TimeValue(NanoSeconds(gi)));
        Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MpduBufferSize",
                    UintegerValue(mpduBufferSize));


        /* Mobility model */
        MobilityHelper mobility;
        Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

        /*
        positionAlloc->Add(Vector(²0, 0.0, 0.0)); // AP1
        positionAlloc->Add(Vector(d3, 0.0, 0.0));  // AP2
        positionAlloc->Add(Vector(-d4, 0.0, 0.0));  // AP3
        positionAlloc->Add(Vector(0.0, -d6, 0.0));  // AP4


        positionAlloc->Add(Vector(0.0, d1, 0.0));  // STA1
        positionAlloc->Add(Vector(d3, d2, 0.0));   // STA2
        positionAlloc->Add(Vector(-d4, d5, 0.0));   // STA3
        positionAlloc->Add(Vector(-d7, -d6, 0.0));   // STA4
        */

        //Static circular layout
        positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // Target AP
        positionAlloc->Add(Vector(dAP, 0.0, 0.0));  // OBSS A
        positionAlloc->Add(Vector(0.0, dAP, 0.0));  // OBSS B
        positionAlloc->Add(Vector(-dAP, 0.0, 0.0)); // OBSS C


        positionAlloc->Add(Vector(dSta, dSta, 0.0)); // Station for target AP
        positionAlloc->Add(Vector(dSta+dAP, dSta, 0.0));  // STA2
        positionAlloc->Add(Vector(dSta,dSta+dAP, 0.0));  // STA3
        positionAlloc->Add(Vector(-dAP+dSta,dSta , 0.0)); // STA4

        /* Rectangular grid layout
        positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // Target AP
        positionAlloc->Add(Vector(0.0, dAP, 0.0));  // OBSS A
        positionAlloc->Add(Vector(dAP, 0.0, 0.0));  // OBSS B
        positionAlloc->Add(Vector(0.0, -dAP, 0.0)); // OBSS C


        positionAlloc->Add(Vector(dSta, dSta, 0.0)); // Station for target AP
        positionAlloc->Add(Vector(dSta, dSta+dAP, 0.0));  // STA2
        positionAlloc->Add(Vector(dSta+dAP,  dSta , 0.0));  // STA3
        positionAlloc->Add(Vector(dSta, -dAP+dSta, 0.0)); // STA4
        */


        mobility.SetPositionAllocator(positionAlloc);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(WifiApNodes);
        mobility.Install(wifiStaNodesA);
        mobility.Install(wifiStaNodesB);
        mobility.Install(wifiStaNodesC);
        mobility.Install(wifiStaNodesD);

        /* Internet stack*/
        /*int64_t streamNumber = 150;
        streamNumber += wifi.AssignStreams(apDeviceA, streamNumber);
        streamNumber += wifi.AssignStreams(apDeviceB, streamNumber);
        streamNumber += wifi.AssignStreams(apDeviceC, streamNumber);
        streamNumber += wifi.AssignStreams(apDeviceD, streamNumber);
        streamNumber += wifi.AssignStreams(staDevicesA, streamNumber);
        streamNumber += wifi.AssignStreams(staDevicesB, streamNumber);
        streamNumber += wifi.AssignStreams(staDevicesC, streamNumber);
        streamNumber += wifi.AssignStreams(staDevicesD, streamNumber);
        */


        InternetStackHelper stack;
        stack.Install(WifiApNodes);
        stack.Install(wifiStaNodesA);
        stack.Install(wifiStaNodesB);
        stack.Install(wifiStaNodesC);
        stack.Install(wifiStaNodesD);
        //streamNumber += stack.AssignStreams(WifiApNodes, streamNumber);
        //streamNumber += stack.AssignStreams(wifiStaNodesA, streamNumber);
        //streamNumber += stack.AssignStreams(wifiStaNodesB, streamNumber);
        //streamNumber += stack.AssignStreams(wifiStaNodesC, streamNumber);
        //streamNumber += stack.AssignStreams(wifiStaNodesD, streamNumber);

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



        /* Applications */
        // BSS1
        std::vector<ApplicationContainer> serverSet;
        {
        const auto maxRate =  nLinksA * EhtPhy::GetDataRate(mcs, channelWidth, gi, 1) / nStations;
        //std::cout << "maxRate " << maxRate/1000000 << std::endl;


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
        //streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

        serverAppA.Start(Seconds(0.0));
        serverAppA.Stop(simulationTime + Seconds(1.0));
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
            client.SetAttribute("MaxPackets", UintegerValue(number_packets));
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval1)));
            client.SetAttribute("PacketSize", UintegerValue(payloadSize));
            ApplicationContainer clientAppA = client.Install(clientNodes.Get(0));
            //streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

            clientAppA.Start(Seconds(1.0));
            clientAppA.Stop(simulationTime + Seconds(1.0));
        }
        }

        // BSS2
        {
        const auto maxRate_OBSS = nLinksB * EhtPhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStations;

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
        //streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

        serverAppB.Start(Seconds(0.0));
        serverAppB.Stop(simulationTime + Seconds(1.0));
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
            client.SetAttribute("MaxPackets", UintegerValue(number_packets));
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval2)));
            client.SetAttribute("PacketSize", UintegerValue(payloadSize));
            ApplicationContainer clientAppB = client.Install(clientNodes.Get(0));
            //dstreamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

            clientAppB.Start(Seconds(1.0));
            clientAppB.Stop(simulationTime + Seconds(1.0));
        }
        }

        // BSS3
        {
        const auto maxRate_OBSS3 = nLinksC * EhtPhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStations;

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
        //streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

        serverAppC.Start(Seconds(0.0));
        serverAppC.Stop(simulationTime + Seconds(1.0));
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
            client.SetAttribute("MaxPackets", UintegerValue(number_packets));
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval3)));
            client.SetAttribute("PacketSize", UintegerValue(payloadSize));
            ApplicationContainer clientAppC = client.Install(clientNodes.Get(0));
            //streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

            clientAppC.Start(Seconds(1.0));
            clientAppC.Stop(simulationTime + Seconds(1.0));
        }
        }

        // BSS4
        {
        const auto maxRate_OBSS4 = nLinksD * EhtPhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStations;

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
        //streamNumber += server.AssignStreams(serverNodes.get(), streamNumber);

        serverAppD.Start(Seconds(0.0));
        serverAppD.Stop(simulationTime + Seconds(1.0));
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
            client.SetAttribute("MaxPackets", UintegerValue(number_packets));
            client.SetAttribute("Interval", TimeValue(Seconds(packetInterval4)));
            client.SetAttribute("PacketSize", UintegerValue(payloadSize));
            ApplicationContainer clientAppD = client.Install(clientNodes.Get(0));
            //streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);

            clientAppD.Start(Seconds(1.0));
            clientAppD.Stop(simulationTime + Seconds(1.0));
        }
        }
            

        // Install FlowMonitor 
        FlowMonitorHelper flowmon;
        Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

        if (enablePcap)
        {
            spectrumWifiPhyA.EnablePcap("wifi_7_OBSSA", apDeviceA);
            //spectrumWifiPhyB.EnablePcap("wifi_7_OBSSB", apDeviceB);
            //spectrumWifiPhyC.EnablePcap("wifi_7_OBSSC", apDeviceC);
            //spectrumWifiPhyC.EnablePcap("wifi_7_OBSSD", apDeviceD);
        }
        Simulator::Stop(simulationTime + Seconds(1.0));
        Simulator::Run();



        /* Print per flow statistics */
        monitor->CheckForLostPackets ();
        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
        std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

        double through = 0;
        double delay = 0;
        double txed_packets = 0;
        double rxed_packets = 0;
        double lost_packets = 0;
        double counter =0;
        //std::cout << std::setw(5) << "Delay(s) " << std::setw(15) << "txed packets " << std::setw(13) << "rxed packets "
        //            << std::setw(12) << "lost packets " << std::setw(10) << "Throughput " << std::endl;
        for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
        {   
            //Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
            //std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
            //std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
            //std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
            through = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024;
            //std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1024 / 1024  << " Mbps\n";
            //std::cout << "Time last packet received " << i->second.timeFirstTxPacket.GetSeconds () << "\n";*
            delay = (i->second.delaySum.GetSeconds())/(i->second.rxPackets);
            //std::cout << "  Delay sum:  " << (i->second.delaySum.GetSeconds()) << " s\n";
            txed_packets = (i->second.txPackets);
            //std::cout << "  txed packets: " << (i->second.txPackets)<< "\n";
            rxed_packets = (i->second.rxPackets);
            //std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
            lost_packets = (i->second.lostPackets);
            //std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";
            std::cout << std::setw(5) << delay << std::setw(10) << txed_packets << std::setw(13) << rxed_packets
                    << std::setw(12) << lost_packets << std::setw(15) << through << std::endl;
            if (counter == 0) // Save the results of the target BSS 
            {
            Throughput_analysis[load][run] = through;
            Latency_analysis[load][run] = delay;
            }
            counter++;
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
    }
}   

// Print the matrices
std::cout << "Matrix:" << std::endl;
for (size_t i = 0; i < loads_size; ++i) {
    for (size_t j = 0; j < Number_runs; ++j) {
        std::cout << Throughput_analysis[i][j] << "\t";
    }
    std::cout << std::endl;
}
std::cout << "Matrix:" << std::endl;
for (size_t i = 0; i < loads_size; ++i) {
    for (size_t j = 0; j < Number_runs; ++j) {
        std::cout << Latency_analysis[i][j] << "\t";
    }
    std::cout << std::endl;
}

throughputOBSS2File.flush();
latencyOBSS2File.flush();

// Write the matrix in a space-separated format
for (size_t i = 0; i < loads_size; ++i) {
    for (size_t j = 0; j < Number_runs; ++j) {
        throughputOBSS2File << Throughput_analysis[i][j] << " ";
    }
    throughputOBSS2File << std::endl;  // New line after each row
}
for (size_t i = 0; i < loads_size; ++i) {
    for (size_t j = 0; j < Number_runs; ++j) {
        latencyOBSS2File << Latency_analysis[i][j] << " ";
    }
    latencyOBSS2File << std::endl;  // New line after each row
}

throughputOBSS2File.close();
latencyOBSS2File.close();

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
std::cout << "Simulation took: " << duration.count() << " seconds in real time" << std::endl;
return 0;
}   
