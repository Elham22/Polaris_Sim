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

#pragma once

#include "src/SCION/model/externs.h"
#include "src/SCION/model/scion-core-as.h"
#include "src/SCION/model/apps/app.h"
#include "src/SCION/model/webrtc-cc/delay-based-estimator.cc"
#include "src/SCION/model/webrtc-cc/loss-based-estimator.cc"
#include "src/SCION/model/apps/connection-metrics.h"
#include "src/SCION/model/apps/path-state.h"

#include "modules/congestion_controller/goog_cc/goog_cc_network_control.h"
#include "api/transport/network_types.h"

namespace ns3 {

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
  PathTransitionStrategy path_transition_strategy = PathTransitionStrategy::SIGMOID;

  // How much better a path must be before we consider switching to it
  double path_change_margin = 1.5;

  double initial_bandwidth_factor = 0.85;

  bool path_shifting = false; // if enabled, shift gradually between paths

  // How long we wait before switching paths again
  Time path_switch_min_interval = Seconds (10);

  // How long a path needs to be a candidate before we select it
  Time path_switch_min_candidacy = Seconds (1);

  // Time since last report before we consider a path dead
  const Time path_alive_treshold = Seconds (5);

  // Time after which we consider a C-CA stale
  Time ciao_congestion_alert_timeout = Seconds (5);

  // Wait at most this long before initiating data transfer
  int32_t initial_probe_wait_ms = 250;

  uint32_t num_paths;
  app_path_id_t active_path;
  app_path_id_t previous_path;

  // Store information on each candidate path
  std::vector<PathState> path_states;

  // JSON to store results for visualization
  nlohmann::json results_json = nlohmann::json::object ();
  nlohmann::json results_states = nlohmann::json::array ();

  std::map<app_packet_id_t, BottleneckProbe> in_flight_probes;

  const uint16_t fps = 30;
  const Time frame_interval = Seconds (1.0 / fps); // how much time between frames
  uint32_t frame_no = 0; // number of the video frame
  uint32_t sequence_number = 0;

  Time round_trip_time = Seconds (0);
  CongestionControlPhase phase;
  DelayBasedController delay_based_estimator;
  LossBasedEstimator loss_based_estimator;

  ConnectionMetrics metrics;

  std::unique_ptr<webrtc::NetworkControllerInterface> network_controller;

  double A_r = 0; // send rate estimate by the receiver side loss based controller
  double A_s = 0; // send rate estimate by the sender side loss based controller
  double target_sendrate = 300'000 / 8; // in Bytes per second
  double previous_sendrate = 0; // Sendrate right before starting path shift, used to compute maintenance rate on old path

  double total_bytes_sent = 0; // total of app packets sent
  double total_bytes_arrived = 0; // total of app packets arrived

  bool in_path_transition = false;
  Time path_transition_end = Seconds (0);
  Time last_path_change = Seconds (0);
  Time last_A_r_update = Seconds (0);

  const Time update_interval = Seconds (0.25);
  const Time probe_interval = Seconds (0.25); // How often new probes are sent out
  uint16_t probe_simultaneous = 3; // How many paths to probe at the same time
  app_packet_id_t probe_id = 0; // Identifies a probe, not the packet though, that is probe_seq_no

  void Log (std::string msg, bool with_prefix = true);
  bool WaitForInitialProbes ();

public:
  CiaoApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
           host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
           uint32_t runtime_config);

  void StartAppTraffic ();

  void StopAppTraffic ();

  void RecordMetrics ();

  void SendProbes ();

  void Update ();

  /**
   * Returns a list of path indexes that are candidates for probing.
  */
  std::vector<app_path_id_t> FindProbeCandidates ();

  void SendProbeOnPath (app_path_id_t path_id);

  void ScheduleSend ();

  void SendFrameData (double sendrate, app_path_id_t path);

  void SendPacket (double payload_size, u_int16_t scion_header_bytes, app_path_id_t path);

  void ReceiveAppResponse (AppResp app_resp);

  void ReceiveProbeResponse (Scmp scmp, BottleneckProbe probe_resp);

  void ReceiveScmp (Scmp scmp);

  void UpdatePathCandidates ();

  void SwitchToPath (uint32_t new_path);

  void EndPathTransition ();

  void UpdateBWE ();

  void ScheduleControllerProcessInterval (Time &process_interval);

  void ResetController (double new_target_rate);

  void OnNetworkControlUpdate (webrtc::NetworkControlUpdate &update);

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
