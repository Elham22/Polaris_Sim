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

public:
  RTCApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
          host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
          bool enable_logging)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, enable_logging)
  {
    num_paths = all_paths.size ();

    // initialize path infos
    for (int i = 0; i < num_paths; i++)
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
  }

  void
  ReceiveAppResponse (AppResp app_resp)
  {
    auto path_id = app_resp.path_id;
    if (path_id != active_path)
      {
        std::cout << "Receiving feedback about inactive (old) path" << std::endl;
      }
    Time resp_time = MicroSeconds (app_resp.timestamp);
    path_infos[path_id].bandwidth =
        app_resp.bytes_received /
        Seconds (resp_time - path_infos[path_id].last_update).GetSeconds ();
    path_infos[path_id].ecn = app_resp.ecn;
    path_infos[path_id].last_update = resp_time;
    path_infos[path_id].latency = app_resp.avg_latency;
    path_infos[path_id].loss = app_resp.loss;
  }

  void
  StartAppTraffic ()
  {
    SendTraffic ();
  }

  void
  ProbeRandomPath ()
  {
  }

  void
  SendPacket (double packetSize, std::vector<const PathSegment *> path)
  {
    Payload payload;
    payload.app_data.app_id = app_id;
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
