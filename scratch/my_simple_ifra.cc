/*
 * Copyright (c) 2009 The Boeing Company
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
 */

// This script configures two nodes on an 802.11b physical layer, with
// 802.11b NICs in infrastructure mode, and by default, the station sends
// one packet of 1000 (application) bytes to the access point.  Unlike
// the default physical layer configuration in which the path loss increases
// (and the received signal strength decreases) as the distance between the
// nodes increases, this example uses an artificial path loss model that
// allows the configuration of the received signal strength (RSS) regardless
// of other transmitter parameters (such as transmit power) or distance.
// Therefore, changing position of the nodes has no effect.
//
// There are a number of command-line options available to control
// the default behavior.  The list of available command-line options
// can be listed with the following command:
// ./ns3 run "wifi-simple-infra --help"
// Additional command-line options are available via the generic attribute
// configuration system.
//
// For instance, for the default configuration, the physical layer will
// stop successfully receiving packets when rss drops to -82 dBm or below.
// To see this effect, try running:
//
// ./ns3 run "wifi-simple-infra --rss=-80 --numPackets=20"
// ./ns3 run "wifi-simple-infra --rss=-81 --numPackets=20"
// ./ns3 run "wifi-simple-infra --rss=-82 --numPackets=20"
//
// The last command (and any RSS value lower than this) results in no
// packets received.  This is due to the preamble detection model that
// dominates the reception performance.  By default, the
// ThresholdPreambleDetectionModel is added to all WifiPhy objects, and this
// model prevents reception unless the incoming signal has a RSS above its
// 'MinimumRssi' value (default of -82 dBm) and has a SNR above the
// 'Threshold' value (default of 4).
//
// If we relax these values, we can instead observe that signal reception
// due to the 802.11b error model alone is much lower.  For instance,
// setting the MinimumRssi to -101 (around the thermal noise floor).
// and the SNR Threshold to -10 dB, shows that the DsssErrorRateModel can
// successfully decode at RSS values of -97 or -98 dBm.
//
// ./ns3 run "wifi-simple-infra --rss=-97 --numPackets=20
// --ns3::ThresholdPreambleDetectionModel::Threshold=-10
// --ns3::ThresholdPreambleDetectionModel::MinimumRssi=-101"
// ./ns3 run "wifi-simple-infra --rss=-98 --numPackets=20
// --ns3::ThresholdPreambleDetectionModel::Threshold=-10
// --ns3::ThresholdPreambleDetectionModel::MinimumRssi=-101"
// ./ns3 run "wifi-simple-infra --rss=-99 --numPackets=20
// --ns3::ThresholdPreambleDetectionModel::Threshold=-10
// --ns3::ThresholdPreambleDetectionModel::MinimumRssi=-101"

//
// Note that all ns-3 attributes (not just the ones exposed in the below
// script) can be changed at command line; see the documentation.
//
// This script can also be helpful to put the Wifi layer into verbose
// logging mode; this command will turn on all wifi logging:
//
// ./ns3 run "wifi-simple-infra --verbose=1"
//
// When you are done, you will notice two pcap trace files in your directory.
// If you have tcpdump installed, you can try this:
//
// tcpdump -r wifi-simple-infra-0-0.pcap -nn -tt
//

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiSimpleInfra");

// This first part is used for doxygen documentation
/**
 * Function called when a packet is received.
 *
 * \param socket The receiving socket.
 */

// void specifies that the function doesnot return a value
// there is a Ptr<Socket> socket: This is the parameter of the function, which is a smart pointer (Ptr) to an ns3::Socket object. T
//The Ptr<Socket> type is used in ns-3 for automatic memory management, ensuring that the object it points to is properly managed and deleted when no longer needed.
// std::cout: This is the standard output stream in C++.
// << "Received one packet!" << std::endl: This part of the statement sends the string "Received one packet!" followed by an end-of-line character to the standard output (usually the console). 
// This line is executed every time a packet is successfully received, providing a simple way to log or indicate that a packet has been received.
void
ReceivePacket(Ptr<Socket> socket)
{
    while (socket->Recv())
    {
        std::cout << "Received one packet!" << std::endl;
    }
}

/**
 * Generate traffic.
 *
 * \param socket The sending socket.
 * \param pktSize The packet size.
 * \param pktCount The packet count.
 * \param pktInterval The interval between two packets.
 */

// static: This means that the function is limited in scope to the file in which it is declared, meaning it cannot be accessed from other files.
// void: The function does not return any value.
//Ptr<Socket> socket: A smart pointer to a Socket object used for sending packets.
// 
static void
GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval)
{
    
    if (pktCount > 0)
    {
        NS_LOG_INFO("Generating one packet of size " << pktSize);
        socket->Send(Create<Packet>(pktSize));
        // This schedules an event to be occured in the future simulation time.
        // pktInterval: This specifies the time interval after which the event should occur.
// &GenerateTraffic: This is a pointer to the GenerateTraffic function itself, meaning that the function will call itself recursively.
//socket, pktSize, pktCount - 1, pktInterval: These are the arguments passed to the next invocation of the GenerateTraffic function.
//socket: The same socket used for sending packets.
//pktSize: The size of the packets to be sent.
//pktCount - 1: Decrements the packet count by 1, meaning one less packet needs to be sent in the next call.
//pktInterval: The same interval between packets.
        Simulator::Schedule(pktInterval,
                            &GenerateTraffic,
                            socket,
                            pktSize,
                            pktCount - 1,
                            pktInterval);
    }
    else
    {
        socket->Close();
    }
}

int
main(int argc, char* argv[])
{
    std::string phyMode("DsssRate1Mbps");
    double rss = -80;           // -dBm
    uint32_t packetSize = 1000; // bytes
    uint32_t numPackets = 1;
    Time interval = Seconds(1.0);
    bool verbose = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
    cmd.AddValue("rss", "received signal strength", rss);
    cmd.AddValue("packetSize", "size of application packet sent", packetSize);
    cmd.AddValue("numPackets", "number of packets generated", numPackets);
    cmd.AddValue("interval", "interval between packets", interval);
    cmd.AddValue("verbose", "turn on all WifiNetDevice log components", verbose);
    cmd.Parse(argc, argv);

    // Fix non-unicast data rate to be the same as that of unicast
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(phyMode));



    // Create 2 nodes 
    NodeContainer c;
    c.Create(2);



// WifiHelper is the netdevice of the wifi so it creates the wifinetdevice objects
// change the standard to the string you found to check it 
// In addition change the loging component to include also the application to see the packets as usual 
// The below set of helpers will help us to put together the wifi NICs we want
    WifiHelper wifi;
    if (verbose)
    {
        //WifiHelper::EnableLogComponents(); // Turn on all Wifi logging
        LogComponentEnable("WifiPhy", LOG_INFO);
        //LogComponentEnable("WifiMac", LOG_LEVEL_ALL);
        //LogComponentEnable("wifiChannel", LOG_LEVEL_ALL);
        //LogComponentEnable("WifiHelper", LOG_LEVEL_ALL);
    }
    wifi.SetStandard(WIFI_STANDARD_80211b);





//    physical object creation
    YansWifiPhyHelper wifiPhy;
    // This is one parameter that matters when using FixedRssLossModel
    // set it to zero; otherwise, gain will be added
    wifiPhy.Set("RxGain", DoubleValue(0));
    // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);




// channel creation

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    // The below FixedRssLossModel will cause the rss to be fixed regardless
    // of the distance between the two stations, and the transmit power
    wifiChannel.AddPropagationLoss("ns3::FixedRssLossModel", "Rss", DoubleValue(rss));
//channel is created on the phy layer
    wifiPhy.SetChannel(wifiChannel.Create());




// Add a mac and disable rate control
// How to know the attributes to be added to the SetRemoteStationManager, y3ny I checked the SetRemoteStationManager function in wifihelper class and checked the .CC and .h files for 
// both of them still don't know where to find the attributes 
// he created one device for the ap and the station
    WifiMacHelper wifiMac;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(phyMode),
                                 "ControlMode",
                                 StringValue(phyMode));

    // Setup the rest of the MAC
    Ssid ssid = Ssid("wifi-default");
    // setup STA
    wifiMac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevice = wifi.Install(wifiPhy, wifiMac, c.Get(0));
    NetDeviceContainer devices = staDevice;
    // setup AP
    wifiMac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(wifiPhy, wifiMac, c.Get(1));
    devices.Add(apDevice);




// Note that with FixedRssLossModel, the positions below are not
// used for received signal strength.
    MobilityHelper mobility;

 //   ListPositionAllocator is a class 
 // When shall I use the  class by creating an object from it or by having a poiunter like that ListPositionAllocator pos = great
 //This setup positions two nodes at (0.0, 0.0, 0.0) and (5.0, 0.0, 0.0) respectively, sets their mobility model to constant positions, configures a Wi-Fi network
 // Ptr<ListPositionAllocator>: This declares a smart pointer (Ptr) to an object of type ListPositionAllocator. Smart pointers in ns-3 manage the lifetime of objects, 
 // ensuring they are properly deleted when no longer in use.
 //CreateObject<T>() is a template function used for creating instances of ns-3 objects. It is part of the ns-3 object creation and memory management framework,
 //  which simplifies the creation and management of objects in a simulation.
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    positionAlloc->Add(Vector(5.0, 0.0, 0.0));
 // In ns3  The Add method of this class (or you can apply it in any class) which have methods is used to add positions to the list of positions managed by the allocator.
 //  There are two ways to call this method, which are functionally equivalent:
// ns3::ListPositionAllocator::Add(Vector v) or positionAlloc->Add(Vector v)
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(c);







// Here, starting the uppper layers, intenret stack, ip address to the net devices.
    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);



 // Finally the application layer and its containers which generates the traffic
//  Here instead of using application layers he did something else, I will explain it using to function he implemented
 // He used the socket class to vreate the sockets which will generate packets using The provided code sets up communication using UDP sockets in an ns-3 simulation.

    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(c.Get(0), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));


    Ptr<Socket> source = Socket::CreateSocket(c.Get(1), tid);
    InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
    source->SetAllowBroadcast(true);
    source->Connect(remote);




    // Tracing
    wifiPhy.EnablePcap("wifi-simple-infra", devices);



    // Output what we are doing
    std::cout << "Testing " << numPackets << " packets sent with receiver rss " << rss << std::endl;


// This is the Simulator::ScheduleWithContext: This is a method provided by ns-3's Simulator class, used to schedule an event to occur at a specified simulation time, with a given context.
    Simulator::ScheduleWithContext(source->GetNode()->GetId(),
                                   Seconds(1.0),
                                   &GenerateTraffic,
                                   source,
                                   packetSize,
                                   numPackets,
                                   interval);

    Simulator::Stop(Seconds(2.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
