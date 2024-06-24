/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 ETH Zuerich
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
 * Author: Patrick Wicki <patrick.wicki@inf.ethz.ch>
 */
#include "src/SCION/model/externs.h"
#include "src/SCION/model/scion-core-as.h"
#include "src/SCION/model/apps/app.h"
#include "src/internet/model/tcp-congestion-ops.h"
#include "src/internet/model/tcp-l4-protocol.h"
// #include "src/internet/model/tcp-cubic.h"
#include "src/internet/model/ipv4-route.h"
#include "src/internet/model/ipv4-static-routing.h"
#include "src/internet/model/tcp-socket-base.h"
#include "src/internet/helper/internet-stack-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4.h"
#include <iomanip>

namespace ns3 {

/**
 * @brief 
 * Common base class for TCP source and sink apps
 */
class TCPApp : public App
{
protected:
  // App config
  bool cfgLogging = false;
  bool cfgPathSwitching = false;

  uint64_t bytes_received = 0;
  uint64_t bytes_sent = 0;

  app_path_id_t active_path;

  ApplicationContainer sourceApps;
  ApplicationContainer sinkApps;

  // What is active and what is inactive must be set by subclass
  ApplicationContainer *active_app;
  ApplicationContainer *inactive_app;

  Ptr<Node> sender;
  Ptr<Node> receiver;

  // References to the layer 4 protocol handlers where we inject our L3 callback or received packets
  Ptr<TcpL4Protocol> sender_l4;
  Ptr<TcpL4Protocol> receiver_l4;

  // The source and sink subclasses must set this pointer the l4 of sender and sink respectively
  Ptr<TcpL4Protocol> layer4;

  void
  SendScionPacket (Ptr<Packet> ip_packet)
  {
    Payload payload = AppData{
        .app_id = app_id,
        .path_id = active_path,
        .seq_no = 0,
        .frame_no = 0,
        .timestamp = Simulator::Now ().GetMicroSeconds (),
        .ip_packet = ip_packet,
    };

    PayloadType payload_type = PayloadType::APPLICATION_DATA;
    host->SendAppPacket (this, payload, payload_type,
                         ip_packet->GetSize () * scale + sizeof (AppData), all_paths[active_path]);
  }

  /**
   * Callback function to intercept packets passed to layer 3 by TCP and instead
   * send them via SCION
   */
  void
  CallbackSndL4ToScion (Ptr<Packet> packet, Ipv4Address source, Ipv4Address destination,
                        uint8_t protocol, Ptr<Ipv4Route> route)
  {
    Log ("Encapsulating IP L3 packet via SCION:", true, false);

    TcpHeader incomingTcpHeader;
    packet->PeekHeader (incomingTcpHeader);
    auto tcp_payload_size = packet->GetSize () - incomingTcpHeader.GetLength () * 4;

    std::cout << " seq " << incomingTcpHeader.GetSequenceNumber () << " ack "
              << incomingTcpHeader.GetAckNumber () << " flags "
              << TcpHeader::FlagsToString (incomingTcpHeader.GetFlags ()) << " payload size "
              << tcp_payload_size << std::endl;

    // Add IPv4 header before embedding into a SCION packet
    Ipv4Header header;
    header.SetSource (source);
    header.SetDestination (destination);
    header.SetProtocol (protocol);
    header.SetTtl (254);
    header.SetPayloadSize (packet->GetSize ());

    packet->AddHeader (header);
    SendScionPacket (packet);

    bytes_sent += packet->GetSize ();
  }

  /**
   * @brief Manually create a TCP socket base
   * 
   */
  void
  SetupTCPSocket ()
  {
    int r;
    std::cout << "TCP: Socket create" << std::endl;
    Ptr<ns3::TcpSocketBase> socket = CreateObject<ns3::TcpSocketBase> ();

    // Ptr<ns3::TcpCongestionOps> congestion_ops = CreateObject<ns3::TcpCubic> ();
    // socket->SetCongestionControlAlgorithm (congestion_ops);

    std::cout << "TCP: Alg set" << std::endl;

    Ptr<TcpL4Protocol> l4 = new TcpL4Protocol ();
    l4->SetDownTarget (MakeCallback (&TCPApp::CallbackSndL4ToScion, this));
    socket->SetTcp (l4);

    std::cout << "TCP Socket: L4 set" << std::endl;

    Ptr<Node> node = CreateObject<Node> ();
    node->GetObject<Ipv4> ()->SetRoutingProtocol (CreateObject<Ipv4StaticRouting> ());
    socket->SetNode (node);

    r = socket->Bind ();
    if (r == -1)
      {
        std::cout << "TCP: Error binding" << std::endl;
      }
    else
      {
        std::cout << "TCP: Bound" << std::endl;
      }
    Ipv4Address a = Ipv4Address ("172.16.1.1");
    Address addr = InetSocketAddress (a, 80);
    r = socket->Connect (addr);
    // r = socket->DoConnect();
    if (r == -1)
      {
        std::cout << "TCP: Error connecting" << std::endl;
      }
    else
      {

        std::cout << "TCP: Connected" << std::endl;
      }

    // Try sending a dummy packet for now
    Ptr<ns3::Packet> packet = new Packet (1024);
    r = socket->Send (packet, 0);

    if (r == -1)
      {
        std::cout << "TCP: Error sending packet" << std::endl;
      }
    else
      {
        std::cout << "TCP: Packet sent" << std::endl;
      }
  }

  void
  SetupInetStack ()
  {
    NodeContainer nodes;
    nodes.Create (2);

    // These values don't matter because the packets never actually traverse
    // this fake network
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install (nodes);

    InternetStackHelper stackHelper;
    stackHelper.Install (nodes);

    Ipv4AddressHelper addressHelper;
    addressHelper.NewNetwork ();
    // Get a random number from 0 to 255
    std::string a = std::to_string (rand () % 256);
    // Ipv4Address address = ("10.1." + "1" + ".0").c_str();
    addressHelper.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addressHelper.Assign (devices);

    uint16_t port = 9; // Well-known echo port number
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory",
                                 InetSocketAddress (interfaces.GetAddress (1), port));
    sourceHelper.SetAttribute ("MaxBytes", UintegerValue (0));
    sourceApps = sourceHelper.Install (nodes.Get (0));

    // TODO: Adding a custom callback here doesn't seem to work
    // sourceApps.Get (0)->TraceConnectWithoutContext ("Tx", MakeCallback (&ns3::TCPSender::SendViaScion, this));

    // Access the Internet stack of sender node to get the TCP protocol instance
    sender = nodes.Get (0);
    Ptr<Ipv4> ipv4;
    ipv4 = sender->GetObject<Ipv4> ();
    if (ipv4)
      {
        Ptr<IpL4Protocol> ipL4Protocol = ipv4->GetProtocol (6); // 6 is the protocol number for TCP
        Ptr<TcpL4Protocol> tcp = DynamicCast<TcpL4Protocol> (ipL4Protocol);
        if (tcp)
          {
            // HACK: Override with a custom callback to send packets via SCION
            // instead of passing them on to layer 3
            sender_l4 = tcp;
            sender_l4->SetDownTarget (MakeCallback (&TCPApp::CallbackSndL4ToScion, this));
          }
      }

    // Print the type of congestion control algorithm used
    // Ptr<TcpSocketBase> socket = DynamicCast<TcpSocketBase> (sourceApps.Get (0));
    // Ptr<TcpCongestionOps> congestion_ops = socket->GetCongestionControlAlgorithm ();
    // std::cout << "Congestion control algorithm: " << congestion_ops->GetName () << std::endl;

    PacketSinkHelper sink ("ns3::TcpSocketFactory",
                           InetSocketAddress (Ipv4Address::GetAny (), port));
    sinkApps = sink.Install (nodes.Get (1));

    receiver = nodes.Get (1);
    ipv4 = receiver->GetObject<Ipv4> ();
    if (ipv4)
      {
        Ptr<IpL4Protocol> ipL4Protocol = ipv4->GetProtocol (6); // 6 is the protocol number for TCP
        Ptr<TcpL4Protocol> tcp = DynamicCast<TcpL4Protocol> (ipL4Protocol);
        if (tcp)
          {
            // HACK: Override with a custom callback to send packets via SCION
            // instead of passing them on to layer 3
            receiver_l4 = tcp;
            receiver_l4->SetDownTarget (MakeCallback (&TCPApp::CallbackSndL4ToScion, this));
          }
      }

    std::cout << "Inet Stack setup finished" << std::endl;
  }

  void
  InitC ()
  {
    //
    // Explicitly create the nodes required by the topology (shown above).
    //
    NS_LOG_INFO ("Create nodes.");
    NodeContainer nodes;
    nodes.Create (2);

    NS_LOG_INFO ("Create channels.");

    //
    // Explicitly create the point-to-point link required by the topology (shown above).
    //
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("500Kbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("5ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install (nodes);

    //
    // Install the internet stack on the nodes
    //
    InternetStackHelper internet;
    internet.Install (nodes);

    //
    // We've got the "hardware" in place.  Now we need to add IP addresses.
    //
    NS_LOG_INFO ("Assign IP Addresses.");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign (devices);

    NS_LOG_INFO ("Create Applications.");

    //
    // Create a BulkSendApplication and install it on node 0
    //
    uint16_t port = 9; // well-known echo port number

    BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (i.GetAddress (1), port));
    // Set the amount of data to send in bytes.  Zero is unlimited.
    source.SetAttribute ("MaxBytes", UintegerValue (0));
    ApplicationContainer sourceApps = source.Install (nodes.Get (0));
    sourceApps.Start (Seconds (0.0));
    sourceApps.Stop (Seconds (10.0));

    //
    // Create a PacketSinkApplication and install it on node 1
    //
    PacketSinkHelper sink ("ns3::TcpSocketFactory",
                           InetSocketAddress (Ipv4Address::GetAny (), port));
    sinkApps = sink.Install (nodes.Get (1));
    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (10.0));
  }

  void
  Log (std::string msg, bool with_prefix = true, bool newline = true)
  {
    if (!cfgLogging)
      {
        return;
      }

    std::string prefix;
    if (with_prefix)
      {
        prefix += "[" + std::to_string (Simulator::Now ().ToDouble (Time::Unit::MIN)) + "][" +
                  InfoString () + "-" + std::to_string (app_id) + "] ";
      }

    if (newline)
      {
        std::cout << prefix << msg << std::endl;
      }
    else
      {
        std::cout << prefix << msg;
      }
  }

public:
  TCPApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
          host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
          uint32_t runtime_config)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, runtime_config)
  {
    std::cout << InfoString () << " initializing..." << std::endl;
    // Setup the runtime configuration
    cfgLogging = ENABLE_LOGGING (runtime_config);
    cfgPathSwitching = !(DISABLE_PATH_SWITCHING (runtime_config));

    SetupInetStack ();

    active_path = 0;

    std::cout << InfoString () << " initialized." << std::endl;
  }

  virtual std::string
  InfoString ()
  {
    return "TCP-App";
  }

  /**
   * Inject a packet received via SCION directly into layer 4 of the inet stack
   * of the receiver
   */
  void
  RcvScionToInetL4 (Ptr<Packet> packet)
  {
    uint64_t ip_pkt_size = packet->GetSize ();
    // Peel off IP header
    Ipv4Header ipHeader;
    packet->RemoveHeader (ipHeader);

    TcpHeader incomingTcpHeader;
    packet->PeekHeader (incomingTcpHeader);

    // Subtract number of tcp header words (4 bytes each) to get the payload size
    auto tcp_payload_size = packet->GetSize () - incomingTcpHeader.GetLength () * 4;

    Log ("Receiving IP L3 packet via SCION:    ", true, false);
    std::cout << " seq " << incomingTcpHeader.GetSequenceNumber () << " ack "
              << incomingTcpHeader.GetAckNumber () << " flags "
              << TcpHeader::FlagsToString (incomingTcpHeader.GetFlags ()) << " payload size "
              << tcp_payload_size << std::endl;

    // TODO: We could even set ECN according to the SCION packet ECN
    // information, but this would require checking for support at time of
    // creation of the SCION packet
    ipHeader.SetEcn (Ipv4Header::EcnType::ECN_NotECT);

    // Deliver TCP packet to layer 4
    layer4->Receive (packet, ipHeader, Ptr<Ipv4Interface> ());
    bytes_received += ip_pkt_size;
  }

  void
  StartAppTraffic ()
  {
    stopped = false;
    active_app->Start (Simulator::Now ());
    active_app->Stop (Time::Max());
  }

  void
  StopAppTraffic ()
  {
    stopped = true;
    active_app->Stop (Simulator::Now ());
  }
};

class TCPSource : public TCPApp
{
public:
  TCPSource (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
             host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
             uint32_t runtime_config)
      : TCPApp (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, runtime_config)
  {
    layer4 = sender_l4;
    active_app = &sourceApps;
    inactive_app = &sinkApps;
    inactive_app->Start (Time::Max ());
  }

  std::string
  InfoString ()
  {
    return "TCP-Source";
  }

  void
  PrintResults ()
  {
    std::cout << "Results for " << app_id << std::endl;
    std::cout << "  Total Bytes Sent     (Layer 3): " << bytes_sent << std::endl;
    std::cout << "  Total Bytes Received (Layer 3): " << bytes_received << std::endl;
  }
};

class TCPSink : public TCPApp
{
public:
  TCPSink (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
           host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
           uint32_t runtime_config)
      : TCPApp (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, runtime_config)
  {
    layer4 = receiver_l4;
    active_app = &sinkApps;
    inactive_app = &sourceApps;
    inactive_app->Start (Time::Max ());
  }

  std::string
  InfoString ()
  {
    return "TCP-Sink";
  }

  void
  PrintResults ()
  {
    std::cout << "Results for " << app_id << std::endl;
    std::cout << "  Total Bytes Sent     (Layer 3): " << bytes_sent << std::endl;
    std::cout << "  Total Bytes Received (Layer 3): " << bytes_received << std::endl;
    Ptr<PacketSink> sink = DynamicCast<PacketSink> (active_app->Get (0));
    std::cout << "  Total Bytes Received (Layer 4): " << sink->GetTotalRx () << std::endl;
  }
};

} // namespace ns3
