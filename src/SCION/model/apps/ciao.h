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

#ifndef SCION_CIAO_APP_H
#define SCION_CIAO_APP_H

#include "src/SCION/model/externs.h"
#include "src/SCION/model/scion-core-as.h"
#include "src/SCION/model/apps/app.h"
#include "src/SCION/model/webrtc-cc/delay-based-estimator.cc"
#include "src/SCION/model/webrtc-cc/loss-based-estimator.cc"

#include "modules/congestion_controller/goog_cc/goog_cc_network_control.h"
#include "api/transport/network_types.h"

namespace ns3 {

/**
 * @brief Struct to store path information
 */
struct PathMetric
{
  uint16_t ecn;
  Time last_report; // time of last ecn mark
  Time last_scmp; // time of last scmp congestion response
  app_packet_id_t seq_no = 1; // Next seq no to send
  app_packet_id_t seq_no_ack = 0; // Highest acked package
  double latency = 0; // observed latency, in µs
  double bottleneck_share = 0; // estimated fair share in Gbps
  double bottleneck_no_flows = 0; // number of flows at the bottleneck link
  double loss = 0; // estimated loss fraction
  double sendrate = 0; // current send rate

  std::vector<webrtc::SentPacket> in_flight_packets;
  double in_flight_bytes = 0;

  Time probed_last = Seconds (0); // time the last probe was sent
  AppProbe last_probe_result;
  bool HasFreshProbeResultsSince (Time t);

  app_packet_id_t probe_seq_no = 0; // probe packet seq_no

  // Earliest point in time since which path has continously been a switching candidate
  Time is_candidate_since = Time::Max ();
  bool is_candidate = false;
};

/**
 * @brief Struct to store application state for visualization
 */
struct RTCAppMetric
{
  Time timestamp;
  double sendrate = 0;
  double oldrate = 0;
  double newrate = 0;
  bool in_transition = false;
  double A_s = 0;
  double A_r = 0;
  double bottleneck_share = 0;
  double latency = 0;
  double loss = 0;
  app_path_id_t active_path;
};

enum class PathChangeStrategy {
  IMMEDIATE, // instantly switch, with hint to congestion controller
  TRANSITION // enter transition phase where manual ramp up the rate
};

enum class PathTransitionStrategy { LINEAR, SIGMOID, CUBIC };

/**
 * Application with WebRTC congestion control and smart path selection
*/
class CiaoApp : public App
{
protected:
  // App config
  bool cfgLogging = false;
  bool cfgLossBwe = true;
  bool cfgDelayBwe = true;
  bool cfgPathSwitching = true;

  PathChangeStrategy path_change_strategy = PathChangeStrategy::TRANSITION;
  PathTransitionStrategy path_transition_strategy = PathTransitionStrategy::CUBIC;

  // How much better a path must be before we consider switching to it
  const double path_candidate_treshold = 1.4;

  // How long we wait before switching paths again
  const Time path_switch_min_interval = Seconds (10);

  // How long a path needs to be a candidate before we select it
  const Time path_switch_min_candidacy = Seconds (5);

  // Time since last report before we consider a path dead
  const Time path_alive_treshold = Seconds (1);

  uint32_t num_paths;
  app_path_id_t active_path;
  app_path_id_t previous_path;

  // Store information on each candidate path
  std::vector<PathMetric> path_metrics;
  // Store app timeseries data for evaluation
  std::vector<RTCAppMetric> app_metrics;

  std::map<app_packet_id_t, AppProbe> in_flight_probes;

  const uint16_t fps = 30;
  const Time frame_interval = Seconds (1.0 / fps); // how much time between frames
  uint32_t frame_no = 0; // number of the video frame

  Time round_trip_time = Seconds (0);
  CongestionControlPhase phase;
  DelayBasedController delay_based_estimator;
  LossBasedEstimator loss_based_estimator;

  std::unique_ptr<webrtc::NetworkControllerInterface> network_controller;

  double A_r = 0; // send rate estimate by the receiver side loss based controller
  double A_s = 0; // send rate estimate by the sender side loss based controller
  double target_sendrate = 300'000 / 8; // 300 Kbps
  double previous_sendrate = 0; // used to limit rate on old path while shifting

  double total_bytes_sent = 0; // total of app packets sent
  double total_bytes_arrived = 0; // total of app packets arrived

  bool path_shifting = false; // if enabled, shift gradually between paths
  bool in_path_transition = false;
  double prev_sendrate = 0;
  Time path_transition_end = Seconds (0);
  Time last_path_change = Seconds (0);
  Time last_A_r_update = Seconds (0);

  Time probe_interval = Seconds (0.25); // How often new probes are sent out
  uint16_t probe_simultaneous = 3; // How many paths to probe at the same time
  app_packet_id_t probe_id = 0; // Identifies a probe, not the packet though, that is probe_seq_no

  void Log (std::string msg, bool with_prefix = true);

public:
  CiaoApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
          host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
          uint32_t runtime_config);

  void StartAppTraffic ();

  void StopAppTraffic ();

  void RecordMetrics ();

  void SendProbes ();

  /**
   * Returns a list of path indexes that are candidates for probing.
  */
  std::vector<app_path_id_t> FindProbeCandidates ();

  void ProbePath (app_path_id_t path_id);

  void ScheduleSend ();

  void SendFrameData (double sendrate, app_path_id_t path);

  void SendPacket (double payload_size, u_int16_t scion_header_bytes, app_path_id_t path);

  void ReceiveAppResponse (AppResp app_resp);

  void ReceiveProbeResponse (AppProbe probe_resp);

  void ReceiveScmp (ScmpReqOrResp scmp);

  void UpdatePathCandidates ();

  void SwitchToPath (uint32_t new_path);

  void EndPathTransition ();

  void UpdateBWE ();

  /**
   * @brief Get a webrtc timestamp of the current ns3 time
   */
  webrtc::Timestamp TimestampNow ();

  std::string InfoString ();

  void PrintResults ();

  /**
   * Override to throw error, we don't use probes but keep the existing code
  */
  void ReceiveProbeResponse (ProbeResp probe_resp);

  /**
   * @return a random time between -N and N where N is an integer parameter in ps
   */
  Time RandomDelay (int N);
};

} // namespace ns3

#endif // SCION_CIAO_APP_H
