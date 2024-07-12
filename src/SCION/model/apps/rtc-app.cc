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
#include "src/SCION/model/webrtc-cc/types.h"
#include "src/SCION/model/webrtc-cc/cc-units.h"
#include "src/SCION/model/webrtc-cc/delay-based-estimator.cc"
#include "src/SCION/model/webrtc-cc/loss-based-estimator.cc"

#include "modules/congestion_controller/goog_cc/goog_cc_network_control.h"
#include "api/environment/environment_factory.h"
#include "api/transport/network_types.h"
#include "api/transport/network_control.h"
#include "api/units/timestamp.h"
#include "api/transport/goog_cc_factory.h"

#include <iomanip>

namespace ns3 {

// Struct to store path information
struct PathStatistics
{
  uint16_t ecn;
  Time last_report; // time of last ecn mark
  Time last_scmp; // time of last scmp congestion response
  app_packet_id_t seq_no = 1; // Next seq no to send
  app_packet_id_t seq_no_ack = 0; // Highest acked package
  double score = 0;
  double latency = 0; // observed latency
  double bandwidth; // estimated bandwidth in Gbps
  double fair_share = 0; // estimated fair share in Gbps
  double bottleneck_no_flows = 0; // number of flows at the bottleneck link
  double loss = 0; // estimated loss ratio

  std::vector<webrtc::SentPacket> in_flight_packets;

  Time probed_last = Seconds (0); // time the last probing was initiated
  app_packet_id_t probe_seq_no = 0; // probe packet seq_no
};

// Struct to store application state for visualization
struct RTCAppState
{
  Time timestamp;
  double sendrate = 0;
  // ControllerStateSnapshot controller_state;
  double A_s = 0;
  double A_r = 0;
  double fair_share = 0;
  double latency = 0;
  double loss = 0;
  CongestionControlPhase phase;
  app_path_id_t active_path;
  std::vector<PathStatistics> path_stats;
};

/**
 * RTC like application with some path selection smarts
*/
class RTCApp : public App
{
protected:
  // App config
  bool cfgLogging = false;
  bool cfgLossBwe = true;
  bool cfgDelayBwe = true;
  bool cfgPathSwitching = true;

  const double PATH_SWITCH_TRESHOLD = 1.5;
  const Time TRACKING_INTERVAL = Seconds (0.1);
  uint32_t num_paths;
  app_path_id_t active_path;

  // vector to store information each path
  std::vector<PathStatistics> path_infos;

  std::map<app_packet_id_t, AppProbe> in_flight_probes;
  double in_flight_bytes = 0;

  std::vector<double> bitrates{10 * 0.7e6, 10 * 1.5e6, 10 * 5e6};
  uint32_t selected_bitrate = 0;
  const uint16_t fps = 30;
  const Time frame_interval = Seconds (1.0 / fps); // how much time between frames
  uint32_t frame_no = 0; // number of the video frame

  Time round_trip_time = Seconds (0);
  CongestionControlPhase phase;
  DelayBasedController delay_based_estimator;
  LossBasedEstimator loss_based_estimator;

  // webrtc::NetworkControllerInterface *network_controller;
  std::unique_ptr<webrtc::NetworkControllerInterface> network_controller;

  double A_r = 0; // send rate estimate by the receiver side loss based controller
  double A_s = 0; // send rate estimate by the sender side loss based controller
  double sendrate = 300'000 / 8; // 300 Kbps

  double steering_threshold_u = 20;
  double steering_threshold_l = 0;

  Time last_path_change = Seconds (0);
  Time last_A_r_update = Seconds (0);
  Time last_report = Seconds (0);
  Time last_cngst_event = Time::Min ();

  Time probe_interval = Seconds (0.25); // How often new probes are sent out
  uint16_t probe_simultaneous = 2; // How many paths to probe at the same time
  app_packet_id_t probe_id = 0; // Identifies a probe, not the packet though, that is probe_seq_no

  // Store app state at different time points for evaluation
  std::vector<RTCAppState> statistics;

  void
  Log (std::string msg, bool with_prefix = true)
  {
    if (!cfgLogging)
      {
        return;
      }

    std::string prefix;
    if (with_prefix)
      {
        prefix += "[" + std::to_string (Simulator::Now ().ToDouble (Time::Unit::MIN)) + "][rtc-" +
                  std::to_string (app_id) + "] ";
      }
    std::cout << prefix << msg << std::endl;
  }

public:
  RTCApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
          host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
          uint32_t runtime_config)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, runtime_config)
  {
    // Setup the runtime configuration
    cfgLogging = ENABLE_LOGGING (runtime_config);
    cfgLossBwe = !(DISABLE_LOSS_BWE (runtime_config));
    cfgDelayBwe = !(DISABLE_DELAY_BWE (runtime_config));
    cfgPathSwitching = !(DISABLE_PATH_SWITCHING (runtime_config));

    // Initialize path infos
    num_paths = all_paths.size ();
    for (uint32_t i = 0; i < num_paths; i++)
      {
        PathStatistics path_info;
        path_infos.push_back (path_info);
      }

    // webrtc::DataRate initial_rate = webrtc::DataRate::KilobitsPerSec(300);
    // std::cout << "Initial rate: " << initial_rate.bps() << std::endl;

    // During startup, we just use the loss based estimate
    phase = CongestionControlPhase::STARTUP;
    loss_based_estimator.SetRate (CC_INITIAL_SEND_RATE);

    // network_controller = new webrtc::GoogCcNetworkController (webrtc::NetworkControllerConfig (),
    //                                                   webrtc::GoogCcConfig{
    //                                                       .network_state_estimator = nullptr,
    //                                                       .feedback_only = false,
    //                                                   });

    // using namespace webrtc;
    webrtc::GoogCcFactoryConfig factory_config;
    factory_config.feedback_only = true;
    factory_config.network_state_estimator_factory = nullptr;

    webrtc::GoogCcNetworkControllerFactory factory =
        webrtc::GoogCcNetworkControllerFactory (std::move (factory_config));
    webrtc::Environment default_env = webrtc::EnvironmentFactory ().Create ();
    webrtc::NetworkControllerConfig config (default_env);

    config.constraints.at_time = TimestampNow ();

    network_controller = factory.Create (config);

    // network_controller = new webrtc::GoogCcNetworkController (
    //     webrtc::NetworkControllerConfig (), webrtc::GoogCcConfig{
    //                                             .network_state_estimator = nullptr,
    //                                             .feedback_only = false,
    //                                         });

    // Choose a random path to start with
    active_path = rand () % num_paths;
    active_path = 0; // TODO: For testing

    Log ("Initialized RTCApp " + std::to_string (app_id));
    Log ("  Runtime config: " + std::to_string (runtime_config), false);
    Log ("  Active path: " + std::to_string (active_path), false);
    Log ("  Logging enabled: " + std::to_string (cfgLogging), false);
    Log ("  Loss controller enabled: " + std::to_string (cfgLossBwe), false);
    Log ("  Delay controller enabled: " + std::to_string (cfgDelayBwe), false);
    Log ("  Path switching enabled: " + std::to_string (cfgPathSwitching), false);
  }

  void
  StartAppTraffic ()
  {
    // TODO
    // network_controller->OnNetworkAvailability (NetworkAvailability{
    //     .network_available = true,
    //     .at_time = Simulator::Now (),
    // });

    // Start sending probes and video frames after a random delay, to avoid synchronization
    // Simulator::Schedule (MilliSeconds (5000) + RandomDelay (4000), &RTCApp::SendVideoFrame, this);
    // Simulator::Schedule (MilliSeconds (2000) + RandomDelay (1500), &RTCApp::SendProbes, this);

    SendVideoFrame ();
    SendProbes ();
  }

  void
  TrackState ()
  {
    if (stopped)
      {
        return;
      }

    auto path_info = path_infos[active_path];

    // store current state of the app
    RTCAppState state;
    state.timestamp = Simulator::Now ();
    state.active_path = active_path;
    // state.path_stats = path_infos;
    state.sendrate = sendrate;
    state.latency = path_info.latency;
    state.loss = loss_based_estimator.GetLoss ();
    // state.controller_state = delay_based_estimator.GetStateSnapshot ();
    state.A_s = A_s;
    state.A_r = A_r;
    state.fair_share = path_info.fair_share;
    statistics.push_back (state);

    Log ("Current state:");
    for (uint32_t i = 0; i < num_paths; i++)
      {
        std::string prefix = (i == active_path) ? "    -> " : "       ";
        Log ("    Path " + std::to_string (i) +
                 ", latency [ms]: " + std::to_string (path_infos[i].latency / 1000.0) +
                 ", loss: " + std::to_string (path_infos[i].loss) +
                 ", fair_share: " + std::to_string (path_infos[i].fair_share) +
                 ", score: " + std::to_string (path_infos[i].score),
             false);
      }
  }

  /**
   * Returns a list of path indexes that are candidates for probing.
   * Candidates are paths that have not been probed in the longest time
   * and haven't been probed in at least a second.
  */
  std::vector<app_path_id_t>
  FindProbeCandidates ()
  {
    std::vector<app_path_id_t> candidates;
    Time probed_last_min = Simulator::Now () - Seconds (1);
    for (uint32_t i = 0; i < num_paths; i++)
      {
        // // Don't probe active path
        // if (i == active_path)
        //   {
        //     continue;
        //   }
        if (path_infos[i].probed_last < probed_last_min)
          {
            candidates.clear ();
            candidates.push_back (i);
            probed_last_min = path_infos[i].probed_last;
          }
        else if (path_infos[i].probed_last == probed_last_min)
          {
            candidates.push_back (i);
          }
      }
    // Shuffle the candidates to avoid multiple applications probing in sync
    std::random_shuffle (candidates.begin (), candidates.end ());

    // Return only the first probe_simultaneous candidates
    candidates.resize (std::min (probe_simultaneous, (uint16_t) candidates.size ()));

    if (candidates.empty ())
      {
        Log ("Found no probe candidates");
      }
    else
      {
        std::string candidates_str = "[";
        for (auto candidate : candidates)
          {
            candidates_str += std::to_string (candidate) + ", ";
          }
        candidates_str += "]";
        Log ("Found probe candidates: " + candidates_str);
      }
    return candidates;
  }

  void
  ProbePath (app_path_id_t path_id)
  {
    // First set the probe id. This belongs to the entire probing action that
    // we're performing right now, not to the individual packet. Performing a
    // BWE for example involves sending multiple packets, all with the same probe id.
    app_path_id_t current_probe_id = probe_id++;

    AppProbe probe;
    probe.app_id = app_id;
    probe.type = AppProbeType::BANDWIDTH;
    probe.path_id = path_id;
    probe.probe_id = current_probe_id++;
    probe.probe_seq_no = path_infos[path_id].probe_seq_no++;
    probe.time_tx = Simulator::Now ().ToInteger (Time::Unit::US);
    probe.min_fair_share = INFINITY;
    probe.min_fair_share_hop = 0xFFFFFFFFFFFFFFFF;
    probe.max_queuing_delay = 0;
    in_flight_probes[probe.probe_id] = probe;

    Payload payload = probe;
    host->SendAppPacket (this, payload, PayloadType::APPLICATION_PROBE, sizeof (AppProbe),
                         all_paths[path_id]);
  }

  void
  SendProbes ()
  {
    if (stopped)
      {
        return;
      }

    auto candidates = FindProbeCandidates ();
    if (!candidates.empty ())
      {
        Time now = Simulator::Now ();
        for (app_path_id_t candidate : candidates)
          // for (app_path_id_t candidate = 0; candidate < num_paths; candidate++) // TODO: remove
          {
            ProbePath (candidate);

            // NOTE: Set probed_last to the exact same time for all candidates
            // we probe now. This way, when we find candidates in the future
            // with the oldest probed_last, it's actually likely to find a set,
            // and not just a single oldest one.
            path_infos[candidate].probed_last = now;
          }
      }

    // Schedule the next round of probing
    Simulator::Schedule (probe_interval +
                             RandomDelay ((probe_interval / 2).ToInteger (Time::Unit::MS)),
                         &RTCApp::SendProbes, this);
  }

  void
  HandleSCMP (ScmpReqOrResp scmp)
  {
    // TODO
    // Need information from host here about which path is affected
    // and then decide how exactly it influences the scoring
  }

  void
  SendVideoFrame ()
  {
    if (stopped)
      {
        return;
      }

    auto frame_bytes = sendrate / fps;

    // Check if frame_bytes is finite
    if (!std::isfinite (frame_bytes))
      {
        NS_FATAL_ERROR ("Frame size is not finite");
      }

    Log ("Sending frame " + std::to_string (frame_no) + " with " + std::to_string (frame_bytes) +
         " bytes on path " + std::to_string (active_path));

    double packets_to_send = std::ceil ((double) frame_bytes / m_pktSize);
    Time packet_interval = frame_interval / packets_to_send;
    Time packet_schedule_delay = Seconds (0);

    // split up into multiple packets if larger than max pkt size
    while (frame_bytes > m_pktSize)
      {
        Simulator::Schedule (packet_schedule_delay, &RTCApp::SendPacket, this, m_pktSize,
                             all_paths[active_path]);
        packet_schedule_delay += packet_interval;
        frame_bytes -= m_pktSize;
      }
    if (frame_bytes > 0)
      {
        Simulator::Schedule (packet_schedule_delay, &RTCApp::SendPacket, this, m_pktSize,
                             all_paths[active_path]);
      }

    frame_no++;

    // Schedule next packet
    auto delay = RandomDelay ((frame_interval / 10).ToInteger (Time::Unit::PS));
    Simulator::Schedule (frame_interval + delay, &RTCApp::SendVideoFrame, this);
  }

  /***
   * Handle response
  */
  void
  ReceiveAppResponse (AppResp app_resp)
  {
    auto path_id = app_resp.path_id;
    PacketsReport *report = app_resp.packets_report;

    if (path_id != active_path)
      {
        Log ("Receiving response on inactive (old) path: " + std::to_string (path_id));

        // Don't process responses on inactive paths
        delete report;
        return;
      }

    Log ("Processing report on active path " + std::to_string (path_id) +
         " with sequence numbers " + std::to_string (report->packets.front ().seq_no) + " to " +
         std::to_string (report->packets.back ().seq_no));

    Time resp_time = MicroSeconds (app_resp.timestamp);
    path_infos[path_id].last_report = resp_time;

    path_infos[path_id].ecn = app_resp.ecn;
    path_infos[path_id].latency = app_resp.avg_latency;

    // RTT update
    Time send_delay = report->packets.back ().time_received - report->packets.back ().time_sent;
    Time receive_delay = Simulator::Now () - MicroSeconds (app_resp.timestamp);
    round_trip_time = send_delay + receive_delay;
    delay_based_estimator.SetRoundTripTime (round_trip_time);
    loss_based_estimator.SetRoundTripTime (round_trip_time);

    if (app_resp.loss < 0)
      {
        Log ("WARN: Loss <0 detected");
        app_resp.loss = 0;
      }
    path_infos[path_id].loss = app_resp.loss;

    loss_based_estimator.FeedReport (report);
    delay_based_estimator.FeedReport (report);

    path_infos[path_id].loss = loss_based_estimator.GetLoss ();

    // if report empty
    if (report->packets.empty ())
      {
        Log ("WARN: Empty report received");
        delete report;
        return;
      }

    // Re-construct a valid TransportPacketsFeedback
    std::vector<webrtc::PacketResult> packet_feedbacks;

    std::sort (report->packets.begin (), report->packets.end (),
               [] (auto a, auto b) { return a.seq_no < b.seq_no; });

    // highest seq no
    uint32_t highest_seq_no = report->packets.back ().seq_no;

    for (webrtc::SentPacket packet : path_infos[path_id].in_flight_packets)
      {
        if (packet.sequence_number <= highest_seq_no)
          {
            webrtc::PacketResult packet_result;
            packet_result.sent_packet = packet;

            // Find the corresponding packet in the report
            auto it = std::find_if (report->packets.begin (), report->packets.end (),
                                    [packet] (PacketRecord report_packet) {
                                      return report_packet.seq_no == packet.sequence_number;
                                    });

            if (it != report->packets.end ())
              {
                // packet_result.receive_time = Timestamp (it->time_received);
                packet_result.receive_time =
                    webrtc::Timestamp::Millis (it->time_received.GetMilliSeconds ());
              }
            else
              {
                // continue;
              }

            packet_feedbacks.push_back (packet_result);
          }
      }

    // Clean up in-flight packets for which packet.sequence_number <= highest_seq_no
    // and also remove their size from in_flight_bytes
    path_infos[path_id].in_flight_packets.erase (
        std::remove_if (path_infos[path_id].in_flight_packets.begin (),
                        path_infos[path_id].in_flight_packets.end (),
                        [this, highest_seq_no] (webrtc::SentPacket packet) {
                          if (packet.sequence_number <= highest_seq_no)
                            {
                              in_flight_bytes -= packet.size.bytes ();
                              return true;
                            }
                          return false;
                        }),
        path_infos[path_id].in_flight_packets.end ());

    // Use the WebRTC network controller
    webrtc::TransportPacketsFeedback feedback;
    feedback.feedback_time = TimestampNow ();
    feedback.packet_feedbacks = std::move (packet_feedbacks);

    // TODO: is this correct?
    feedback.data_in_flight = webrtc::DataSize::Bytes (in_flight_bytes);

    webrtc::NetworkControlUpdate update =
        network_controller->OnTransportPacketsFeedback (std::move (feedback));

    if (update.has_updates ())
      {
        Log ("Received network control update");
        // update.congestion_window
      }
    if (update.target_rate.has_value ())
      {
        //  std::to_string (update.target_rate.value ().target_rate));
        webrtc::TargetTransferRate target_rate = update.target_rate.value ();
        sendrate = target_rate.target_rate.bps () / 8;
        Log ("Received target rate update: " + std::to_string (sendrate));

        // cast network controller to Googccnetworkcontroller
        webrtc::GoogCcNetworkController *goog_cc_network_controller =
            dynamic_cast<webrtc::GoogCcNetworkController *> (network_controller.get ());
        update = goog_cc_network_controller->GetNetworkState (TimestampNow ());
        if (update.target_rate.has_value ())
          {
            Log ("Another target rate update:  ");
            if (target_rate.target_rate != update.target_rate.value ().target_rate)
              {
                Log ("Different target rate update: ");

                target_rate = update.target_rate.value ();
                sendrate = target_rate.target_rate.bps () / 8;
              }
          }
      }

    // CheckPhase ();
    // UpdateBWE ();
    TrackState ();

    if (cfgPathSwitching)
      {
        CheckPathSwitch ();
      }

    delete report;
  }

  void
  UpdateBWE ()
  {

    A_r = delay_based_estimator.GetRate ();
    sendrate = A_r;
    return;

    A_s = loss_based_estimator.GetRate ();
    Log ("Sender estimate: " + std::to_string (A_s));

    if (phase == CongestionControlPhase::STARTUP)
      {
        if (delay_based_estimator.CongestionDetected () || loss_based_estimator.GetLoss () > 0)
          {
            Log ("Congestion detected in startup phase. Switching to congestion avoidance phase.");
            phase = CongestionControlPhase::CONGESTION_AVOIDANCE;
            loss_based_estimator.SetPhase (phase);
            delay_based_estimator.SetPhase (phase);
          }
        else // stay in startup phase
          {
            // During startup, we just use the loss based estimate
            sendrate = A_s;
          }
      }
    else if (phase == CongestionControlPhase::CONGESTION_AVOIDANCE)
      {
        // Now we incorporate the delay based estimate too
        A_r = delay_based_estimator.GetRate ();
        Log ("Receiver estimate: " + std::to_string (A_r));

        sendrate = A_s;

        // If we got an estimate from the receiver, use it
        if (cfgDelayBwe && A_r > 0)
          {
            if (A_r < A_s)
              {
                Log ("Receiver estimate lower than sender estimate: " + std::to_string (A_r) +
                     " < " + std::to_string (A_s) + ". Using receiver estimate.");
                sendrate = A_r;
                loss_based_estimator.LimitRate (1 * sendrate);
              }
            else
              {
                Log ("Receiver estimate higher than sender estimate: " + std::to_string (A_r) +
                     " > " + std::to_string (A_s) + ". Using sender estimate.");
              }
          }
        else
          {
            Log ("No receiver estimate available. Using sender estimate.");
          }

        if (cfgPathSwitching)
          {
            CheckPathSwitch ();
          }
      }
  }

  void
  ReceiveProbeResponse (AppProbe probe_resp)
  {
    auto probe_id = probe_resp.probe_id;
    auto path_id = probe_resp.path_id;
    auto seq_no = probe_resp.probe_seq_no;

    // Find the corresponding probe
    auto it = in_flight_probes.find (probe_id);
    if (it == in_flight_probes.end ())
      {
        Log ("Received probe response for unknown probe id " + std::to_string (probe_id));
        return;
      }

    // Compute latency
    auto latency = probe_resp.time_rx - probe_resp.time_tx;
    auto hop = probe_resp.min_fair_share_hop;

    Log ("Received probe response on path " + std::to_string (path_id));
    Log ("    Latency [ms]: " + std::to_string (latency / 1000.0), false);
    Log ("    min fair share: " + std::to_string (probe_resp.min_fair_share * 1e6 / 8), false);
    Log ("    min fair share seen at AS " + std::to_string (GET_HOP_AS (hop)) + " and hop " +
             std::to_string (GET_HOP_EG_IF (hop)),
         false);

    // Update path info
    path_infos[path_id].latency = latency;
    path_infos[path_id].bottleneck_no_flows = probe_resp.min_fair_share_no_flows;

    // Probe contains fair share in Gbps, convert to Bps
    path_infos[path_id].fair_share = probe_resp.min_fair_share * 1e9 / 8;

    // The fair share on the active path needs to be adjusted for the fact that
    // bottleneck_no_flows already includes a flow from this application.
    // For example, on a path that has a bottleneck link with 1Gbps and has two
    // flows from other applications, what we get back in the probe is 1Gbps / (2+1) = 333Mbps
    // The division by (2+1) is because it's the fair share we would get if we
    // were to also start sending via this path. But if we ARE already sending
    // through this path, the fair share is actually 1Gbps / (2) = 500Mbps, so
    // we need account for this ourselves.
    if (path_id == active_path)
      {
        path_infos[path_id].fair_share = path_infos[path_id].fair_share *
                                         path_infos[path_id].bottleneck_no_flows /
                                         (path_infos[path_id].bottleneck_no_flows - 1);
      }

    // Update score
    UpdateScore (path_id);

    // Remove the probe from the in-flight list
    in_flight_probes.erase (it);
  }

  void
  UpdateScore (uint32_t path_id)
  {
    // Lots of TODOs here, this is just a starting point
    double score = 0.0;
    score += 0.050 * path_infos[path_id].bandwidth;
    score -= 100.0 * path_infos[path_id].loss;
    score -= 0.001 * path_infos[path_id].latency - 100;

    path_infos[path_id].score = score;
  }

  void
  CheckPathSwitch ()
  {
    // If the loss is very low, don't bother switching paths as we're still upping the send rate
    // if (path_infos[active_path].loss < 0.2)
    // if (!delay_based_estimator.CongestionDetected ())
    //   {
    //     return;
    //   }

    // Don't switch paths too often
    if (Simulator::Now () - last_path_change < Seconds (10))
      {
        return;
      }

    // Choose as candidates all paths that have a significantly higher
    // fair_share than what is our current send rate. We also include the active
    // path, to provide a chance to stay on the current path and desynchronize
    // applications. A nice side effect is that the more better paths are
    // available, the more likely we are to switch. This is good, because if
    // there's just one better path, we don't want all applications to switch to
    // it at the same time. Whereas if we have many, applications are likely to
    // spread out.
    std::vector<uint32_t> switch_candidates;
    for (uint32_t i = 0; i < num_paths; i++)
      {
        if (i == active_path)
          {
            switch_candidates.push_back (i);
          }
        else if (path_infos[i].fair_share > sendrate * PATH_SWITCH_TRESHOLD)
          {
            Log ("Selecting path switch candidate: " + std::to_string (active_path) + " to " +
                 std::to_string (i) + ". Estimated fair share is higher than current bitrate " +
                 std::to_string (path_infos[i].fair_share) + " > " + std::to_string (sendrate));
            switch_candidates.push_back (i);
          }
        else
          {
            Log ("Excluding path switch candidate: " + std::to_string (active_path) + " to " +
                 std::to_string (i) + ". Estimated fair share is not high enough " +
                 std::to_string (path_infos[i].fair_share) + " / " + std::to_string (sendrate));
          }
      }

    // We should always have at least the active path as a candidate
    NS_ASSERT (!switch_candidates.empty ());

    // Pick a candidate at random
    uint32_t new_path = switch_candidates.at (rand () % switch_candidates.size ());
    SwitchToPath (new_path);
  }

  void
  SwitchToPath (uint32_t new_path)
  {
    if (new_path == active_path)
      {
        Log ("Staying on current path " + std::to_string (active_path));
        return;
      }
    Log ("Switching from path " + std::to_string (active_path) + " to " +
         std::to_string (new_path));
    active_path = new_path;
    loss_based_estimator.Reset ();
    delay_based_estimator.SetPhase (CongestionControlPhase::STARTUP);
    last_path_change = Simulator::Now ();

    double new_rate = path_infos[new_path].fair_share;

    webrtc::TargetRateConstraints new_constraints;
    new_constraints.at_time = TimestampNow ();
    new_constraints.starting_rate = webrtc::DataRate::BytesPerSec (new_rate);
    // contraints.min_data_rate
    // contraints.max_data_rate

    webrtc::NetworkRouteChange route_change;
    route_change.at_time = TimestampNow ();
    // route_change.target_rate = webrtc::DataRate::BytesPerSec (path_infos[new_path].fair_share);
    route_change.constraints = new_constraints;

    network_controller->OnNetworkRouteChange (route_change);
  }

  void
  SteerTreshold ()
  {
    // Compute the sigmoid of the score
    // NOTE: subject to change
    double alpha = 1. / (1. + exp (-path_infos[active_path].score));

    // With probability (1 - alpha), try to switch paths
    bool try_switch = !(rand () % 100 < 100 * alpha);

    // score lower than threshold_l
    if (path_infos[active_path].score < steering_threshold_l)
      {
        std::vector<uint32_t> candidate_paths;

        // with probability (1-alpha), switch paths
        if (try_switch)
          {
            // find alternative candidate paths with score > threshold_l
            for (uint32_t i = 0; i < num_paths; i++)
              {
                if (i == active_path)
                  {
                    continue;
                  }
                if (path_infos[i].score > steering_threshold_l)
                  {
                    candidate_paths.push_back (i);
                  }
              }

            if (candidate_paths.size () > 0)
              {
                uint32_t new_path = candidate_paths.at (rand () % candidate_paths.size ());

                Log ("Switching from path " + std::to_string (active_path) + " to " +
                     std::to_string (new_path));
                active_path = new_path;
              }
            else
              {
                Log ("No better path > threshold_l found");
              }
          }

        // with probability alpha or if no other path is available
        if (!try_switch || candidate_paths.size () == 0)
          {
            // lower the bitrate, if we can still go lower
            if (selected_bitrate > 0)
              {
                selected_bitrate--;
                Log ("Lowering bitrate to " + std::to_string (bitrates[selected_bitrate]));
              }
          }
      }
    // score higher than upper threshold
    else if (path_infos[active_path].score > steering_threshold_u)
      {
        if (!try_switch)
          {
            // Increase sending rate if we can
            if (selected_bitrate < bitrates.size () - 1)
              {
                selected_bitrate++;
                Log ("Increasing bitrate to " + std::to_string (bitrates[selected_bitrate]));
              }
          }
      }
    else
      {
        if (try_switch)
          {
            // Try to find a better path with score > threshold_u
            std::set<uint32_t> candidate_paths;
            for (uint32_t i = 0; i < num_paths; i++)
              {
                if (i != active_path && path_infos[i].score > steering_threshold_u)
                  {
                    candidate_paths.insert (i);
                  }
              }
            if (candidate_paths.size () > 0)
              {
                uint32_t new_path = rand () % candidate_paths.size ();
                active_path = new_path;
                Log ("Switching from path " + std::to_string (active_path) + " to " +
                     std::to_string (new_path));
              }
            else
              {
                Log ("No better path > threshold_u found");
              }
          }
      }
  }

  void
  SendPacket (double payload_size, std::vector<const PathSegment *> path)
  {
    AppData app_data;
    app_data.app_id = app_id;
    app_data.path_id = active_path;
    app_data.seq_no = path_infos[active_path].seq_no++;
    app_data.frame_no = frame_no;
    app_data.timestamp = Simulator::Now ().ToInteger (Time::Unit::US);
    Payload payload = app_data;
    PayloadType payload_type = PayloadType::APPLICATION_DATA;
    auto packet_size = payload_size + sizeof (AppData);
    host->SendAppPacket (this, payload, payload_type, packet_size, path);
    Log ("Sending packet with frame_no " + std::to_string (frame_no) + " and seq_no " +
         std::to_string (app_data.seq_no) + " on path " + std::to_string (active_path));

    in_flight_bytes += packet_size;

    webrtc::SentPacket sent_packet;
    sent_packet.sequence_number = app_data.seq_no;
    sent_packet.send_time = TimestampNow ();
    sent_packet.size = webrtc::DataSize::Bytes (packet_size);

    // TODO are these fields necessary? How to use them?
    // sent_packet.prior_unacked_data
    sent_packet.data_in_flight = webrtc::DataSize::Bytes (in_flight_bytes);

    path_infos[active_path].in_flight_packets.push_back (sent_packet);
    (void) network_controller->OnSentPacket (sent_packet);
  }

  webrtc::Timestamp
  TimestampNow ()
  {
    return webrtc::Timestamp::Micros (Simulator::Now ().GetMicroSeconds ());
  }

  void
  StopAppTraffic ()
  {
    stopped = true;
  }

  std::string
  InfoString ()
  {
    std::string info = "rtc";
    if (cfgLossBwe && cfgDelayBwe)
      {
        info += " D+L";
      }
    else if (cfgLossBwe)
      {
        info += "   L";
      }
    else if (cfgDelayBwe)
      {
        info += " D  ";
      }
    return info;
  }

  void
  PrintResults ()
  {
    nlohmann::json j;
    j["app_id"] = app_id;
    j["app_type"] = InfoString ();
    j["src_ia"] = ia_addr;
    j["dst_ia"] = dst_ia;
    j["dst_host_addr"] = dst_host_addr;

    nlohmann::json j_states;
    for (RTCAppState state : statistics)
      {
        nlohmann::json j_state;
        j_state["time"] = state.timestamp.ToDouble (Time::Unit::MS);
        j_state["sendrate"] = state.sendrate / 1e6;
        j_state["latency"] = state.latency / 1000.0;
        j_state["loss"] = state.loss;
        j_state["active_path"] = state.active_path;
        j_state["fair_share"] = state.fair_share / 1e6;
        j_state["A_s"] = state.A_s / 1e6;
        j_state["A_r"] = state.A_r / 1e6;
        // j_state["gradient"] = state.controller_state.m;
        // j_state["threshold_hi"] = state.controller_state.threshold_hi;
        // j_state["threshold_lo"] = 0; // TODO
        // j_state["gcc_state"] = state.controller_state.state;
        // j_state["gcc_signal"] = state.controller_state.signal;
        // j_state["kalman_gain"] = state.controller_state.kalman_gain;
        // j_state["variance"] = state.controller_state.variance;
        // j_state["error"] = state.controller_state.error;

        j_states.push_back (j_state);
      }
    j["states"] = j_states;

    // Dump JSON into a single line
    std::cout << j.dump () << std::endl;
  }

  /**
   * Override to throw error, we don't use probes but keep the existing code
  */
  void
  ReceiveProbeResponse (ProbeResp probe_resp)
  {
    NS_FATAL_ERROR ("[rtc-app] should not have received probe response");
  }

  /**
   * @return a random time between -N and N where N is an integer parameter in ps
   */
  Time
  RandomDelay (int N)
  {
    return PicoSeconds (rand () % (2 * N) - N);
  }
};

} // namespace ns3
