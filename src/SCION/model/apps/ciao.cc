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

#include "ciao.h"

#include "api/environment/environment_factory.h"
#include "api/transport/goog_cc_factory.h"

namespace ns3 {

void
CiaoApp::Log (std::string msg, bool with_prefix)
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

CiaoApp::CiaoApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
                  host_addr_t app_dst_host_addr,
                  std::vector<std::vector<const PathSegment *>> all_paths, uint32_t runtime_config)
    : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, runtime_config)
{
  // Setup the runtime configuration
  cfgLogging = ENABLE_LOGGING (runtime_config);
  cfgLossBwe = !(DISABLE_LOSS_BWE (runtime_config));
  cfgDelayBwe = !(DISABLE_DELAY_BWE (runtime_config));
  cfgPathSwitching = !(DISABLE_PATH_SWITCHING (runtime_config));

  // Config map overrides the runtime config arg
  cfgLogging = inputs.contains ("logging") ? inputs["logging"].get<bool> () : cfgLogging;
  cfgPathSwitching =
      inputs.contains ("path_switching") ? inputs["path_switching"].get<bool> () : cfgPathSwitching;
  path_shifting = inputs.contains ("path_shifting") ? inputs["path_shifting"].get<bool> () : false;
  path_change_margin = inputs.contains ("path_change_margin")
                           ? inputs["path_change_margin"].get<double> ()
                           : path_change_margin;
  path_switch_min_interval = inputs.contains ("path_switch_min_interval")
                                 ? Seconds (inputs["path_switch_min_interval"].get<double> ())
                                 : path_switch_min_interval;
  path_switch_min_candidacy = inputs.contains ("path_switch_min_candidacy")
                                  ? Seconds (inputs["path_switch_min_candidacy"].get<double> ())
                                  : path_switch_min_candidacy;

  // Initialize path infos
  num_paths = paths.size ();
  for (uint32_t i = 0; i < num_paths; i++)
    {
      PathMetric metric;
      path_metrics.push_back (metric);
    }

  metrics = TrafficMetrics (cfgLogging);

  webrtc::GoogCcFactoryConfig factory_config;
  factory_config.feedback_only = true;
  factory_config.network_state_estimator_factory = nullptr;

  webrtc::GoogCcNetworkControllerFactory factory =
      webrtc::GoogCcNetworkControllerFactory (std::move (factory_config));
  webrtc::Environment default_env = webrtc::EnvironmentFactory ().Create ();
  webrtc::NetworkControllerConfig config (default_env);

  config.constraints.at_time = TimestampNow ();

  network_controller = factory.Create (config);
  Time process_interval = MicroSeconds (factory.GetProcessInterval ().us ());
  ScheduleControllerProcessInterval (process_interval);
  Log ("Created GoogCC network controller with process interval " +
       std::to_string (process_interval.GetMilliSeconds ()) + " ms");

  // Choose a random path to start with
  active_path = rand () % num_paths;
  // active_path = 0; // TODO: For testing

  // Allow override via config map
  active_path =
      inputs.contains ("start_path") ? inputs["start_path"].get<uint32_t> () : active_path;

  std::cout << "CiaoApp " << app_id << " initialized" << std::endl;
  std::cout << "  Active path: " << active_path << std::endl;
  std::cout << "  Logging enabled: " << cfgLogging << std::endl;
  std::cout << "  Path switching enabled: " << cfgPathSwitching << std::endl;
  std::cout << "  Path shifting enabled: " << path_shifting << std::endl;
}

void
CiaoApp::StartAppTraffic ()
{

  // Start sending probes and video frames after a random delay, to avoid synchronization
  // Simulator::Schedule (MilliSeconds (5000) + RandomDelay (4000), &CiaoApp::ScheduleSend, this);
  // Simulator::Schedule (MilliSeconds (2000) + RandomDelay (1500), &CiaoApp::SendProbes, this);

  ScheduleSend ();
  SendProbes ();
  Update ();

  // Record metrics exactly at multiples of metrics_interval absolute simulation time
  uint64_t sched_abs_time_ms = Simulator::Now ().GetMilliSeconds () + metrics_interval_ms;
  sched_abs_time_ms = std::ceil (sched_abs_time_ms / metrics_interval_ms) * metrics_interval_ms;
  Time sched_rel_time = MilliSeconds (sched_abs_time_ms) - Simulator::Now ();
  std::cout << "Scheduling metrics recording at " << sched_abs_time_ms << " ms" << std::endl;
  std::cout << "Current time: " << Simulator::Now ().GetMilliSeconds () << " ms" << std::endl;
  Simulator::Schedule (sched_rel_time, &CiaoApp::RecordMetrics, this);
}

void
CiaoApp::RecordMetrics ()
{
  if (stopped)
    {
      return;
    }

  // Assert that time now is a multiple of metrics_interval
  NS_ASSERT_MSG (Simulator::Now ().GetMilliSeconds () % metrics_interval_ms == 0,
                 "Time now is not a multiple of metrics_interval: " +
                     std::to_string (Simulator::Now ().GetMilliSeconds ()) + " % " +
                     std::to_string (metrics_interval_ms));

  // Schedule the next metrics recording
  Simulator::Schedule (metrics_interval, &CiaoApp::RecordMetrics, this);

  // Wait until we have received at least one report
  if (!first_report_received)
    {
      return;
    }

  auto metric = path_metrics[active_path];

  (void) metrics.UpdateStatistics ();
  metrics.sendrate = target_sendrate;

  // Capture current state of the app
  RTCAppMetric state;
  state.timestamp = Simulator::Now ();
  state.active_path = active_path;
  state.sendrate = target_sendrate;
  state.latency = metric.latency;
  state.loss = metrics.GetFractionLoss ();
  state.A_s = A_s;
  state.A_r = A_r;
  state.bottleneck_share = metric.bottleneck_share;

  if (in_path_transition)
    {
      state.in_transition = true;
      state.sendrate = path_metrics[previous_path].sendrate + path_metrics[active_path].sendrate;
      state.oldrate = path_metrics[previous_path].sendrate;
      state.newrate = path_metrics[active_path].sendrate;
    }

  app_metrics.push_back (state);

  Log ("Current state:");
  for (uint32_t i = 0; i < num_paths; i++)
    {
      std::string prefix = (i == active_path) ? "    -> " : "       ";
      Log ("    Path " + std::to_string (i) +
               ", latency [ms]: " + std::to_string (path_metrics[i].latency / 1000.0) +
               ", loss: " + std::to_string (path_metrics[i].loss) +
               ", bottleneck_share: " + std::to_string (path_metrics[i].bottleneck_share),
           false);
    }
}

std::vector<app_path_id_t>
CiaoApp::FindProbeCandidates ()
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
      if (path_metrics[i].probed_last < probed_last_min)
        {
          candidates.clear ();
          candidates.push_back (i);
          probed_last_min = path_metrics[i].probed_last;
        }
      else if (path_metrics[i].probed_last == probed_last_min)
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
CiaoApp::SendProbeOnPath (app_path_id_t path_id)
{
  BottleneckProbe probe;
  probe.interface_id = -1; // Indicates no results contained yet
  probe.ia = ia_addr;
  probe.id = probe_id++;
  probe.seq_no = path_metrics[path_id].probe_seq_no++;

  Scmp scmp;
  scmp.type = SCMPType::PROBE;
  scmp.data = probe;
  scmp.code = 1; // Traffic class, video conferencing

  // Technically we'll have to live without these two fields, but there's no
  // good way around it for now
  scmp.app_id = app_id;
  scmp.path_id = path_id;

  in_flight_probes[probe.id] = probe;

  PayloadType payload_type = PayloadType::SCMP;
  Payload payload = scmp;
  host->SendAppPacket (this, payload, payload_type, sizeof (Scmp) + sizeof (BottleneckProbe),
                       paths[path_id]);
}

void
CiaoApp::SendProbes ()
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
          SendProbeOnPath (candidate);

          // NOTE: Set probed_last to the exact same time for all candidates
          // we probe now. This way, when we find candidates in the future
          // with the oldest probed_last, it's actually likely to find a set,
          // and not just a single oldest one.
          path_metrics[candidate].probed_last = now;
        }
    }

  // Schedule the next round of probing
  Simulator::Schedule (probe_interval +
                           RandomDelay ((probe_interval / 2).ToInteger (Time::Unit::MS)),
                       &CiaoApp::SendProbes, this);
}

void
CiaoApp::Update ()
{
  if (stopped)
    {
      return;
    }

  if (cfgPathSwitching && !in_path_transition)
    {
      UpdatePathCandidates ();
    }

  // Schedule the next update
  Simulator::Schedule (update_interval, &CiaoApp::Update, this);
}

void
CiaoApp::ReceiveScmp (Scmp scmp)
{
  if (scmp.type == SCMPType::PROBE_ECHO)
    {
      BottleneckProbe probe = std::get<BottleneckProbe> (scmp.data);
      ReceiveProbeResponse (scmp, probe);
    }
  else if (scmp.type == SCMPType::CONGESTION_ALERT)
    {
      CongestionAlert &alert = std::get<CongestionAlert> (scmp.data);
      Log ("Received congestion alert from " + std::to_string (alert.ia) + " on interface " +
           std::to_string (alert.interface_id));

      // TODO(wickip): Do this for all paths that share this bottleneck
      path_metrics[scmp.path_id].last_ciao_congestion_alert = Simulator::Now ();
    }
  else
    {
      Log ("Received unknown SCMP type: " + std::to_string ((int) scmp.type));
    }
}

void
CiaoApp::EndPathTransition ()
{
  last_path_change = Simulator::Now ();
  in_path_transition = false;
  Log ("Finished transition from path from " + std::to_string (previous_path) + " to " +
       std::to_string (active_path));

  // webrtc::TargetRateConstraints new_constraints;
  // webrtc::NetworkRouteChange route_change;
  // new_constraints.at_time = TimestampNow ();
  // new_constraints.starting_rate = webrtc::DataRate::BytesPerSec (target_sendrate);
  // route_change.at_time = TimestampNow ();
  // route_change.constraints = new_constraints;

  // (void) network_controller->OnNetworkRouteChange (route_change);
  // // (void) network_controller->OnNetworkRouteChange (route_change);

  // webrtc::NetworkStateEstimate estimate;
  // estimate.update_time = TimestampNow ();
  // estimate.link_capacity = webrtc::DataRate::BytesPerSec(target_sendrate);
  // (void) network_controller->OnNetworkStateEstimate(estimate);
}

void
CiaoApp::ScheduleSend ()
{
  if (stopped)
    {
      return;
    }

  if (in_path_transition && Simulator::Now () >= path_transition_end)
    {
      EndPathTransition ();
    }

  if (in_path_transition)
    {
      double transition_progress =
          ((Simulator::Now () - last_path_change) / (path_transition_end - last_path_change))
              .GetDouble ();

      NS_ASSERT_MSG (transition_progress >= 0 && transition_progress <= 1,
                     "Transition progress out of bounds: " + std::to_string (transition_progress));
      NS_ASSERT_MSG (target_sendrate > prev_sendrate,
                     "Target sendrate is not greater than previous sendrate");

      double ramp_up_rate = 0;

      if (path_transition_strategy == PathTransitionStrategy::SIGMOID)
        {
          // Use sigmoid function to gradually increase sending rate on the new path from 0 to 1 times the sendrate
          double sigmoid = 1 / (1 + std::exp (-10 * (transition_progress - 0.5)));

          ramp_up_rate = target_sendrate * sigmoid;
        }
      else if (path_transition_strategy == PathTransitionStrategy::CUBIC)
        {
          // Use cubic function to very quickly ramp up and then slow down towards the end
          double cubic = 1 - std::pow (1 - transition_progress, 3);

          ramp_up_rate = target_sendrate * cubic;
        }
      else // linear
        {
          ramp_up_rate = target_sendrate * transition_progress;
        }

      // If a new probe result comes back during the transition with a bottleneck share lower than
      // our ramp-up rate, clamp the rate and finish the transition early
      if (path_metrics[active_path].bottleneck_share < ramp_up_rate)
        {
          Log ("Bottleneck share from new probe result lower than ramp-up rate. Exiting transition "
               "early.");
          ramp_up_rate = path_metrics[active_path].bottleneck_share;
          target_sendrate = ramp_up_rate;
          EndPathTransition ();
        }

      path_metrics[active_path].sendrate = ramp_up_rate;
      SendFrameData (ramp_up_rate, active_path);

      // Send on the old path with the remaining rate
      double maintenance_rate = target_sendrate - ramp_up_rate;
      maintenance_rate = std::clamp (maintenance_rate, 0.0, prev_sendrate);
      // maintenance_rate = 0;
      path_metrics[previous_path].sendrate = maintenance_rate;
      if (maintenance_rate > 0)
        {
          SendFrameData (maintenance_rate, previous_path);
        }
    }
  else // on a single path
    {
      SendFrameData (target_sendrate, active_path);
    }

  frame_no++;

  // Introduce a random offset of up to 1ms when scheduling the next frame
  auto rand_offset = RandomDelay (1000);
  Simulator::Schedule (frame_interval + rand_offset, &CiaoApp::ScheduleSend, this);
}

void
CiaoApp::SendFrameData (double sendrate, app_path_id_t path)
{
  // sendrate / fps == (payload_data + header_overhead) * no_pkts

  auto available_bytes = sendrate / fps;

  uint16_t scion_header_bytes = ScionPacketHeaderSize (paths[path]);
  double max_payload_bytes = m_pktSize - sizeof (AppData) - scion_header_bytes;
  double packets_to_send = std::ceil ((double) available_bytes / max_payload_bytes);

  Log ("Sending " + std::to_string (available_bytes) + " bytes of frame " +
       std::to_string (frame_no) + " on path " + std::to_string (path));

  Time packet_interval = frame_interval / packets_to_send;
  Time packet_schedule_delay = Seconds (0);

  while (available_bytes > max_payload_bytes)
    {
      Simulator::Schedule (packet_schedule_delay, &CiaoApp::SendPacket, this, max_payload_bytes,
                           scion_header_bytes, path);
      packet_schedule_delay += packet_interval;
      available_bytes -= max_payload_bytes;
    }
  if (available_bytes > 0)
    {
      Simulator::Schedule (packet_schedule_delay, &CiaoApp::SendPacket, this, max_payload_bytes,
                           scion_header_bytes, path);
    }

  frame_no++;
}

void
CiaoApp::SendPacket (double payload_bytes, u_int16_t scion_header_bytes, app_path_id_t path)
{
  AppData data{
      .app_id = app_id,
      .path_id = path,
      .seq_no = path_metrics[path].seq_no++,
      .frame_no = frame_no,
      .timestamp = Simulator::Now ().ToInteger (Time::Unit::US),
  };
  Payload payload = data;
  PayloadType payload_type = PayloadType::APPLICATION_DATA;
  host->SendAppPacket (this, payload, payload_type, payload_bytes, paths[path]);
  Log ("Sending packet with frame_no " + std::to_string (frame_no) + " and seq_no " +
       std::to_string (data.seq_no) + " on path " + std::to_string (path));

  double scion_pkt_total_bytes = payload_bytes + scion_header_bytes;

  PacketRecord record{
      .time_sent = Simulator::Now (),
      .time_received = Time::Min (),
      .seq_no = data.seq_no,
      .frame_no = frame_no,
      .size = static_cast<uint16_t> (scion_pkt_total_bytes),
  };

  (void) metrics.OnSentPacket (record);

  // Track in-flight packets for congestion controller on active path
  if (path == active_path)
    {
      webrtc::SentPacket sent_packet;
      sent_packet.sequence_number = data.seq_no;
      sent_packet.send_time = TimestampNow ();

      path_metrics[path].in_flight_bytes += scion_pkt_total_bytes;
      total_bytes_sent += scion_pkt_total_bytes;

      sent_packet.size = webrtc::DataSize::Bytes (scion_pkt_total_bytes);
      sent_packet.data_in_flight = webrtc::DataSize::Bytes (path_metrics[path].in_flight_bytes);
      // sent_packet.prior_unacked_data // TODO
      path_metrics[path].in_flight_packets.push_back (sent_packet);
      // Update the network controller with the sent packet
      (void) network_controller->OnSentPacket (sent_packet);
    }
}

void
CiaoApp::ReceiveAppResponse (AppResp app_resp)
{
  first_report_received = true;
  auto path_id = app_resp.path_id;
  std::shared_ptr<PacketsReport> report = app_resp.packets_report;
  total_bytes_arrived += report->total_bytes;

  if (path_id != active_path)
    {
      Log ("Receiving response on inactive (old) path: " + std::to_string (path_id) +
           " with sequence numbers " + std::to_string (report->packets.front ().seq_no) + " to " +
           std::to_string (report->packets.back ().seq_no));
      // Don't process responses on inactive paths
      return;
    }

  Log ("Processing report on active path " + std::to_string (path_id) + " with sequence numbers " +
       std::to_string (report->packets.front ().seq_no) + " to " +
       std::to_string (report->packets.back ().seq_no));

  Time resp_time = MicroSeconds (app_resp.timestamp);
  path_metrics[path_id].last_report = resp_time;

  path_metrics[path_id].ecn = app_resp.ecn;
  path_metrics[path_id].latency = app_resp.avg_latency;

  // RTT update
  Time send_delay = report->packets.back ().time_received - report->packets.back ().time_sent;
  Time receive_delay = Simulator::Now () - MicroSeconds (app_resp.timestamp);
  round_trip_time = send_delay + receive_delay;

  metrics.OnReceivedPackets (report->packets);

  // loss_based_estimator.FeedReport (report);
  // delay_based_estimator.FeedReport (report);
  // path_metrics[path_id].loss = loss_based_estimator.GetLoss ();

  // if report empty
  if (report->packets.empty ())
    {
      Log ("WARN: Empty report received");
      return;
    }

  // Re-construct a valid TransportPacketsFeedback
  std::vector<webrtc::PacketResult> packet_feedbacks;

  std::sort (report->packets.begin (), report->packets.end (),
             [] (auto a, auto b) { return a.seq_no < b.seq_no; });

  // highest seq no
  uint32_t highest_seq_no = report->packets.back ().seq_no;

  // Create a PacketResult for each in-flight packet
  for (webrtc::SentPacket packet : path_metrics[path_id].in_flight_packets)
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

          // If the in-flight packet is in the report, set the receive time
          if (it != report->packets.end ())
            {
              packet_result.receive_time =
                  webrtc::Timestamp::Millis (it->time_received.GetMilliSeconds ());
            }
          else
            {
              // Consider packet lost (receive_time = inf)
            }

          packet_feedbacks.push_back (packet_result);
        }
    }

  // Clean up in-flight packets
  path_metrics[path_id].in_flight_packets.erase (
      std::remove_if (path_metrics[path_id].in_flight_packets.begin (),
                      path_metrics[path_id].in_flight_packets.end (),
                      [this, path_id, highest_seq_no] (webrtc::SentPacket packet) {
                        if (packet.sequence_number <= highest_seq_no)
                          {
                            path_metrics[path_id].in_flight_bytes -= packet.size.bytes ();
                            return true;
                          }
                        return false;
                      }),
      path_metrics[path_id].in_flight_packets.end ());

  webrtc::TransportPacketsFeedback feedback;
  feedback.feedback_time = TimestampNow ();
  feedback.packet_feedbacks = std::move (packet_feedbacks);
  feedback.data_in_flight = webrtc::DataSize::Bytes (path_metrics[path_id].in_flight_bytes);

  webrtc::NetworkControlUpdate update =
      network_controller->OnTransportPacketsFeedback (std::move (feedback));

  if (in_path_transition)
    {
      // Return at this point, we're not going to use the controller estimate or update path candidates
      return;
    }

  OnNetworkControlUpdate (update);
}

void
CiaoApp::UpdateBWE ()
{

  A_r = delay_based_estimator.GetRate ();
  target_sendrate = A_r;
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
          target_sendrate = A_s;
        }
    }
  else if (phase == CongestionControlPhase::CONGESTION_AVOIDANCE)
    {
      // Now we incorporate the delay based estimate too
      A_r = delay_based_estimator.GetRate ();
      Log ("Receiver estimate: " + std::to_string (A_r));

      target_sendrate = A_s;

      // If we got an estimate from the receiver, use it
      if (cfgDelayBwe && A_r > 0)
        {
          if (A_r < A_s)
            {
              Log ("Receiver estimate lower than sender estimate: " + std::to_string (A_r) + " < " +
                   std::to_string (A_s) + ". Using receiver estimate.");
              target_sendrate = A_r;
              loss_based_estimator.LimitRate (1 * target_sendrate);
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
    }
}

void
CiaoApp::ReceiveProbeResponse (Scmp scmp, BottleneckProbe probe)
{

  // Find the corresponding probe
  auto it = in_flight_probes.find (probe.id);
  if (it == in_flight_probes.end ())
    {
      Log ("Received probe response for unknown probe id " + std::to_string (probe.id));
      return;
    }
  // Remove the probe from the in-flight list
  in_flight_probes.erase (it);

  if (probe.interface_id == -1)
    {
      Log ("Received probe with no results, ignoring");
      return;
    }

  auto &path_id = scmp.path_id;
  auto &path_m = path_metrics[path_id];

  // Store the probe result in the path metric
  path_m.last_probe_echo = probe;
  path_m.last_probe_echo_time = Simulator::Now ();

  if (path_id != active_path)
    {
      // TODO(wickip): We can compute this if we store the send times for probes
      path_m.latency = 200000; // 200ms
    }

  // Update path info
  path_m.bottleneck_num_flows = probe.bottleneck_num_flows;

  // Probe contains fair share in Gbps, convert to Bps
  path_m.bottleneck_share = probe.bottleneck_share * 1e9 / 8;

  // The fair share on the active path needs to be adjusted for the fact that
  // bottleneck_no_flows already includes a flow from this application.
  // For example, on a path that has a bottleneck link with 1Gbps and has two
  // flows from other applications, what we get back in the probe is 1Gbps / (2+1) = 333Mbps
  // The division by (2+1) is because it's the fair share we would get if we
  // were to also start sending via this path. But if we ARE already sending
  // through this path, the fair share is actually 1Gbps / (2) = 500Mbps, so
  // we need account for this ourselves.
  // if (path_id == active_path && probe.bottleneck_num_flows > 1 && Simulator::Now() > last_path_change + MilliSeconds(100))
  //   {
  //     auto correction = probe.bottleneck_num_flows / (probe.bottleneck_num_flows - 1);
  //     path_m.bottleneck_share *= correction;
  //   }
}

void
CiaoApp::UpdatePathCandidates ()
{
  bool is_active_path_usable =
      path_metrics[active_path].last_report > Simulator::Now () - path_alive_treshold &&
      path_metrics[active_path].loss < 0.9 &&
      path_metrics[active_path].last_ciao_congestion_alert <
          Simulator::Now () - ciao_congestion_alert_timeout;

  // Choose as candidates all paths that have a significantly higher
  // bottleneck_share than what is our current send rate. We also include the active
  // path, to provide a chance to stay on the current path and desynchronize
  // applications. A nice side effect is that the more better paths are
  // available, the more likely we are to switch. This is good, because if
  // there's just one better path, we don't want all applications to switch to
  // it at the same time. Whereas if we have many, applications are likely to
  // spread out.
  std::vector<uint32_t> final_candidates;
  for (uint32_t i = 0; i < num_paths; i++)
    {
      PathMetric &path_m = path_metrics[i];

      // The active path itself is always a candidate unless it becomes unusable
      if (i == active_path && is_active_path_usable)
        {
          final_candidates.push_back (i);
          continue;
        }

      // If we saw a recent C-CA on this path, exclude it from candidacy
      if (path_m.last_ciao_congestion_alert > Simulator::Now () - ciao_congestion_alert_timeout)
        {
          path_m.is_candidate = false;
          Log ("Excluding path switch candidate: " + std::to_string (active_path) + " (from " +
               std::to_string (i) + "). Recent congestion alert");
          continue;
        }

      // If the probe result is not fresh enough, this is not a candidate
      if (!path_m.HasFreshProbeResultsSince (last_path_change))
        {
          path_m.is_candidate = false;
          Log ("Excluding path switch candidate: " + std::to_string (active_path) + " (from " +
               std::to_string (i) + "). No fresh probe results");
          continue;
        }

      // If the estimated bottleneck share is not high enough, this is not a candidate
      if (is_active_path_usable && path_m.bottleneck_share < target_sendrate * path_change_margin)
        {
          path_m.is_candidate = false;
          Log ("Excluding path switch candidate: " + std::to_string (active_path) + " (from " +
               std::to_string (i) + "). Estimated fair share is not high enough " +
               std::to_string (path_m.bottleneck_share) + " < " + std::to_string (target_sendrate));
          continue;
        }

      Log ("Potential candidate path: " + std::to_string (active_path) + " (from " +
           std::to_string (i) + ").");
      // We have found a candidate path
      if (!path_m.is_candidate)
        {
          path_m.is_candidate = true;
          path_m.is_candidate_since = Simulator::Now ();
        }

      // If path has been candidate for long enough, select it for final list
      if (path_m.is_candidate_since <= Simulator::Now () - path_switch_min_candidacy)
        {
          Log ("Adding final path candidate: " + std::to_string (active_path) + " to " +
               std::to_string (i) + ". Estimated fair share is higher than current bitrate " +
               std::to_string (path_m.bottleneck_share) + " > " + std::to_string (target_sendrate));
          final_candidates.push_back (i);
        }
    }

  // Avoid switching paths too often if possible
  if (is_active_path_usable)
    {
      if (Simulator::Now () - last_path_change < path_switch_min_interval)
        {
          Log ("Waiting before switching paths again");
          return;
        }
    }
  else
    {
      Log ("Active path found to be unusable. Skipping the minimum path switch interval");
    }

  if (final_candidates.empty ())
    {
      Log ("WARNING: No candidate path available!");
      return;
    }

  // Pick a candidate at random
  uint32_t new_path = final_candidates.at (rand () % final_candidates.size ());
  SwitchToPath (new_path);
}

void
CiaoApp::SwitchToPath (uint32_t new_path)
{
  last_path_change = Simulator::Now ();
  if (new_path == active_path)
    {
      Log ("Staying on current path " + std::to_string (active_path));
      return;
    }
  Log ("Switching from path " + std::to_string (active_path) + " to " + std::to_string (new_path));

  previous_path = active_path;
  active_path = new_path;

  path_metrics[active_path].in_flight_bytes = 0;
  path_metrics[active_path].in_flight_packets.clear ();

  double new_rate = path_metrics[new_path].bottleneck_share;

  // loss_based_estimator.Reset ();
  // delay_based_estimator.SetPhase (CongestionControlPhase::STARTUP);

  // Reset candidacy of all paths
  for (size_t i = 0; i < num_paths; i++)
    {
      path_metrics[i].is_candidate = false;
    }

  if (path_shifting)
    {
      Time duration = MilliSeconds (200) + 2 * round_trip_time; // TODO: use RTT of new path
      path_transition_end = Simulator::Now () + duration;
      // path_transition_end = Simulator::Now () + Seconds (10); // TODO: For testing
      Log ("Starting path transition from " + std::to_string (previous_path) + " to " +
           std::to_string (active_path) +
           ", duration: " + std::to_string (duration.GetMilliSeconds ()) + " ms");
      prev_sendrate = target_sendrate;
      target_sendrate = path_metrics[new_path].bottleneck_share;
      path_metrics[new_path].sendrate = 0;
      path_metrics[previous_path].sendrate = prev_sendrate;
      target_sendrate = new_rate;
      in_path_transition = true;
      return;
    }

  // Update congestion controller
  webrtc::TargetRateConstraints new_constraints;
  webrtc::NetworkRouteChange route_change;
  new_constraints.at_time = TimestampNow ();
  new_constraints.starting_rate = webrtc::DataRate::BytesPerSec (new_rate);
  // new_constraints.min_data_rate = webrtc::DataRate::KilobitsPerSec (300);
  route_change.at_time = TimestampNow ();
  route_change.constraints = new_constraints;

  (void) network_controller->OnNetworkRouteChange (route_change);
}

// Return a WebRTC timestamp with the current simulation time
webrtc::Timestamp
CiaoApp::TimestampNow ()
{
  return webrtc::Timestamp::Micros (Simulator::Now ().GetMicroSeconds ());
}

void
CiaoApp::StopAppTraffic ()
{
  stopped = true;
}

std::string
CiaoApp::InfoString ()
{
  std::string info = "Ciao";

  // Ciao without path switching is basically GCC
  if (!cfgPathSwitching)
    {
      info = "GCC";
    }

  return info;
}

void
CiaoApp::PrintResults ()
{
  nlohmann::json j;
  j["app_id"] = app_id;
  j["app_type"] = InfoString ();
  j["src_ia"] = ia_addr;
  j["dst_ia"] = dst_ia;
  j["dst_host_addr"] = dst_host_addr;
  j["bytes_sent"] = total_bytes_sent;
  j["bytes_received"] = total_bytes_arrived;

  nlohmann::json j_states;
  for (RTCAppMetric state : app_metrics)
    {
      nlohmann::json j_state;
      j_state["time"] = state.timestamp.ToInteger (Time::Unit::MS);
      j_state["sendrate"] = state.sendrate / 1e6;
      j_state["latency"] = state.latency / 1000.0;
      j_state["loss"] = state.loss;
      j_state["active_path"] = state.active_path;
      j_state["bottleneck_share"] = state.bottleneck_share / 1e6;
      j_state["in_transition"] = state.in_transition;
      j_state["oldrate"] = state.oldrate / 1e6;
      j_state["newrate"] = state.newrate / 1e6;
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

bool
PathMetric::HasFreshProbeResultsSince (Time t)
{
  // For the result to be fresh, it must be from a probe initiated after t +
  // latency to account for any potential changes to the bottleneck share
  // incurred by a path switch
  return last_probe_echo_time > t + MicroSeconds (latency);
}

/**
   * Override to throw error, we don't use probes but keep the existing code
  */
void
CiaoApp::ReceiveProbeResponse (ProbeResp probe_resp)
{
  NS_FATAL_ERROR ("[ciao] legacy probe response not supported");
}

/**
   * @return a random time between -N and N where N is an integer parameter in ps
   */
Time
CiaoApp::RandomDelay (int N)
{
  return PicoSeconds (rand () % (2 * N) - N);
}

void
CiaoApp::ScheduleControllerProcessInterval (Time &process_interval)
{
  Simulator::Schedule (process_interval, &CiaoApp::ScheduleControllerProcessInterval, this,
                       process_interval);

  if (!first_report_received)
    {
      return;
    }
  webrtc::ProcessInterval process_interval_msg;
  process_interval_msg.at_time = TimestampNow ();
  webrtc::NetworkControlUpdate update =
      network_controller->OnProcessInterval (process_interval_msg);
  OnNetworkControlUpdate (update);
}

void
CiaoApp::OnNetworkControlUpdate (webrtc::NetworkControlUpdate &update)
{
  // Don't use the controller estimate during path transition
  if (in_path_transition)
    {
      return;
    }

  if (update.target_rate.has_value ())
    {
      webrtc::TargetTransferRate target_rate = update.target_rate.value ();
      auto estimate = target_rate.target_rate.bps () / 8;
      Log ("Received target rate update: " + std::to_string (estimate));

      webrtc::GoogCcNetworkController *goog_cc_network_controller =
          dynamic_cast<webrtc::GoogCcNetworkController *> (network_controller.get ());
      update = goog_cc_network_controller->GetNetworkState (TimestampNow ());
      if (update.target_rate.has_value ())
        {
          if (target_rate.target_rate != update.target_rate.value ().target_rate)
            {
              Log ("Warning: GetNetworkState returned different target rate: " +
                   std::to_string (update.target_rate.value ().target_rate.bps () / 8));

              target_rate = update.target_rate.value ();
              estimate = target_rate.target_rate.bps () / 8;
            }
        }

      double alpha = 0.5;
      target_sendrate = alpha * target_sendrate + (1 - alpha) * estimate;
    }
}

} // namespace ns3
