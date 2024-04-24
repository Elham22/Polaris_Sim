/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 ETH Zuerich
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

namespace ns3 {

// Struct to store path information
struct PathStatistics
{
  uint16_t ecn;
  Time last_update; // time of last ecn mark
  Time last_scmp; // time of last scmp congestion response
  double score = -INFINITY;
  double latency; // observed latency
  double bandwidth; // estimated bandwidth
  double loss; // estimated loss
};

/**
 * RTC like application with some path selection smarts
*/
class RTCApp : public App
{
protected:
  uint32_t num_paths;
  app_path_id_t active_path;

  // vector to store information each path
  std::vector<PathStatistics> path_infos;

  std::vector<double> bitrates{10 * 0.7e6, 10 * 1.5e6, 10 * 5e6};
  uint32_t selected_bitrate = 0;
  const uint16_t fps = 30;

  double moving_average_weight = 0.75;
  double steering_treshold_u = 20;
  double steering_treshold_l = 0;

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

    // choose a random path to start with
    active_path = rand () % num_paths;
  }

  void
  HandleSCMP (ScmpReqOrResp scmp)
  {
    // TODO
    // Need information from host here about which path is affected
    // and then decide how exactly it influences the scoring
  }

  void
  ReceiveAppResponse (AppResp app_resp)
  {
    auto path_id = app_resp.path_id;
    std::cout << "[rtc] Receiving response on path " << path_id << std::endl;
    if (path_id != active_path)
      {
        std::cout << "[rtc] Receiving feedback about inactive (old) path: " << path_id << std::endl;
      }
    Time resp_time = MicroSeconds (app_resp.timestamp);

    // Very basic bandwidth estimation, this is basically just a lower bound
    // ...and apparently sometimes even negative (TODO)
    path_infos[path_id].bandwidth =
        app_resp.bytes_received /
        Seconds (resp_time - path_infos[path_id].last_update).GetSeconds ();

    path_infos[path_id].ecn = app_resp.ecn;
    path_infos[path_id].last_update = resp_time;
    path_infos[path_id].latency = app_resp.avg_latency;

    // Update loss with moving average
    path_infos[path_id].loss *= (1 - moving_average_weight);
    path_infos[path_id].loss += (moving_average_weight * app_resp.loss);

    UpdateScore (path_id);

    std::cout << "    Loss: " << app_resp.loss << std::endl
              << "    Latency: " << app_resp.avg_latency << std::endl
              << "    Bandwidth: " << path_infos[path_id].bandwidth << std::endl
              << "    Score: " << path_infos[path_id].score << std::endl;

    if (path_id == active_path)
      {
        Steer ();
      }
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
  Steer ()
  {
    // Compute the sigmoid of the score
    // NOTE: subject to change
    double alpha = 1. / (1. + exp (-path_infos[active_path].score));
    bool E_alpha = rand () % 100 < 100 * alpha;
    std::cout << "alpha " << alpha << std::endl;

    // if score lower than treshold_l, switch to a different random path
    if (path_infos[active_path].score < steering_treshold_l)
      {
        // with probability (1-alpha), pick a new random path
        if (E_alpha)
          {
            // Pick a random path from all other candidates, we can't afford to be picky now
            std::set<uint32_t> candidate_paths;
            for (uint32_t i = 0; i < num_paths; i++)
              {
                // TODO: add additional conditions, e.g., score > treshold_l
                if (i != active_path)
                  {
                    candidate_paths.insert (i);
                  }
              }
            if (candidate_paths.size () > 0)
              {
                uint32_t new_path = rand () % candidate_paths.size ();
                active_path = new_path;
                std::cout << "[rtc] Steering to a different path " << active_path << std::endl;
              }
          }
        else
          {
            // Lower the bitrate instead, if we can still go lower
            if (selected_bitrate > 0)
              {
                selected_bitrate--;
                std::cout << "[rtc] Lowering bitrate to " << bitrates[selected_bitrate]
                          << std::endl;
              }
          }
      }
    else if (path_infos[active_path].score > steering_treshold_u)
      {
        if (E_alpha)
          {
            // Increase sending rate if we can
            if (selected_bitrate < bitrates.size () - 1)
              {
                selected_bitrate++;
                std::cout << "[rtc] Increasing bitrate to " << bitrates[selected_bitrate]
                          << std::endl;
              }
          }
      }
    else
      {
        if (E_alpha)
          {
            // Pick a random path from all good ones
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
                std::cout << "[rtc] Steering to a different path " << active_path << std::endl;
              }
          }
      }
  }

  void
  StartAppTraffic ()
  {
    SendTraffic ();
  }

  void
  ProbeRandomPath ()
  {
    // TODO: implement a way to probe non-working paths, both for latency and
    // even to make a bandwidth estimation, e.g., by sending consecutive probe
    // pairs
  }

  void
  SendPacket (double packetSize, std::vector<const PathSegment *> path)
  {
    std::cout << "[rtc] Sending data packet via path " << active_path << std::endl;
    Payload payload;
    payload.app_data.app_id = app_id;
    payload.app_data.path_id = active_path;
    payload.app_data.seq_no = packet_id++;
    payload.app_data.timestamp = Simulator::Now ().ToInteger (Time::Unit::US);
    PayloadType payload_type = PayloadType::APPLICATION_DATA;
    host->SendAppPacket (this, payload, payload_type, packetSize * scale, path);
  }

  void
  SendTraffic ()
  {
    if (stopped)
      {
        return;
      }

    // Send packet of selected bitrate
    // TODO: implement realistic sending (with natural variation)
    SendPacket (bitrates[selected_bitrate] / fps, all_paths[active_path]);

    // Schedule next packet
    Time next = Seconds (1.0 / fps);
    Simulator::Schedule (next, &RTCApp::SendTraffic, this);
  }

  void
  StopAppTraffic ()
  {
    stopped = true;
  }

  void
  PrintResults ()
  {
    // TODO
  }

  /**
   * Override to throw error, we don't use probes but keep the existing code
  */
  void
  ReceiveProbeResponse (ProbeResp probe_resp)
  {
    NS_FATAL_ERROR ("[rtc-app] should not have received probe response");
  }
};

} // namespace ns3
