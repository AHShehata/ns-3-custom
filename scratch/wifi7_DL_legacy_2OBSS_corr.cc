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
 NS_LOG_COMPONENT_DEFINE("EHT_wifi-network");
 std::ofstream LatecnyCdfFile;
 
 
 // Open files to write the matrices
 std::ofstream throughputOBSS2File;
 std::ofstream latencyOBSS2File;
 uint32_t payloadSize = 1500;
 
 
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
 
 uint32_t targetPacketCount = 10000;
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
 
     
     Ipv4Address target_add("192.168.1.1");
     if (destAddress_corr == target_add)
     {
         Rx_udp_packets++;
         if (Rx_udp_packets >= targetPacketCount)
         {
             Rx_udp_packets = 0;
             NS_LOG_INFO("Target packet count reached! Stopping simulation.");
             Simulator::Stop(); // Stop the simulation
         }
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
 
 int numPackets_rxed_phy = 0;
 void
 PhyRxDoneTrace(std::string context, Ptr<const Packet> p)
 {
     if (p->GetSize() >= payloadSize) // ignore non-data frames
     {
         numPackets_rxed_phy ++;
     }
 }
 
 int numrxedPackets_dropped_phy = 0;
 void
 PhyRxDropTrace(std::string context, Ptr<const Packet> p, WifiPhyRxfailureReason reason)
 {
     numrxedPackets_dropped_phy ++;
 }
 
 
 
 int
 main(int argc, char* argv[])
 {
 
 
 // Parameters 
 bool udp{true}; // Application used either TCP/UDP
 bool downlink{true};    // Downlink/ UL
 bool useRts{true};      //Use RTS/CTS or not
 Time simulationTime{"10s"}; //seconds
 double distance{10.0};      // meters
 std::size_t nStationsT{1};
 uint16_t nStationsL = 1;
 double frequency{2.4};  // whether the first link operates in the 2.4s, 5 or 6 GHz
 double frequency2{0}; // whether the second link operates in the 2.4, 5 or 6 GHz (0 means no second link exists)
 double frequency3{0}; // whether the third link operates in the 2.4, 5 or 6 GHz (0 means no third link exists)
 int mcs{7}; // -1 indicates an unset value
 uint16_t mpduBufferSize{64};
 Time accessReqInterval{"1s"};
 std::string dlAckSeqType{"NO-OFDMA"};
 bool enableUlOfdma{false};
 bool enableBsrp{false};
 uint32_t number_packets = 4294967295U;
 uint16_t channelWidth = 20;
 uint16_t gi = 800;
 uint32_t payloadSize = 1500;
 bool useExtendedBlockAck{false}; // Illustrate more it for me
 
 bool verbose{false};
 bool tracing{false};
 bool enablePcap{true};
 Time tputInterval{0}; // interval for detailed throughput measurement
 
 
 /* EMLSR Parameters */ 
 std::string emlsrLinks = "0";   // Comma sepearated link ids
 //std::string emlsrLinks = "0,1";
 //std::string emlsrLinks = "0,1,2";
 uint16_t Links_number{1};
 
 

 uint16_t paddingDelayUsec{32};
 uint16_t transitionDelayUsec{128};
 uint16_t channelSwitchDelayUsec{128};
 bool switchAuxPhy{true};
 bool auxPhyTxCapable{true};
 uint16_t auxPhyChWidth{20};
 bool EMLSR_mode{true};     // Switch between STR and EMLSR modes, True enabling EMLSR
 
 
 /*OBSS Parameters*/
 uint32_t nBSSs = 3;
 int mcs_OBSS = 7;
 double traffic_load_BBS1{1.0};  // Factor determine the chanel congestion on the band of different BSSs
 double traffic_load_BBS2{1.0};  // Factor determine the chanel congestion on the band of different BSSs
 double& traffic_load_BBS3 = traffic_load_BBS2;  // Factor determine the chanel congestion on the band of different BSSs
 double& traffic_load_BBS4 = traffic_load_BBS2;  // Factor determine the chanel congestion on the band of different BSSs
 double& traffic_load_BBS5 = traffic_load_BBS2 ;  // Factor determine the chanel congestion on the band of different BSSs
 double& traffic_load_BBS6 = traffic_load_BBS2;  // Factor determine the chanel congestion on the band of different BSSs
 double& traffic_load_BBS7 = traffic_load_BBS2;  // Factor determine the chanel congestion on the band of different BSSs
 
 //double d1 = 30.0;            // meters
 //double d2 = 30.0;            // meters
 //double d3 = 150.0;           // meters
 //double d4 = 150.0;           //meters
 //double d5 = 30.0;            // meters
 bool enableObssPd = false;
 double obssPdThreshold = -72.0; // dBm
 
 double powSta1 = 10.0;       // dBm
 double powSta2 = 10.0;       // dBm
 double powSta3 = 10.0;       // dBm
 double powSta4 = 10.0;       // dBm
 double powSta5 = 10.0;       // dBm
 double powSta6 = 10.0;       // dBm
 double powSta7 = 10.0;       // dBm
 
 double powAp1 = 16.02;        // dBm
 double powAp2 = 16.02;        // dBm
 double powAp3 = 16.02;        // dBm
 double powAp4 = 16.02;        // dBm
 double powAp5 = 16.02;        // dBm
 double powAp6 = 16.02;        // dBm
 double powAp7 = 16.02;        // dBm
 
 double ccaEdTrSta1 = -62;    // dBm
 double ccaEdTrSta2 = -62;    // dBm
 double ccaEdTrSta3 = -62;    // dBm
 double ccaEdTrSta4 = -62;    // dBm
 double ccaEdTrSta5 = -62;    // dBm
 double ccaEdTrSta6 = -62;    // dBm
 double ccaEdTrSta7 = -62;    // dBm
 
 double ccaEdTrAp1 = -62;     // dBm
 double ccaEdTrAp2 = -62;     // dBm
 double ccaEdTrAp3 = -62;     // dBm
 double ccaEdTrAp4 = -62;     // dBm
 double ccaEdTrAp5 = -62;     // dBm
 double ccaEdTrAp6 = -62;     // dBm
 double ccaEdTrAp7 = -62;     // dBm
 
 double minimumRssi = -82;    // dBm
 double rxSensitivity = -101;  // dBm (-92)
 
 double dSta = 10; //meters
 double dAP = 150;  //meters
 uint32_t Number_runs = 1;   // number of seeds per run
 uint16_t antennas_AP = 1;   // number of antennas of the AP of the target AP
uint16_t antennas_Sta = 1;   // number of antennas of the station
 uint16_t antennas_OBSS = 1; // number of antennas of the AP of the OBSS APs
 
 
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
 cmd.AddValue("nStationsT", "Number of non-AP EHT stations", nStationsT);
 cmd.AddValue("nStationsL", "Number of non-AP EHT stations", nStationsL);
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
 cmd.AddValue("traffic_load_BBS4", "OBSS 4 congestion level", traffic_load_BBS4);
 cmd.AddValue("traffic_load_BBS5", "OBSS 5 congestion level", traffic_load_BBS5);
 cmd.AddValue("traffic_load_BBS6", "OBSS 6 congestion level", traffic_load_BBS6);
 cmd.AddValue("traffic_load_BBS7", "OBSS 7 congestion level", traffic_load_BBS7);
 cmd.AddValue("EMLSR_mode", "Flag, true for EMLSR, false for STR",  EMLSR_mode);
 
 
 
 cmd.AddValue("mcs_OBSS", "Used mcs for OBSS", mcs_OBSS);
 cmd.AddValue("Links_number", "Number_links", Links_number);
 cmd.AddValue("dSta", "Distance between STA and AP (m)", dSta);
 cmd.AddValue("dAP", "Distance between Access points (m)", dAP);
 cmd.AddValue("Number_runs", "Number of trials", Number_runs);
 cmd.AddValue("useExtendedBlockAck", "Enable/disable use of extended BACK", useExtendedBlockAck);
 cmd.AddValue("nBSSs", "Number of BSSs in the network", nBSSs);
 cmd.AddValue("antennas_AP", "Number of antennas for spatial multiplexing",  antennas_AP);
cmd.AddValue("antennas_Sta", "Number of antennas for spatial multiplexing",  antennas_Sta);
 cmd.AddValue("antennas_OBSS", "Number of antennas for spatial multiplexing in the OBSS network",  antennas_OBSS);
 
 
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
 double ObssLoads[] = {0.5}; // Array with specific values
 const int loads_size = sizeof(ObssLoads) / sizeof(ObssLoads[0]);
 
 //std::vector<std::vector<double>> Throughput_analysis(loads_size, std::vector<double>(Number_runs));
 //std::vector<std::vector<double>> Latency_analysis(loads_size, std::vector<double>(Number_runs));
 
 std::vector<double> Throughput_analysis(Number_runs);
 std::vector<double> Latency_analysis(Number_runs);
 
 throughputOBSS2File.open("throughput_mtx.txt");
 latencyOBSS2File.open("latency_mtx.txt");
 
 
 
 
 for (std::size_t run = 0; run < Number_runs; ++run) 
 {
     // Set the random seed and run number
     std::cout << "Seed value is " << seedVector[run] << std::endl;
     RngSeedManager::SetSeed(12345); // Set a specific seed
     RngSeedManager::SetRun(run+1);       // Set the run number
     std::cout << "Run number " << run +1 << std::endl;
     for (std::size_t load = 0; load <loads_size ; load++) 
     {  
         traffic_load_BBS2 = ObssLoads[load];  // Factor determine the chanel congestion on the band of different BSSs
         std::cout << "OBSS load " << traffic_load_BBS2 << std::endl;
 
         if (verbose)
         {
             WifiHelper::EnableLogComponents(LOG_LEVEL_INFO);
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
 
         /* Create the station and AP nodes */ 
         NodeContainer wifiStaNodesA;
         wifiStaNodesA.Create(nStationsT);
 
         NodeContainer wifiStaNodesB;
         wifiStaNodesB.Create(nStationsL);
         NodeContainer wifiStaNodesC;
         wifiStaNodesC.Create(nStationsL);
         NodeContainer wifiStaNodesD;
         wifiStaNodesD.Create(nStationsL);
         NodeContainer wifiStaNodesE;
         wifiStaNodesE.Create(nStationsL);
         NodeContainer wifiStaNodesF;
         wifiStaNodesF.Create(nStationsL);
         NodeContainer wifiStaNodesG;
         wifiStaNodesG.Create(nStationsL);
         
         if (frequency2==0 && frequency3==0)
         {
             nBSSs = 3;
         }
         else if (frequency2==0 || frequency3==0)
         {
         nBSSs = 5;
         }
         else 
         {
             nBSSs = 7;
         }
         NodeContainer WifiApNodes;
         WifiApNodes.Create(nBSSs);
         WifiHelper wifi;
 
 
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
         WifiMacHelper mac;
         wifi.SetStandard(WIFI_STANDARD_80211be);
 
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
         wifi.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
 
         SpectrumWifiPhyHelper spectrumWifiPhyA(nLinksA);
         spectrumWifiPhyA.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
         spectrumWifiPhyA.Set("ChannelSwitchDelay", TimeValue(MicroSeconds(channelSwitchDelayUsec)));
 
         for (uint8_t linkId = 0; linkId < nLinksA; linkId++)
         {
             spectrumWifiPhyA.Set(linkId, "ChannelSettings", StringValue(channelStrA[linkId]));
             spectrumWifiPhyA.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
 
             //spectrumWifiPhyA.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
             spectrumWifiPhyA.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelD"));
 
             spectrumWifiPhyA.AddChannel(ChannelSet[linkId], freqRangesA[linkId]);
         }
 
         spectrumWifiPhyA.Set("TxPowerStart", DoubleValue(powSta1));
         spectrumWifiPhyA.Set("TxPowerEnd", DoubleValue(powSta1));
         spectrumWifiPhyA.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta1));
         spectrumWifiPhyA.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
         
         for (uint8_t linkId = 0; linkId < nLinksA; linkId++)
         {
            spectrumWifiPhyA.Set(linkId, "Antennas", UintegerValue(antennas_Sta)); // At least 2 antennas for 2 RF chains
            spectrumWifiPhyA.Set(linkId, "MaxSupportedRxSpatialStreams", UintegerValue(antennas_Sta)); // Support 2 Rx streams
            spectrumWifiPhyA.Set(linkId, "MaxSupportedTxSpatialStreams", UintegerValue(antennas_Sta)); // Support 2 Tx streams
         }
 
         mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidA),
                     "MpduBufferSize", UintegerValue(mpduBufferSize));
         mac.SetEmlsrManager("ns3::DefaultEmlsrManager",
                             "EmlsrLinkSet",
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
         if (nLinksA > 1 && EMLSR_mode)
         {
             std::cout << "EMLSR mode is activated " << std::endl;
             wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(true));
         }
         else if (nLinksA > 1 && !EMLSR_mode)
         {
             std::cout << "STR mode is activated " << std::endl;
             wifi.ConfigEhtOptions("EmlsrActivated", BooleanValue(false));
         }
 
         NetDeviceContainer staDeviceA = wifi.Install(spectrumWifiPhyA, mac, wifiStaNodesA);
 
 
         spectrumWifiPhyA.Set("TxPowerStart", DoubleValue(powAp1));
         spectrumWifiPhyA.Set("TxPowerEnd", DoubleValue(powAp1));
         spectrumWifiPhyA.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp1));
         spectrumWifiPhyA.Set("RxSensitivity", DoubleValue(rxSensitivity));
         for (uint8_t linkId = 0; linkId < nLinksA; linkId++)
         {
            spectrumWifiPhyA.Set(linkId, "Antennas", UintegerValue(antennas_AP)); // At least 2 antennas for 2 RF chains
            spectrumWifiPhyA.Set(linkId, "MaxSupportedRxSpatialStreams", UintegerValue(antennas_AP)); // Support 2 Rx streams
            spectrumWifiPhyA.Set(linkId, "MaxSupportedTxSpatialStreams", UintegerValue(antennas_AP)); // Support 2 Tx streams
         }

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
 
 
         std::array<std::string, 3> channelStrB;
         std::array<std::string, 3> datastrB;
         std::array<std::string, 3> ctrlstrB;
         uint64_t nonHtRefRateMbpsB = HePhy::GetNonHtReferenceRate(mcs_OBSS) / 1e6;
         std::string dataModeStrB = "HeMcs" + std::to_string(mcs_OBSS);
         std::string ctrlRateStrB;           
         uint8_t nLinksB = 0;
 
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
                 datastrB[nLinksB] += dataModeStrB;
                 ctrlstrB[nLinksB] += dataModeStrB;
             }
             else if (freq == 5)
             {
                 channelStrB[nLinksB] += "BAND_5GHZ, 0}";
                 ctrlRateStrB = "OfdmRate" + std::to_string(nonHtRefRateMbpsB) + "Mbps";
                 datastrB[nLinksB] += dataModeStrB;
                 ctrlstrB[nLinksB] += ctrlRateStrB;
             }
             else if (freq == 2.4)
             {
                 channelStrB[nLinksB] += "BAND_2_4GHZ, 0}";
                 ctrlRateStrB = "ErpOfdmRate" + std::to_string(nonHtRefRateMbpsB) + "Mbps";
                 datastrB[nLinksB] += dataModeStrB;
                 ctrlstrB[nLinksB] += ctrlRateStrB;
             }
             else
             {
                 NS_FATAL_ERROR("Wrong frequency value!");
             }
             nLinksB++;
         }
 
         // BSS B
         WifiHelper wifiB;
         WifiMacHelper macB;
         Ssid ssidB = Ssid("ns3-80211ax-B");
 
         wifiB.SetStandard(WIFI_STANDARD_80211ax);
         wifiB.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
         wifiB.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode", StringValue(datastrB[0]),
                                     "ControlMode", StringValue(ctrlstrB[0]));
 
         SpectrumWifiPhyHelper spectrumWifiPhyB;
         spectrumWifiPhyB.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
         spectrumWifiPhyB.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
         //   spectrumWifiPhyB.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
         //   spectrumWifiPhyB.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelB"));
 
         spectrumWifiPhyB.Set("ChannelSettings", StringValue(channelStrB[0]));
         spectrumWifiPhyB.SetChannel(ChannelSet[0]);
     
 
         spectrumWifiPhyB.Set("TxPowerStart", DoubleValue(powSta2));
         spectrumWifiPhyB.Set("TxPowerEnd", DoubleValue(powSta2));
         spectrumWifiPhyB.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta2));
         spectrumWifiPhyB.Set("RxSensitivity", DoubleValue(rxSensitivity));
         spectrumWifiPhyB.Set("Antennas", UintegerValue(antennas_OBSS)); // At least 2 antennas for 2 RF chains
         spectrumWifiPhyB.Set("MaxSupportedRxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Rx streams
         spectrumWifiPhyB.Set("MaxSupportedTxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Tx streams
 
         
         macB.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidB),
                     "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
         NetDeviceContainer staDeviceB = wifiB.Install(spectrumWifiPhyB, macB, wifiStaNodesB);
 
 
         spectrumWifiPhyB.Set("TxPowerStart", DoubleValue(powAp2));
         spectrumWifiPhyB.Set("TxPowerEnd", DoubleValue(powAp2));
         spectrumWifiPhyB.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp2));
         spectrumWifiPhyB.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
         macB.SetType("ns3::ApWifiMac",
                     "EnableBeaconJitter", BooleanValue(false),
                     "Ssid", SsidValue(ssidB));
         NetDeviceContainer apDeviceB = wifiB.Install(spectrumWifiPhyB, macB, WifiApNodes.Get(1));
 
         Ptr<WifiNetDevice> APdevB = DynamicCast<WifiNetDevice>(apDeviceB.Get(0));
         Ptr<WifiPhy> AP_phyB = DynamicCast<WifiPhy>(APdevB->GetPhy());
         Ptr<MultiModelSpectrumChannel> channelB =  DynamicCast<MultiModelSpectrumChannel>(AP_phyB->GetChannel());
         Ptr<FriisPropagationLossModel> friisModelB = DynamicCast<FriisPropagationLossModel>(channelB->GetPropagationLossModel());
         friisModelB->SetFrequency(2.4e9);
 
 
         // BSS C
         WifiHelper wifiC;
         WifiMacHelper macC;
         Ssid ssidC = Ssid("ns3-80211ax-C");
 
         wifiC.SetStandard(WIFI_STANDARD_80211ax);
         wifiC.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
         wifiC.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode", StringValue(datastrB[0]),
                                     "ControlMode", StringValue(ctrlstrB[0]));
 
         SpectrumWifiPhyHelper spectrumWifiPhyC;
         spectrumWifiPhyC.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
         spectrumWifiPhyC.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
         //   spectrumWifiPhyC.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
          //  spectrumWifiPhyC.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelB"));
 
         spectrumWifiPhyC.Set("ChannelSettings", StringValue(channelStrB[0]));
         spectrumWifiPhyC.SetChannel(ChannelSet[0]);
 
         spectrumWifiPhyC.Set("TxPowerStart", DoubleValue(powSta3));
         spectrumWifiPhyC.Set("TxPowerEnd", DoubleValue(powSta3));
         spectrumWifiPhyC.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta3));
         spectrumWifiPhyC.Set("RxSensitivity", DoubleValue(rxSensitivity));
         spectrumWifiPhyC.Set("Antennas", UintegerValue(antennas_OBSS)); // At least 2 antennas for 2 RF chains
         spectrumWifiPhyC.Set("MaxSupportedRxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Rx streams
         spectrumWifiPhyC.Set("MaxSupportedTxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Tx streams
 
         macC.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidC),
                     "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
         NetDeviceContainer staDeviceC = wifiC.Install(spectrumWifiPhyC, macC, wifiStaNodesC);
 
 
         spectrumWifiPhyC.Set("TxPowerStart", DoubleValue(powAp3));
         spectrumWifiPhyC.Set("TxPowerEnd", DoubleValue(powAp3));
         spectrumWifiPhyC.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp3));
         spectrumWifiPhyC.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
         macC.SetType("ns3::ApWifiMac",
                     "EnableBeaconJitter", BooleanValue(false),
                     "Ssid", SsidValue(ssidC));
         NetDeviceContainer apDeviceC = wifiC.Install(spectrumWifiPhyC, macC, WifiApNodes.Get(2));
 
         Ptr<WifiNetDevice> APdevC = DynamicCast<WifiNetDevice>(apDeviceC.Get(0));
         Ptr<WifiPhy> AP_phyC = DynamicCast<WifiPhy>(APdevC->GetPhy());
         Ptr<MultiModelSpectrumChannel> channelC =  DynamicCast<MultiModelSpectrumChannel>(AP_phyC->GetChannel());
         Ptr<FriisPropagationLossModel> friisModelC = DynamicCast<FriisPropagationLossModel>(channelC->GetPropagationLossModel());
         friisModelC->SetFrequency(2.4e9);
 
 
         NetDeviceContainer staDeviceD;
         NetDeviceContainer apDeviceD;
         NetDeviceContainer apDeviceE;
         NetDeviceContainer staDeviceE;
         NetDeviceContainer apDeviceF;
         NetDeviceContainer staDeviceF;
         NetDeviceContainer apDeviceG;
         NetDeviceContainer staDeviceG;
         SpectrumWifiPhyHelper spectrumWifiPhyD;
         SpectrumWifiPhyHelper spectrumWifiPhyE;
         SpectrumWifiPhyHelper spectrumWifiPhyF;
         SpectrumWifiPhyHelper spectrumWifiPhyG;
 
 
         if (nLinksB == 2 || nLinksB == 3)
         {
             // BSS D
             WifiHelper wifiD;
             WifiMacHelper macD;
             Ssid ssidD = Ssid("ns3-80211ax-D");
 
             wifiD.SetStandard(WIFI_STANDARD_80211ax);
             wifiD.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
             wifiD.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                         "DataMode", StringValue(datastrB[1]),
                                         "ControlMode", StringValue(ctrlstrB[1]));
 
             spectrumWifiPhyD.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
             spectrumWifiPhyD.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
             // spectrumWifiPhyD.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
             // spectrumWifiPhyD.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelB"));
 
             spectrumWifiPhyD.Set("ChannelSettings", StringValue(channelStrB[1]));
             spectrumWifiPhyD.SetChannel(ChannelSet[1]);
 
             spectrumWifiPhyD.Set("TxPowerStart", DoubleValue(powSta4));
             spectrumWifiPhyD.Set("TxPowerEnd", DoubleValue(powSta4));
             spectrumWifiPhyD.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta4));
             spectrumWifiPhyD.Set("RxSensitivity", DoubleValue(rxSensitivity));
             spectrumWifiPhyD.Set("Antennas", UintegerValue(antennas_OBSS)); // At least 2 antennas for 2 RF chains
             spectrumWifiPhyD.Set("MaxSupportedRxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Rx streams
             spectrumWifiPhyD.Set("MaxSupportedTxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Tx streams
 
 
             macD.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidD),
                         "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
             staDeviceD = wifiD.Install(spectrumWifiPhyD, macD, wifiStaNodesD);
 
 
             spectrumWifiPhyD.Set("TxPowerStart", DoubleValue(powAp4));
             spectrumWifiPhyD.Set("TxPowerEnd", DoubleValue(powAp4));
             spectrumWifiPhyD.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp4));
             spectrumWifiPhyD.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
 
             macD.SetType("ns3::ApWifiMac",
                         "EnableBeaconJitter", BooleanValue(false),
                         "Ssid", SsidValue(ssidD));
 
             apDeviceD = wifiD.Install(spectrumWifiPhyD, macD, WifiApNodes.Get(3));
 
 
             Ptr<WifiNetDevice> APdevD = DynamicCast<WifiNetDevice>(apDeviceD.Get(0));
             Ptr<WifiPhy> AP_phyD = DynamicCast<WifiPhy>(APdevD->GetPhy());
             Ptr<MultiModelSpectrumChannel> channelD =  DynamicCast<MultiModelSpectrumChannel>(AP_phyD->GetChannel());
             Ptr<FriisPropagationLossModel> friisModelD = DynamicCast<FriisPropagationLossModel>(channelD->GetPropagationLossModel());
             friisModelD->SetFrequency(5.180e9);
 
 
             // BSS E
             WifiHelper wifiE;
             WifiMacHelper macE;
             Ssid ssidE = Ssid("ns3-80211ax-E");
 
             wifiE.SetStandard(WIFI_STANDARD_80211ax);
             wifiE.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
             wifiE.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                             "DataMode", StringValue(datastrB[1]),
                                             "ControlMode", StringValue(ctrlstrB[1]));
 
             spectrumWifiPhyE.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
             spectrumWifiPhyE.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
             // spectrumWifiPhyE.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
             // spectrumWifiPhyE.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelB"));
 
             spectrumWifiPhyE.Set("ChannelSettings", StringValue(channelStrB[1]));
             spectrumWifiPhyE.SetChannel(ChannelSet[1]);
 
             spectrumWifiPhyE.Set("TxPowerStart", DoubleValue(powSta5));
             spectrumWifiPhyE.Set("TxPowerEnd", DoubleValue(powSta5));
             spectrumWifiPhyE.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta5));
             spectrumWifiPhyE.Set("RxSensitivity", DoubleValue(rxSensitivity));
             spectrumWifiPhyE.Set("Antennas", UintegerValue(antennas_OBSS)); // At least 2 antennas for 2 RF chains
             spectrumWifiPhyE.Set("MaxSupportedRxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Rx streams
             spectrumWifiPhyE.Set("MaxSupportedTxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Tx streams
 
 
             macE.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidE),
                         "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
             staDeviceE = wifiE.Install(spectrumWifiPhyE, macE, wifiStaNodesE);
 
 
             spectrumWifiPhyE.Set("TxPowerStart", DoubleValue(powAp5));
             spectrumWifiPhyE.Set("TxPowerEnd", DoubleValue(powAp5));
             spectrumWifiPhyE.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp5));
             spectrumWifiPhyE.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
             macE.SetType("ns3::ApWifiMac",
                         "EnableBeaconJitter", BooleanValue(false),
                         "Ssid", SsidValue(ssidE));
             apDeviceE = wifiE.Install(spectrumWifiPhyE, macE, WifiApNodes.Get(4));
 
             Ptr<WifiNetDevice> APdevE = DynamicCast<WifiNetDevice>(apDeviceE.Get(0));
             Ptr<WifiPhy> AP_phyE = DynamicCast<WifiPhy>(APdevE->GetPhy());
             Ptr<MultiModelSpectrumChannel> channelE =  DynamicCast<MultiModelSpectrumChannel>(AP_phyE->GetChannel());
             Ptr<FriisPropagationLossModel> friisModelE = DynamicCast<FriisPropagationLossModel>(channelE->GetPropagationLossModel());
             friisModelE->SetFrequency(5.180e9);
         }
         
         if (nLinksB == 3)
         {   
             // BSS F
             WifiHelper wifiF;
             WifiMacHelper macF;
             Ssid ssidF = Ssid("ns3-80211ax-F");
 
             wifiF.SetStandard(WIFI_STANDARD_80211ax);
             wifiF.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
             wifiF.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                         "DataMode", StringValue(datastrB[2]),
                                         "ControlMode", StringValue(ctrlstrB[2]));
 
             spectrumWifiPhyF.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
             spectrumWifiPhyF.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
              //spectrumWifiPhyF.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
              //spectrumWifiPhyF.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelB"));
 
             spectrumWifiPhyF.Set("ChannelSettings", StringValue(channelStrB[2]));
             spectrumWifiPhyF.SetChannel(ChannelSet[2]);
 
             spectrumWifiPhyF.Set("TxPowerStart", DoubleValue(powSta6));
             spectrumWifiPhyF.Set("TxPowerEnd", DoubleValue(powSta6));
             spectrumWifiPhyF.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta6));
             spectrumWifiPhyF.Set("RxSensitivity", DoubleValue(rxSensitivity));
             spectrumWifiPhyF.Set("Antennas", UintegerValue(antennas_OBSS)); // At least 2 antennas for 2 RF chains
             spectrumWifiPhyF.Set("MaxSupportedRxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Rx streams
             spectrumWifiPhyF.Set("MaxSupportedTxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Tx streams
 
 
             macF.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidF),
                         "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
             staDeviceF = wifiF.Install(spectrumWifiPhyF, macF, wifiStaNodesF);
 
 
             spectrumWifiPhyF.Set("TxPowerStart", DoubleValue(powAp6));
             spectrumWifiPhyF.Set("TxPowerEnd", DoubleValue(powAp6));
             spectrumWifiPhyF.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp6));
             spectrumWifiPhyF.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
             macF.SetType("ns3::ApWifiMac",
                         "EnableBeaconJitter", BooleanValue(false),
                         "Ssid", SsidValue(ssidF));
             apDeviceF = wifiF.Install(spectrumWifiPhyF, macF, WifiApNodes.Get(5));
 
             Ptr<WifiNetDevice> APdevF = DynamicCast<WifiNetDevice>(apDeviceF.Get(0));
             Ptr<WifiPhy> AP_phyF = DynamicCast<WifiPhy>(APdevF->GetPhy());
             Ptr<MultiModelSpectrumChannel> channelF =  DynamicCast<MultiModelSpectrumChannel>(AP_phyF->GetChannel());
             Ptr<FriisPropagationLossModel> friisModelF = DynamicCast<FriisPropagationLossModel>(channelF->GetPropagationLossModel());
             friisModelF->SetFrequency(6e9);
 
 
             // BSS G
             WifiHelper wifiG;
             WifiMacHelper macG;
             Ssid ssidG = Ssid("ns3-80211ax-G");
 
             wifiG.SetStandard(WIFI_STANDARD_80211ax);
             wifiG.ConfigHeOptions("GuardInterval", TimeValue(NanoSeconds(gi)));
             wifiG.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                             "DataMode", StringValue(datastrB[2]),
                                             "ControlMode", StringValue(ctrlstrB[2]));
 
             spectrumWifiPhyG.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
             spectrumWifiPhyG.SetErrorRateModel ("ns3::TableBasedErrorRateModel");
              //spectrumWifiPhyG.SetErrorRateModel ("ns3::LogSgnErrorRateModel");
              //spectrumWifiPhyG.SetErrorRateModel ("ns3::LogSgnErrorRateModel", "ChannelModelType",StringValue("ChannelModelB"));
 
             spectrumWifiPhyG.Set("ChannelSettings", StringValue(channelStrB[2]));
             spectrumWifiPhyG.SetChannel(ChannelSet[2]);
 
             spectrumWifiPhyG.Set("TxPowerStart", DoubleValue(powSta7));
             spectrumWifiPhyG.Set("TxPowerEnd", DoubleValue(powSta7));
             spectrumWifiPhyG.Set("CcaEdThreshold", DoubleValue(ccaEdTrSta7));
             spectrumWifiPhyG.Set("RxSensitivity", DoubleValue(rxSensitivity));
             spectrumWifiPhyG.Set("Antennas", UintegerValue(antennas_OBSS)); // At least 2 antennas for 2 RF chains
             spectrumWifiPhyG.Set("MaxSupportedRxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Rx streams
             spectrumWifiPhyG.Set("MaxSupportedTxSpatialStreams", UintegerValue(antennas_OBSS)); // Support 2 Tx streams
 
 
             macG.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssidG),
                         "MpduBufferSize", UintegerValue(useExtendedBlockAck ? 256 : 64));
             staDeviceG = wifiG.Install(spectrumWifiPhyG, macG, wifiStaNodesG);
 
 
             spectrumWifiPhyG.Set("TxPowerStart", DoubleValue(powAp7));
             spectrumWifiPhyG.Set("TxPowerEnd", DoubleValue(powAp7));
             spectrumWifiPhyG.Set("CcaEdThreshold", DoubleValue(ccaEdTrAp7));
             spectrumWifiPhyG.Set("RxSensitivity", DoubleValue(rxSensitivity));
 
             macG.SetType("ns3::ApWifiMac",
                         "EnableBeaconJitter", BooleanValue(false),
                         "Ssid", SsidValue(ssidG));
             apDeviceG = wifiG.Install(spectrumWifiPhyG, macG, WifiApNodes.Get(6));
 
             Ptr<WifiNetDevice> APdevG = DynamicCast<WifiNetDevice>(apDeviceG.Get(0));
             Ptr<WifiPhy> AP_phyG = DynamicCast<WifiPhy>(APdevG->GetPhy());
             Ptr<MultiModelSpectrumChannel> channelG =  DynamicCast<MultiModelSpectrumChannel>(AP_phyG->GetChannel());
             Ptr<FriisPropagationLossModel> friisModelG = DynamicCast<FriisPropagationLossModel>(channelG->GetPropagationLossModel());
             friisModelG->SetFrequency(6e9);
         }
 
 
         /* Mobility model */
         MobilityHelper mobility;
         //double diagonalDistance = dAP / std::sqrt(2); // For diagonal APs
         Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
         
         positionAlloc->Add(Vector(0.0, 0.0, 0.0)); // TAP
         positionAlloc->Add(Vector(dAP, 0.0, 0.0));  // AP1
         positionAlloc->Add(Vector(-dAP, 0.0, 0.0));  // AP2
         if (nLinksB == 2 || nLinksB == 3)
         {
             positionAlloc->Add(Vector(0.0, dAP, 0.0));  // AP3
             positionAlloc->Add(Vector(0.0, -dAP, 0.0));  // AP4
         }
         if (nLinksB == 3)
         {
             positionAlloc->Add(Vector(dAP, dAP, 0.0));  // AP5
             positionAlloc->Add(Vector(-dAP, dAP, 0.0));  // AP6
         }
 
         positionAlloc->Add(Vector(dSta, dSta, 0.0));  // TSTA
         positionAlloc->Add(Vector(dSta+dAP, dSta, 0.0));    // STA1
         positionAlloc->Add(Vector(-dAP+dSta,dSta , 0.0));   // STA2
        
         if (nLinksB == 2 || nLinksB == 3)
         {
             positionAlloc->Add(Vector(dSta, dSta+dAP, 0.0));  // StA3
             positionAlloc->Add(Vector(dSta, -dAP+dSta, 0.0));  // STA4
         }
         if (nLinksB == 3)
         {
             positionAlloc->Add(Vector(dAP+dSta, dSta+dAP, 0.0));  // StA5
             positionAlloc->Add(Vector(-dAP+dSta, dAP+dSta, 0.0));  // STA6
         }
 
         mobility.SetPositionAllocator(positionAlloc);
         mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
         mobility.Install(WifiApNodes);
         mobility.Install(wifiStaNodesA);
         mobility.Install(wifiStaNodesB);
         mobility.Install(wifiStaNodesC);
         if (nLinksB == 2 || nLinksB == 3)
         {
             mobility.Install(wifiStaNodesD);
             mobility.Install(wifiStaNodesE);
         }
         if(nLinksB == 3)
         {
             mobility.Install(wifiStaNodesF);
             mobility.Install(wifiStaNodesG);
         }
 
 
         /* Internet stack*/
         InternetStackHelper stack;
         stack.Install(WifiApNodes);
         stack.Install(wifiStaNodesA);
         stack.Install(wifiStaNodesB);
         stack.Install(wifiStaNodesC);
         stack.Install(wifiStaNodesD);
         stack.Install(wifiStaNodesE);
         stack.Install(wifiStaNodesF);
         stack.Install(wifiStaNodesG);
 
         
 
         Ipv4AddressHelper address;
         address.SetBase("192.168.1.0", "255.255.255.0");
 
         Ipv4InterfaceContainer staNodeInterfacesA;
         Ipv4InterfaceContainer staNodeInterfacesB;
         Ipv4InterfaceContainer staNodeInterfacesC;
         Ipv4InterfaceContainer apNodeInterfaceA;
         Ipv4InterfaceContainer apNodeInterfaceB;
         Ipv4InterfaceContainer apNodeInterfaceC;
         Ipv4InterfaceContainer staNodeInterfacesD;
         Ipv4InterfaceContainer staNodeInterfacesE;
         Ipv4InterfaceContainer apNodeInterfaceD;
         Ipv4InterfaceContainer apNodeInterfaceE;
         Ipv4InterfaceContainer staNodeInterfacesF;
         Ipv4InterfaceContainer staNodeInterfacesG;
         Ipv4InterfaceContainer apNodeInterfaceF;
         Ipv4InterfaceContainer apNodeInterfaceG;
 
         staNodeInterfacesA = address.Assign(staDeviceA);
         staNodeInterfacesB = address.Assign(staDeviceB);
         staNodeInterfacesC = address.Assign(staDeviceC);
         if (nLinksB == 2 || nLinksB == 3)
         {
             staNodeInterfacesD = address.Assign(staDeviceD);
             staNodeInterfacesE = address.Assign(staDeviceE);
         }
         if (nLinksB == 3)
         {
             staNodeInterfacesF = address.Assign(staDeviceF);
             staNodeInterfacesG = address.Assign(staDeviceG);
         }
 
         apNodeInterfaceA = address.Assign(apDeviceA);
         apNodeInterfaceB = address.Assign(apDeviceB);
         apNodeInterfaceC = address.Assign(apDeviceC);
         if (nLinksB == 2 || nLinksB == 3)
         {
             apNodeInterfaceD = address.Assign(apDeviceD);
             apNodeInterfaceE = address.Assign(apDeviceE);
         }
         if (nLinksB == 3)
         { 
             apNodeInterfaceF = address.Assign(apDeviceF);
             apNodeInterfaceG = address.Assign(apDeviceG);
         }
 
 
         /* Applications */
         // BSS1
         std::vector<ApplicationContainer> serverSet;
         {
         auto maxRate =  EhtPhy::GetDataRate(mcs, channelWidth, gi, 1) / nStationsT;
         maxRate = 86*4*1000000;
         std::cout << "maxRate " << maxRate/1000000 << std::endl;
 
 
 
         ApplicationContainer serverAppA;
         Ipv4InterfaceContainer serverInterfaces;
         auto serverNodes =  std::ref(wifiStaNodesA);
         for (std::size_t i = 0; i < nStationsT; i++)
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
         for (std::size_t i = 0; i < nStationsT; i++)
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
         const auto maxRate_OBSS =  HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStationsL;
 
         ApplicationContainer serverAppB;
         Ipv4InterfaceContainer serverInterfaces;
         auto serverNodes =  std::ref(wifiStaNodesB);
         
         for (uint8_t i = 0; i < nStationsL; i++) 
         {
             serverInterfaces.Add(staNodeInterfacesB.Get(i));
         }
 
         uint16_t port = 9;
         UdpServerHelper server(port);
         serverAppB = server.Install(serverNodes.get());         
         serverAppB.Start(Seconds(0.0));
         serverAppB.Stop(simulationTime + Seconds(1.0));
         serverSet.push_back(serverAppB);
 
 
         NodeContainer clientNodes;
         clientNodes.Add(WifiApNodes.Get(1));
         double  maxRate_lower_BSS2 = maxRate_OBSS * traffic_load_BBS2;
         const auto packetInterval2 = payloadSize * 8.0 / maxRate_lower_BSS2;  // For calculating the duration of the packet
         //4294967295Uv
         //number_packets = 5000 * 0.1;
 
         // UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
         for (std::size_t i = 0; i < nStationsL; i++)
         {   
             // Ip address and the port of the receiver 
             UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
             client.SetAttribute("MaxPackets", UintegerValue(number_packets));
             client.SetAttribute("Interval", TimeValue(Seconds(packetInterval2)));
             client.SetAttribute("PacketSize", UintegerValue(payloadSize));
             ApplicationContainer clientAppB = client.Install(clientNodes.Get(0));
             //streamNumber += client.AssignStreams(clientNodes.Get(0), streamNumber);
 
             clientAppB.Start(Seconds(1.0));
             clientAppB.Stop(simulationTime + Seconds(1.0));
         }
         }
 
         // BSS3
         {
         const auto maxRate_OBSS3 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStationsL;
 
         ApplicationContainer serverAppC;
         Ipv4InterfaceContainer serverInterfaces;
         auto serverNodes =  std::ref(wifiStaNodesC);
     
         for (uint8_t i = 0; i < nStationsL; i++) 
         {
         serverInterfaces.Add(staNodeInterfacesC.Get(i));
         }
 
 
         uint16_t port = 9;
         UdpServerHelper server(port);
         serverAppC = server.Install(serverNodes.get());         
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
         for (std::size_t i = 0; i < nStationsL; i++)
         {   
             // Ip address and the port of the receiver 
             UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
             client.SetAttribute("MaxPackets", UintegerValue(number_packets));
             client.SetAttribute("Interval", TimeValue(Seconds(packetInterval3)));
             client.SetAttribute("PacketSize", UintegerValue(payloadSize));
             ApplicationContainer clientAppC = client.Install(clientNodes.Get(0));
 
             clientAppC.Start(Seconds(1.0));
             clientAppC.Stop(simulationTime + Seconds(1.0));
         }
         }
 
 
         //BSS4
         if (nLinksB == 2 || nLinksB == 3)
         {   
             {
             const auto maxRate_OBSS4 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStationsL;
 
             ApplicationContainer serverAppD;
             Ipv4InterfaceContainer serverInterfaces;
             auto serverNodes =  std::ref(wifiStaNodesD);
 
             for (uint8_t i = 0; i < nStationsL; i++) 
             {
                 serverInterfaces.Add(staNodeInterfacesD.Get(i));
 
             }
 
 
             uint16_t port = 9;
             UdpServerHelper server(port);
             serverAppD = server.Install(serverNodes.get());         
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
             for (std::size_t i = 0; i < nStationsL; i++)
             {   
                 // Ip address and the port of the receiver 
                 UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
                 client.SetAttribute("MaxPackets", UintegerValue(number_packets));
                 client.SetAttribute("Interval", TimeValue(Seconds(packetInterval4)));
                 client.SetAttribute("PacketSize", UintegerValue(payloadSize));
                 ApplicationContainer clientAppD = client.Install(clientNodes.Get(0));
 
                 clientAppD.Start(Seconds(1.0));
                 clientAppD.Stop(simulationTime + Seconds(1.0));
             }
             }
 
             //BSS5
             {
             const auto maxRate_OBSS5 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStationsL;
 
             ApplicationContainer serverAppE;
             Ipv4InterfaceContainer serverInterfaces;
             auto serverNodes =  std::ref(wifiStaNodesE);
 
             for (uint8_t i = 0; i < nStationsL; i++) 
             {
                 serverInterfaces.Add(staNodeInterfacesE.Get(i));
             }
 
 
             uint16_t port = 9;
             UdpServerHelper server(port);
             serverAppE = server.Install(serverNodes.get());         
             serverAppE.Start(Seconds(0.0));
             serverAppE.Stop(simulationTime + Seconds(1.0));
             serverSet.push_back(serverAppE);
 
             NodeContainer clientNodes;
             clientNodes.Add(WifiApNodes.Get(4));
             double  maxRate_lower_BSS5 = maxRate_OBSS5 * traffic_load_BBS5;
             const auto packetInterval5 = payloadSize * 8.0 / maxRate_lower_BSS5;  // For calculating the duration of the packet
             //4294967295U
             //number_packets = 5000 * 0.1;
 
             // UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
             for (std::size_t i = 0; i < nStationsL; i++)
             {   
                 // Ip address and the port of the receiver 
                 UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
                 client.SetAttribute("MaxPackets", UintegerValue(number_packets));
                 client.SetAttribute("Interval", TimeValue(Seconds(packetInterval5)));
                 client.SetAttribute("PacketSize", UintegerValue(payloadSize));
                 ApplicationContainer clientAppE = client.Install(clientNodes.Get(0));
 
                 clientAppE.Start(Seconds(1.0));
                 clientAppE.Stop(simulationTime + Seconds(1.0));
             }
             }
         }
         if (nLinksB == 3)
         {  
         // BSS6
         {
             const auto maxRate_OBSS6 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStationsL;
 
             ApplicationContainer serverAppF;
             Ipv4InterfaceContainer serverInterfaces;
             auto serverNodes =  std::ref(wifiStaNodesF);
 
             for (uint8_t i = 0; i < nStationsL; i++) 
             {
                 serverInterfaces.Add(staNodeInterfacesF.Get(i));
             }
 
             uint16_t port = 9;
             UdpServerHelper server(port);
             serverAppF = server.Install(serverNodes.get());         
             serverAppF.Start(Seconds(0.0));
             serverAppF.Stop(simulationTime + Seconds(1.0));
             serverSet.push_back(serverAppF);
 
             NodeContainer clientNodes;
             clientNodes.Add(WifiApNodes.Get(5));
             double  maxRate_lower_BSS6 = maxRate_OBSS6 * traffic_load_BBS6;
             const auto packetInterval6 = payloadSize * 8.0 / maxRate_lower_BSS6;  // For calculating the duration of the packet
             //4294967295U
             //number_packets = 5000 * 0.1;
 
             // UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
             for (std::size_t i = 0; i < nStationsL; i++)
             {   
                 // Ip address and the port of the receiver 
                 UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
                 client.SetAttribute("MaxPackets", UintegerValue(number_packets));
                 client.SetAttribute("Interval", TimeValue(Seconds(packetInterval6)));
                 client.SetAttribute("PacketSize", UintegerValue(payloadSize));
                 ApplicationContainer clientAppF = client.Install(clientNodes.Get(0));
 
                 clientAppF.Start(Seconds(1.0));
                 clientAppF.Stop(simulationTime + Seconds(1.0));
             }
             }
 
             //BSS 7
             {
             const auto maxRate_OBSS7 = HePhy::GetDataRate(mcs_OBSS, channelWidth, gi, 1) / nStationsL;
 
             ApplicationContainer serverAppG;
             Ipv4InterfaceContainer serverInterfaces;
             auto serverNodes =  std::ref(wifiStaNodesG);
 
             for (uint8_t i = 0; i < nStationsL; i++) 
             {
                 serverInterfaces.Add(staNodeInterfacesG.Get(i));
             }
 
 
             uint16_t port = 9;
             UdpServerHelper server(port);
             serverAppG = server.Install(serverNodes.get());         
             serverAppG.Start(Seconds(0.0));
             serverAppG.Stop(simulationTime + Seconds(1.0));
             serverSet.push_back(serverAppG);
 
             NodeContainer clientNodes;
             clientNodes.Add(WifiApNodes.Get(6));
             double  maxRate_lower_BSS7 = maxRate_OBSS7 * traffic_load_BBS7;
             const auto packetInterval7 = payloadSize * 8.0 / maxRate_lower_BSS7;  // For calculating the duration of the packet
             //4294967295U
             //number_packets = 5000 * 0.1;
 
             // UDP flow in case of DL, here you installed the AP (to transmit) and logical for it the i now for iteration
             for (std::size_t i = 0; i < nStationsL; i++)
             {   
                 // Ip address and the port of the receiver 
                 UdpClientHelper client(serverInterfaces.GetAddress(i), port);   
                 client.SetAttribute("MaxPackets", UintegerValue(number_packets));
                 client.SetAttribute("Interval", TimeValue(Seconds(packetInterval7)));
                 client.SetAttribute("PacketSize", UintegerValue(payloadSize));
                 ApplicationContainer clientAppG = client.Install(clientNodes.Get(0));
 
                 clientAppG.Start(Seconds(1.0));
                 clientAppG.Stop(simulationTime + Seconds(1.0));
             }
             }
         }
 
             
 
         // Install FlowMonitor 
         FlowMonitorHelper flowmon;
         Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
 
         if (enablePcap)
         {
             spectrumWifiPhyA.EnablePcap("wifi_7_legacy", apDeviceA);
         }
 
         /*Trace sources*/
         Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpClient/TxWithAddresses",
                 MakeCallback(&ClientTxAdd));
 
         Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpServer/RxWithAddresses",
             MakeCallback(&ServerRxAdd));
 
         //uint8_t phyId_psdu_0 = 0;
         //Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phys/" + std::to_string(phyId_psdu_0) + "/PhyRxEnd",
         //            MakeCallback(&PhyRxDoneTrace));
         //uint8_t phyId_psdu_1 = 1;
         //Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phys/" + std::to_string(phyId_psdu_1) + "/PhyRxEnd",
         //            MakeCallback(&PhyRxDoneTrace));
 
         //Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phys/" + std::to_string(phyId_psdu_0) + "/PhyRxDrop",
         //            MakeCallback(&PhyRxDropTrace));
         //Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phys/" + std::to_string(phyId_psdu_1) + "/PhyRxDrop",
         //            MakeCallback(&PhyRxDropTrace));
 
 
 
         Simulator::Stop(simulationTime + Seconds(1.0));
         Simulator::Run();
 
 
         /* Print per flow statistics */
         monitor->CheckForLostPackets ();
         Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
         std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
 
         double through = 0;
         double delay = 0;
         //double txed_packets = 0;
         //double rxed_packets = 0;
         //double lost_packets = 0;
         bool firstuser = true;
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
             //txed_packets = (i->second.txPackets);
             //std::cout << "  txed packets: " << (i->second.txPackets)<< "\n";
             //rxed_packets = (i->second.rxPackets);
             //std::cout << "  rxed packets: " << (i->second.rxPackets)<< "\n";
             //lost_packets = (i->second.lostPackets);
             //std::cout << "  lost packets: " << (i->second.lostPackets)<< "\n";
             //std::cout << std::setw(5) << delay << std::setw(10) << txed_packets << std::setw(13) << rxed_packets
             //        << std::setw(12) << lost_packets << std::setw(15) << through << std::endl;
             if (firstuser)
             {
                 Throughput_analysis[run] = through;
                 Latency_analysis[run] = delay;
                 firstuser = false; 
             }
         }
         //std::cout << "Tx_udp_packets " << Tx_udp_packets << std::endl;
         //std::cout << "Rx_udp_packets " << Rx_udp_packets << std::endl;
         // Print the send and receive time stamps maps 
         //PrintSendTimestamps(sendTimestamps);
         //PrintReceiveTimestamps(receiveTimestamps);
         //CalculateLatencies();
         //PrintEachLatency();
         //PrintAverageLatency();
 
         /*
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
         */
         Simulator::Destroy();
     }
 }
 
 double Final_throughput = 0;
 std::cout << "Throughput" << ":\n";
 for (size_t i = 0; i < Throughput_analysis.size(); ++i) 
 {
     Final_throughput += Throughput_analysis[i] ;
     std::cout << Throughput_analysis[i] << " ";
     std::cout << "\n";
 }
 std::cout << "FinalThroughput " << Final_throughput/ Throughput_analysis.size() << std::endl;
 std::cout << "------------------------\n";
 
 
 double Final_latency = 0;
 std::cout << "Latency" << ":\n";
 for (size_t i = 0; i < Latency_analysis.size(); ++i) 
 {
     Final_latency += Latency_analysis[i] ;
     std::cout << Latency_analysis[i] << " ";
     std::cout << "\n";
 }
 std::cout << "FinalLatency " << Final_latency/ Latency_analysis.size() << std::endl;
 std::cout << "------------------------\n";
 
 
 auto end = std::chrono::high_resolution_clock::now();
 auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
 std::cout << "Simulation took: " << duration.count() << " seconds in real time" << std::endl;
 return 0;
 }   
