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
#include "src/SCION/model/apps/delay-based-controller.cc"
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

  Time probed_last = Seconds (0); // time the last probing was initiated
  app_packet_id_t probe_seq_no = 0; // probe packet seq_no
};

// Struct to store application state for visualization
struct AppState
{
  Time timestamp;
  double bitrate = 0;
  ControllerStateSnapshot controller_state;
  double A_s = 0;
  double A_r = 0;
  double fair_share = 0;
  double latency = 0;
  double loss = 0;
  app_path_id_t active_path;
  std::vector<PathStatistics> path_stats;
};

/**
 * RTC like application with some path selection smarts
*/
class RTCApp : public App
{
protected:
  const double LOSS_TRESHOLD_LOW = 0.02;
  const double LOSS_TRESHOLD_HIGH = 0.10;
  const double LOSS_SMOOTHING_FACTOR = 0.95;
  const double PATH_SWITCH_TRESHOLD = 1.5;
  const Time TRACKING_INTERVAL = Seconds (0.1);
  uint32_t num_paths;
  app_path_id_t active_path;

  std::string
  log_prefix ()
  {
    // print current time in ms
    std::string log_prefix = "[" + std::to_string (Simulator::Now ().ToDouble (Time::Unit::MIN)) +
                             "][rtc-" + std::to_string (app_id) + "] ";
    return log_prefix;
  }

  // vector to store information each path
  std::vector<PathStatistics> path_infos;

  std::map<app_packet_id_t, AppProbe> in_flight_probes;

  std::vector<double> bitrates{10 * 0.7e6, 10 * 1.5e6, 10 * 5e6};
  uint32_t selected_bitrate = 0;
  const uint16_t fps = 30;
  const Time frame_interval = Seconds (1.0 / fps); // how much time between frames
  uint32_t frame_no = 0; // number of the video frame

  ControllerStateSnapshot controller_state; // state of the delay based controller
  double A_r = 0; // send rate estimate by the receiver side loss based controller
  double A_s = 0; // send rate estimate by the sender side loss based controller
  double sendrate = 0.2e7; // in Bytes per second

  double steering_treshold_u = 20;
  double steering_treshold_l = 0;

  Time last_path_change = Time (0);
  Time last_remb_change = Time (0);
  Time last_loss_based_rate_update = Time (0);

  Time probe_interval = Seconds (0.25); // How often new probes are sent out
  uint16_t probe_simultaneous = 2; // How many paths to probe at the same time
  app_packet_id_t probe_id = 0; // Identifies a probe, not the packet though, that is probe_seq_no

  // Store app state at different time points for evaluation
  std::vector<AppState> statistics;

public:
  RTCApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
          host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
          bool enable_logging)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, enable_logging)
  {
    num_paths = all_paths.size ();

    // initialize path infos
    for (uint32_t i = 0; i < num_paths; i++)
      {
        PathStatistics path_info;
        path_infos.push_back (path_info);
      }

    // Use default values for initial state (only relevant for plot)
    controller_state.signal = DetectorSignal::NORMAL;
    controller_state.state = ControllerState::INCREASE;

    // choose a random path to start with
    active_path = rand () % num_paths;
    active_path = 0; // TODO: For testing
    std::cout << log_prefix () << "Initiated." << " Starting path : " << active_path
              << ", Logging: " << enable_logging << std::endl;
  }

  void
  StartAppTraffic ()
  {
    SendProbes ();
    // TrackState ();

    // Start sending video frames after a random delay, to avoid synchronization
    Simulator::Schedule (MilliSeconds (1000) + RandomDelay (500), &RTCApp::SendVideoFrame, this);
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
    AppState state;
    state.timestamp = Simulator::Now ();
    state.active_path = active_path;
    // state.path_stats = path_infos;
    state.bitrate = sendrate;
    state.latency = path_info.latency;
    state.loss = path_info.loss;
    state.controller_state = controller_state;
    state.A_s = A_s;
    state.A_r = A_r;
    state.fair_share = path_info.fair_share;
    statistics.push_back (state);

    // if logging enabled, print all path infos
    if (enable_logging)
      {
        std::cout << log_prefix () << "Current state:" << std::endl;
        for (uint32_t i = 0; i < num_paths; i++)
          {
            std::string prefix = (i == active_path) ? "    -> " : "       ";
            std::cout << prefix << "Path " << i
                      << ", latency [ms]: " << path_infos[i].latency / 1000.0
                      << ", loss: " << path_infos[i].loss
                      << ", fair_share: " << path_infos[i].fair_share
                      << ", score: " << path_infos[i].score << std::endl;
          }
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

    if (enable_logging)
      {
        if (candidates.empty ())
          {
            std::cout << log_prefix () << "No candidates for probing." << std::endl;
          }
        else
          {
            std::cout << log_prefix () << "Selected candidates for probing: [";
            for (auto candidate : candidates)
              {
                std::cout << candidate << " ";
              }
            std::cout << "]" << std::endl;
          }
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

    Payload payload;
    payload.app_probe = probe;
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

    std::cout << log_prefix () << "Sending frame " << frame_no << " with " << frame_bytes
              << " bytes on path " << active_path << std::endl;

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

  void
  ReceiveREMB (AppResp app_resp)
  {
    if (enable_logging)
      {
        std::cout << log_prefix () << "Receiving REMB report on active path " << app_resp.path_id
                  << std::endl;
      }

    // HACK: In reality, we would only get a REMB once a second, or when the
    // receiver detects congestion. For visualization of the controller state,
    // we do want to receive a REMB on every update, but we only update A_r once
    // a second or when congested to keep the simulation realistic.

    controller_state = app_resp.state_snapshot;
    if (Simulator::Now () > last_remb_change + Seconds (1) || app_resp.A_r < 0.97 * A_r)
      {
        A_r = app_resp.A_r;
        last_remb_change = Simulator::Now ();
      }
  }

  /***
   * Handle response
  */
  void
  ReceiveAppResponse (AppResp app_resp)
  {
    auto path_id = app_resp.path_id;

    if (path_id != active_path)
      {
        if (enable_logging)
          {
            std::cout << log_prefix () << "Receiving response on inactive (old) path: " << path_id
                      << std::endl;
          }
      }

    if (app_resp.is_REMB)
      {
        ReceiveREMB (app_resp);
        return;
      }

    if (enable_logging)
      {
        std::cout << log_prefix () << "Processing report on active path " << path_id << std::endl;
      }

    Time resp_time = MicroSeconds (app_resp.timestamp);
    path_infos[path_id].last_report = resp_time;

    path_infos[path_id].ecn = app_resp.ecn;
    path_infos[path_id].latency = app_resp.avg_latency;

    if (app_resp.loss < 0)
      {
        std::cout << log_prefix () << "Loss <0 detected" << std::endl;
        app_resp.loss = 0;
      }

    // Exponential moving average
    path_infos[path_id].loss *= (1 - LOSS_SMOOTHING_FACTOR);
    path_infos[path_id].loss += (LOSS_SMOOTHING_FACTOR * app_resp.loss);

    if (enable_logging)
      {
        std::cout << "    Loss: " << app_resp.loss << std::endl
                  << "    Latency: " << app_resp.avg_latency << std::endl
                  << "    Current sending rate: " << sendrate << std::endl;
      }

    // With feedback on our active path, we can perform congestion control
    if (path_id == active_path)
      {
        UpdateBWE ();
      }
  }

  void
  UpdateBWE ()
  {
    UpdateLossController ();
    sendrate = A_s;

    // If we got an estimate from the receiver, use it
    if (A_r > 0)
      {
        if (A_r < A_s)
          {
            std::cout << log_prefix () << "Receiver estimate lower than sender estimate: " << A_r
                      << " < " << A_s << ". Using receiver estimate." << std::endl;
            sendrate = A_r;
          }
        else
          {
            std::cout << log_prefix () << "Receiver estimate higher than sender estimate: " << A_r
                      << " > " << A_s << ". Using sender estimate." << std::endl;
          }
      }
    else
      {
        std::cout << log_prefix () << "No receiver estimate available. Using sender estimate."
                  << std::endl;
      }
    CheckPathSwitch ();
    TrackState ();
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
        if (enable_logging)
          {
            std::cout << log_prefix () << "Received probe response for unknown probe id "
                      << probe_id << std::endl;
          }
        return;
      }

    // Compute latency
    auto latency = probe_resp.time_rx - probe_resp.time_tx;
    auto hop = probe_resp.min_fair_share_hop;

    if (enable_logging)
      {
        std::cout << log_prefix () << "Received probe response on path " << path_id << std::endl
                  << "    Latency [ms]: " << latency / 1000.0 << std::endl
                  << "    min fair share: " << probe_resp.min_fair_share * 1e6 / 8 << std::endl
                  << "    min fair share seen at AS " << GET_HOP_AS (hop) << " and hop "
                  << GET_HOP_EG_IF (hop) << std::endl;
      }

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
  UpdateLossController ()
  {
    // Simple, loss based congestion control based on
    // https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02

    double time_since_last_update = (Simulator::Now () - last_loss_based_rate_update).GetSeconds ();
    double eta;
    if (path_infos[active_path].loss < LOSS_TRESHOLD_LOW)
      {
        eta = std::pow (1.05, std::min (time_since_last_update, 1.0));
        A_s = eta * sendrate + 1e4;
      }
    else if (path_infos[active_path].loss > LOSS_TRESHOLD_HIGH)
      {
        eta = std::pow ((1 - 0.5 * path_infos[active_path].loss),
                        std::min (time_since_last_update, 1.0));
        A_s = eta * sendrate;
      }
    else
      {
        // do nothing
      }
    last_loss_based_rate_update = Simulator::Now ();
  }

  void
  CheckPathSwitch ()
  {
    return;
    // If the loss is very low, don't bother switching paths as we're still upping the send rate
    if (path_infos[active_path].loss < LOSS_TRESHOLD_LOW)
      {
        return;
      }

    // If we switched paths only recently, and the loss is still tolerable, don't switch
    if (path_infos[active_path].loss < 0.5 && Simulator::Now () - last_path_change < Seconds (0.5))
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
            std::cout << log_prefix () << "Selecting path switch candidate: " << active_path
                      << " to " << i << ". Estimated fair share is higher than current bitrate "
                      << path_infos[i].fair_share << " > " << sendrate << std::endl;
            switch_candidates.push_back (i);
          }
        else
          {
            std::cout << log_prefix () << "Excluding path switch candidate: " << active_path
                      << " to " << i << ". Estimated fair share is not high enough "
                      << path_infos[i].fair_share << " / " << sendrate << std::endl;
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
        std::cout << log_prefix () << "Staying on current path " << active_path << std::endl;
        return;
      }
    if (enable_logging)
      {
        std::cout << log_prefix () << "Switching from path " << active_path << " to " << new_path
                  << std::endl;
      }
    active_path = new_path;
    last_path_change = Simulator::Now ();
  }

  void
  SteerTreshold ()
  {
    // Compute the sigmoid of the score
    // NOTE: subject to change
    double alpha = 1. / (1. + exp (-path_infos[active_path].score));

    // With probability (1 - alpha), try to switch paths
    bool try_switch = !(rand () % 100 < 100 * alpha);

    if (enable_logging)
      {
        std::cout << log_prefix () << "Steering decision:" << std::endl;
        std::cout << "    alpha " << alpha << std::endl;
        std::cout << "    try_switch " << try_switch << std::endl;

        // print scores of all other paths for debugging
        for (uint32_t i = 0; i < num_paths; i++)
          {
            if (i == active_path)
              {
                continue;
              }
            std::cout << "    path " << i << " score " << path_infos[i].score << std::endl;
          }
      }

    // score lower than treshold_l
    if (path_infos[active_path].score < steering_treshold_l)
      {
        std::vector<uint32_t> candidate_paths;

        // with probability (1-alpha), switch paths
        if (try_switch)
          {
            // find alternative candidate paths with score > treshold_l
            for (uint32_t i = 0; i < num_paths; i++)
              {
                if (i == active_path)
                  {
                    continue;
                  }
                if (path_infos[i].score > steering_treshold_l)
                  {
                    candidate_paths.push_back (i);
                  }
              }

            if (candidate_paths.size () > 0)
              {
                uint32_t new_path = candidate_paths.at (rand () % candidate_paths.size ());

                if (enable_logging)
                  {
                    std::cout << log_prefix () << "Switching from path " << active_path << " to "
                              << new_path << std::endl;
                  }
                active_path = new_path;
              }
            else
              {
                if (enable_logging)
                  {
                    std::cout << log_prefix () << "No better path > treshold_l found" << std::endl;
                  }
              }
          }

        // with probability alpha or if no other path is available
        if (!try_switch || candidate_paths.size () == 0)
          {
            // lower the bitrate, if we can still go lower
            if (selected_bitrate > 0)
              {
                selected_bitrate--;
                if (enable_logging)
                  {
                    std::cout << log_prefix () << "Lowering bitrate to "
                              << bitrates[selected_bitrate] << std::endl;
                  }
              }
          }
      }
    // score higher than upper treshold
    else if (path_infos[active_path].score > steering_treshold_u)
      {
        if (!try_switch)
          {
            // Increase sending rate if we can
            if (selected_bitrate < bitrates.size () - 1)
              {
                selected_bitrate++;
                if (enable_logging)
                  {
                    std::cout << log_prefix () << "Increasing bitrate to "
                              << bitrates[selected_bitrate] << std::endl;
                  }
              }
          }
      }
    else
      {
        if (try_switch)
          {
            // Try to find a better path with score > treshold_u
            std::set<uint32_t> candidate_paths;
            for (uint32_t i = 0; i < num_paths; i++)
              {
                if (i != active_path && path_infos[i].score > steering_treshold_u)
                  {
                    candidate_paths.insert (i);
                  }
              }
            if (candidate_paths.size () > 0)
              {
                uint32_t new_path = rand () % candidate_paths.size ();
                active_path = new_path;
                if (enable_logging)
                  {
                    std::cout << log_prefix () << "Steering to a different path " << active_path
                              << std::endl;
                  }
              }
            else
              {
                if (enable_logging)
                  {
                    std::cout << log_prefix () << "No better path > treshold_u found" << std::endl;
                  }
              }
          }
      }
  }

  void
  SendPacket (double packetSize, std::vector<const PathSegment *> path)
  {
    if (enable_logging)
      {
        // std::cout << log_prefix () << "Sending data packet via path " << active_path << std::endl;
      }
    Payload payload;
    payload.app_data.app_id = app_id;
    payload.app_data.path_id = active_path;
    payload.app_data.seq_no = path_infos[active_path].seq_no++;
    payload.app_data.frame_no = frame_no;
    payload.app_data.timestamp = Simulator::Now ().ToInteger (Time::Unit::US);
    PayloadType payload_type = PayloadType::APPLICATION_DATA;
    host->SendAppPacket (this, payload, payload_type, packetSize * scale + sizeof (AppData), path);
  }

  void
  StopAppTraffic ()
  {
    stopped = true;
  }

  std::string
  InfoString ()
  {
    return "rtc fair_share";
  }

  void
  PrintResults ()
  {
    std::cout << "----- " << InfoString () << " id " << app_id << " dst " << dst_ia << ":"
              << dst_host_addr << "-------" << std::endl
              << "----- Timestamp, latency[ms], loss, path, send_rate, A_s, A_r, fair_share, "
                 "state, signal, estimate_gradient, treshold, measured_gradient ------- "
              << std::endl;

    // print all the app statistics
    for (AppState state : statistics)
      {
        std::cout << state.timestamp.ToInteger (Time::Unit::MS) << "(" << std::setw (16)
                  << state.timestamp.ToDouble (Time::Unit::MIN) << std::setw (0) << " min), ";
        std::cout << std::setw (12) << state.latency / 1000.0 << std::setw (0) << ", ";
        std::cout << std::setw (12) << state.loss << std::setw (0) << ", ";
        std::cout << std::setw (4) << state.active_path << std::setw (0) << ", ";
        std::cout << std::setw (12) << state.bitrate << std::setw (0) << ", ";
        std::cout << std::setw (12) << state.A_s << std::setw (0) << ", ";
        std::cout << std::setw (12) << state.A_r << std::setw (0) << ", ";
        std::cout << std::setw (12) << state.fair_share << std::setw (0) << ", ";
        std::cout << std::setw (3) << static_cast<int> (state.controller_state.state)
                  << std::setw (0) << ", ";
        std::cout << std::setw (3) << static_cast<int> (state.controller_state.signal) << ", ";
        std::cout << std::setw (16) << state.controller_state.m << std::setw (0) << ", ";
        std::cout << std::setw (16) << state.controller_state.adaptive_treshold << std::setw (0)
                  << ", ";
        std::cout << std::setw (16) << state.controller_state.d_m << std::setw (0) << ", ";
        std::cout << std::setw (16) << state.controller_state.kalman_gain << ", " << std::setw (0);
        std::cout << std::setw (16) << state.controller_state.measurement_noise_variance << ", "
                  << std::setw (0);
        std::cout << std::setw (16) << state.controller_state.e << std::setw (0);
        std::cout << std::endl;
      }
    std::cout << "----- End of app " << app_id << " results ------" << std::endl;
  }

  /**
   * Override to throw error, we don't use probes but keep the existing code
  */
  void
  ReceiveProbeResponse (ProbeResp probe_resp)
  {
    NS_FATAL_ERROR ("[rtc-app] should not have received probe response");
  }

  // Returns a random time between -N and N where N is an integer parameter in ps
  Time
  RandomDelay (int N)
  {
    return PicoSeconds (rand () % (2 * N) - N);
  }
};

} // namespace ns3
