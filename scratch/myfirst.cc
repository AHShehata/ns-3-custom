/*
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

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"

// Default Network Topology
//
//       10.1.1.0
// n0 -------------- n1
//    point-to-point
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FirstScriptExample");


/*
void RxTrace(std::string context, Ptr<const Packet> pkt, const Address& a, const Address& b){
      std::cout<< context <<std::endl;
      std::cout<< "\tRxTrace: size = " << pkt->GetSize()
               << " From Address: " << InetSocketAddress::ConvertFrom (a).GetIpv4 ()
               << " Local Address: " << InetSocketAddress::ConvertFrom (b).GetIpv4 () << std::endl;

}
*/


void MacTxTrace(std::string context, Ptr<const Packet> pkt){
      std::cout<< context <<std::endl;
      std::cout<< "\t tTrace: size = " << pkt->GetSize()
               << " time: " << Simulator::Now() << std::endl;

}


void MacRxTrace(std::string context, Ptr<const Packet> pkt){
      std::cout<< context <<std::endl;
      std::cout<< "\t RxTrace: size = " << pkt->GetSize() << " time: " << Simulator::Now() << std::endl;
      PppHeader hdr;
      if(pkt->PeekHeader(hdr)) //desrialize header from the packet and save it in this generated hdr but without removing it 
      {
        std::cout << "PPP header size: " << hdr.GetSerializedSize() << std::endl;
      }

}


void EnqueuTrace(std::string context, Ptr<const Packet> pkt){
      std::cout<< context << " time: "  << Simulator::Now() << " Packet of size " <<pkt->GetSize() << " enqueued!" <<std::endl;
}

void DequeueTrace(std::string context, Ptr<const Packet> pkt){
      std::cout<< context 
      << " time: "  << Simulator::Now() << " Packet of size " <<pkt->GetSize() << " dequeued!" << std::endl;
}

void BreakIt (uint32_t n)
{
  std::cout << "inside BreakIt" << std::endl;
  double x = 1/(n-2);
  std::cout << x << std::endl;
}


int main(int argc, char* argv[])
{ 

    uint32_t nPackets = 3;
    uint32_t number_nodes = 2;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numPackets", "Number of packets to echo", nPackets);
    cmd.AddValue("number_nodes", "Number of nodes", number_nodes);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);
    //LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO );
    //LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    NS_LOG_INFO("Creating Topology");
    NodeContainer nodes;
    nodes.Create(number_nodes);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    UdpEchoServerHelper echoServer(9);

    ApplicationContainer serverApps = echoServer.Install(nodes.Get(1));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));

    UdpEchoClientHelper echoClient(interfaces.GetAddress(1), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(nPackets));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(0.0001)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));

// I am going to add a code for network animation using the software called NetAnim
// Netanim is a folder comes away with ns3, There is a  file called netanim-3.109
// in cd /home/ashehata/ns-allinone-3.4 and you will find there netanim-3.109, go to it
// I still have a problem with opening the animation window 
AnimationInterface anim("first.xml");
anim.SetConstantPosition(nodes.Get(0), 10.0,10.0);
anim.SetConstantPosition(nodes.Get(1), 20.0,20.0);



//Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/RxWithAddresses", MakeCallback(&RxTrace));

//config::connect("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/TxWithAddresses", MakeCallback(&TxTrace));
//Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacTx", MakeCallback(&MacTxTrace));


// He tries something very intesresting using this trace 
// He reduces alot the interval of receving the packets in the queue from the udp application layer from 1 seconds to 
// So by that he let the packets stay a little bit in the queue before they are queued where now you will 
// have time difference between the packet queuing and dequeuing which is the queuing latency.
Config::Connect("/NodeList/*/DeviceList/*/TxQueue/Enqueue", MakeCallback(&EnqueuTrace));
Config::Connect("/NodeList/*/DeviceList/*/TxQueue/Dequeue", MakeCallback(&DequeueTrace));


// Next step is that you can tarce, using tracemetrices in Ascii Trace format 
// In that case you will print the logs in the ascii format
// And you check which object will you trace from the helper files
// you generate a .tr file and you can open it using gedit first.tr
AsciiTraceHelper ascii;
pointToPoint.EnableAsciiAll(ascii.CreateFileStream("first.tr"));
// you can also generate tracefiles in the .pcap format. (This is the packet capture format), the most popular program that can read and display such a kind of format is 
// the Wireshark (formerly know as Ethereal). this is a graphical user interface while you also can use tcpdump command lines if you are not able to dowload the wireshark.
// and you can open it like that wireshark myfirst-1-0.pcap
pointToPoint.EnablePcapAll("myfirst");

Simulator::Schedule(Seconds(0.5), &BreakIt, number_nodes);
Simulator::Run();
Simulator::Destroy();
return 0;
}
