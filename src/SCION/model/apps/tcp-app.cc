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
#include <regex>

namespace ns3 {

/**
 * @brief 
 * Common base class for TCP source and sink apps
 */
class TCPApp : public App
{
protected:
  // Struct to store application state for visualization
  struct TCPAppState
  {
    Time timestamp;
    double sendrate = 0; // in Bytes/s
    double fair_share = 0; // in Bytes/s
    double latency = 0;
    double loss = 0;
    uint32_t active_path;
  };

  // App config
  bool cfgLogging = false;
  bool cfgPathSwitching = false;

  std::string app_type;
  std::string tcp_alg;
  bool is_sink = false;

  uint64_t bytes_received = 0;
  uint64_t bytes_sent = 0;

  app_path_id_t active_path;

  ApplicationContainer app;
  Ptr<TcpL4Protocol> tcpLayer4;

  // Things used for state tracking and, ultimately, visualization
  std::vector<TCPAppState> state_history;
  uint32_t seq_no = 0; // SCION layer sequence number, for receiver to count packets and loss
  uint64_t bytes_received_this_window = 0;
  uint64_t bytes_sent_this_window = 0;
  AppResp last_report;
  Time last_report_time = Time::Min ();

  void
  SendScionPacket (Ptr<Packet> ip_packet)
  {
    Payload payload = AppData{
        .app_id = app_id,
        .path_id = active_path,
        .seq_no = seq_no++,
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
    if (stopped)
      {
        Log ("Attempted to send packet while stopped", true, true);
        return;
      }

    Log ("Encapsulating IP L3 packet via SCION:", true, false);

    TcpHeader incomingTcpHeader;
    packet->PeekHeader (incomingTcpHeader);
    auto tcp_payload_size = packet->GetSize () - incomingTcpHeader.GetLength () * 4;

    Log (" seq " + std::to_string (incomingTcpHeader.GetSequenceNumber ().GetValue ()) + " ack " +
             std::to_string (incomingTcpHeader.GetAckNumber ().GetValue ()) + " flags " +
             TcpHeader::FlagsToString (incomingTcpHeader.GetFlags ()) + " payload size " +
             std::to_string (tcp_payload_size),
         false);

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
    bytes_sent_this_window += packet->GetSize ();
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
  SetupIPStack ()
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
    // TODO: Requires a hack currently (disabling the check) because the
    // addresshelper is a singleton and will cause a fatal error on collisions
    addressHelper.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addressHelper.Assign (devices);

    uint16_t port = 9; // Well-known echo port number

    // "Transport protocol to use: TcpNewReno, "
    //             "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
    //             "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "

    Ptr<Node> node;
    if (!is_sink) // We're a sender
      {
        BulkSendHelper sourceHelper ("ns3::TcpSocketFactory",
                                     InetSocketAddress (interfaces.GetAddress (1), port));

        // Send as much as possible
        sourceHelper.SetAttribute ("MaxBytes", UintegerValue (0));
        ApplicationContainer sourceApps = sourceHelper.Install (nodes.Get (0));

        // NOTE: Attempt to override send callback, doesn't work
        // sourceApps.Get (0)->TraceConnectWithoutContext ("Tx", MakeCallback (&ns3::TCPSender::SendViaScion, this));

        node = nodes.Get (0);
        app = sourceApps;
      }
    else // We're a sink
      {
        PacketSinkHelper sink ("ns3::TcpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), port));
        ApplicationContainer sinkApps = sink.Install (nodes.Get (1));

        node = nodes.Get (1);
        app = sinkApps;
      }

    Ptr<Ipv4> ipv4Api = node->GetObject<Ipv4> ();
    if (ipv4Api)
      {
        // Retrieve the instance of the TCP L4 protocol
        Ptr<IpL4Protocol> ipL4Protocol = ipv4Api->GetProtocol (6); // 6 = TCP
        tcpLayer4 = DynamicCast<TcpL4Protocol> (ipL4Protocol);
        if (tcpLayer4)
          {
            // HACK: Override with a custom callback to send packets via SCION
            // instead of passing them on to layer 3
            tcpLayer4->SetDownTarget (MakeCallback (&TCPApp::CallbackSndL4ToScion, this));

            // NOTE: Attempt to override congestion algorithm below, doesn't work either
            // TypeId tcpTypeId = TypeId::LookupByName ("ns3::TcpVegas");
            // tcpLayer4->SetAttribute ("TypeId", TypeIdValue (tcpTypeId));
          }
      }

    // NOTE: Attempt to override congestion algorithm below, doesn't work
    // Ptr<TcpSocketBase> socket = DynamicCast<TcpSocketBase> (sourceApps.Get (0));

    // ObjectFactory congestionAlgorithmFactory;
    // congestionAlgorithmFactory.SetTypeId (TcpNewReno::GetTypeId ());
    // Ptr<TcpCongestionOps> algo = congestionAlgorithmFactory.Create<TcpCongestionOps> ();
    // socket->SetCongestionControlAlgorithm (algo);

    // ObjectFactory recoveryAlgorithmFactory;
    // recoveryAlgorithmFactory.SetTypeId (TcpClassicRecovery::GetTypeId ());
    // Ptr<TcpRecoveryOps> recovery = recoveryAlgorithmFactory.Create<TcpRecoveryOps> ();
    // socket->SetRecoveryAlgorithm (recovery);

    Log ("IP stack setup complete");
  }

  // Setup stack and applications according to the ns3 docs
  void
  InitFromExample ()
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
    ApplicationContainer sinkApps = sink.Install (nodes.Get (1));
    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (10.0));
  }

  void
  TrackState ()
  {
    if (stopped)
      {
        return;
      }

    double window_duration_s = (Simulator::Now () - last_report_time).GetSeconds ();

    // Store current state of the app
    TCPAppState state;
    state.timestamp = Simulator::Now ();
    state.active_path = active_path;
    state.sendrate = bytes_sent_this_window / window_duration_s;
    state.latency = last_report.avg_latency;
    state.loss = last_report.loss;
    state.fair_share = 1e6; // TODO
    state_history.push_back (state);

    bytes_sent_this_window = 0;
    last_report_time = Simulator::Now ();
  }

  std::string
  FancyPrettyPrintJSON (const nlohmann::json &jsonObj)
  {
    // Overkill function to pretty print the JSON but with the inner most
    // objects on the same line, which makes the output a bit more readable

    std::string prettyPrinted = jsonObj.dump (4); // Indentation of 4 spaces
    std::regex innermostObjPattern (R"(\{\n(\s+"[^"]+": [^,\n]+,\n)+\s+"[^"]+": [^,\n]+\n\s+\})");
    std::string result;
    std::sregex_iterator currentMatch (prettyPrinted.begin (), prettyPrinted.end (),
                                       innermostObjPattern);
    std::sregex_iterator lastMatch;

    // Position of the last match to handle text after the last match
    size_t lastMatchEnd = 0;

    while (currentMatch != lastMatch)
      {
        std::smatch match = *currentMatch;
        std::string matchedStr = match.str ();

        // Append text before the current match
        result += prettyPrinted.substr (lastMatchEnd, match.position () - lastMatchEnd);

        // Process and append the current match
        matchedStr = std::regex_replace (matchedStr, std::regex (R"(\n\s+)"), " ");
        result += matchedStr;

        // Update the position of the last match
        lastMatchEnd = match.position () + match.length ();
        currentMatch++;
      }

    // Append remaining text after the last match
    result += prettyPrinted.substr (lastMatchEnd);

    return result;
  }

  void
  PrintResultsJSON ()
  {
    nlohmann::json j;
    j["app_id"] = app_id;
    j["app_type"] = app_type;
    j["src_ia"] = ia_addr;
    j["dst_ia"] = dst_ia;
    j["dst_host_addr"] = dst_host_addr;
    j["bytes_sent"] = bytes_sent;
    j["bytes_received"] = bytes_received;

    nlohmann::json j_states;
    for (TCPAppState state : state_history)
      {
        nlohmann::json j_state;
        j_state["time"] = state.timestamp.ToDouble (Time::Unit::MS);
        j_state["sendrate"] = state.sendrate / 1e6;
        j_state["latency"] = state.latency / 1000.0;
        j_state["loss"] = state.loss;
        j_state["active_path"] = state.active_path;
        j_state["fair_share"] = state.fair_share / 1e6;
        j_states.push_back (j_state);
      }
    j["states"] = j_states;

    // Dump JSON into a single line
    std::cout << j.dump () << std::endl;
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
                  app_type + "-" + std::to_string (app_id) + "] ";
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
          uint32_t runtime_config, std::string type)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, runtime_config)
  {
    app_type = type;

    // Use the type to define TCP algorithm and infer source/sink
    std::regex re ("^(Tcp[A-Za-z]+)(?:-(sink|source))?$");
    std::smatch match;
    if (std::regex_search (app_type, match, re))
      {
        tcp_alg = match[1];
        if (match[2] == "sink")
          {
            is_sink = true;
          }
      }
    else
      {
        std::cerr << "Invalid application type: " << app_type << std::endl;
        exit (1);
      }

    std::cout << app_type << " initializing... " << std::endl;

    // Setup the runtime configuration
    cfgLogging = ENABLE_LOGGING (runtime_config);
    cfgPathSwitching = !(DISABLE_PATH_SWITCHING (runtime_config));

    // TCP applications start time is scheduled at initialization, which happens
    // when the simulation starts. But we start the SCION applications only
    // during the runtime of the simulation because the user defined events are
    // scheduled then. Thus, we just let the TCP sender run from the start, but
    // inhibit packet sending in our callback function until stopped == false;
    stopped = true;

    // IP stack
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType",
                        TypeIdValue (TypeId::LookupByName ("ns3::" + tcp_alg)));
    SetupIPStack ();

    active_path = 0;

    std::cout << app_type << " initialized." << std::endl;
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
    Log (" seq " + std::to_string (incomingTcpHeader.GetSequenceNumber ().GetValue ()) + " ack " +
             std::to_string (incomingTcpHeader.GetAckNumber ().GetValue ()) + " flags " +
             TcpHeader::FlagsToString (incomingTcpHeader.GetFlags ()) + " payload size " +
             std::to_string (tcp_payload_size),
         false);

    // TODO: We could even set ECN according to the SCION packet ECN
    // information, but this would require checking for support at time of
    // creation of the SCION packet
    ipHeader.SetEcn (Ipv4Header::EcnType::ECN_NotECT);

    // Deliver TCP packet to layer 4
    tcpLayer4->Receive (packet, ipHeader, Ptr<Ipv4Interface> ());
    bytes_received += ip_pkt_size;
  }

  void
  StartAppTraffic ()
  {
    Log ("Starting application", true);
    app.Start (Seconds (0));
    stopped = false;
  }

  void
  StopAppTraffic ()
  {
    Log ("Stopping application", true);
    stopped = true;
  }

  void
  ReceiveAppResponse (AppResp app_resp)
  {
    last_report = app_resp;
    TrackState ();
  }

  void
  PrintResults ()
  {
    if (!is_sink)
      {
        PrintResultsJSON ();
      }
    std::cout << app_type << "-" << app_id << " Results summary:" << std::endl;
    std::cout << "  Total Bytes Sent     (Layer 3): " << bytes_sent << std::endl;
    std::cout << "  Total Bytes Received (Layer 3): " << bytes_received << std::endl;

    if (is_sink)
      {
        Ptr<PacketSink> sink = DynamicCast<PacketSink> (app.Get (0));
        std::cout << "  Total Bytes Received (Layer 4): " << sink->GetTotalRx () << std::endl;
      }
  }
};

} // namespace ns3
