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
 * Author: Pascal Suter passuter@student.ethz.ch
 */

#ifndef SCION_SIMULATOR_APP_H
#define SCION_SIMULATOR_APP_H

#include "src/SCION/model/scion-packet.h"
#include "src/SCION/model/scion-host.h"
#include "src/core/model/simulator.h"

namespace ns3 {
class PathInfo
{
public:
  PathInfo (std::vector<const ns3::PathSegment *> path)
  {
    uint32_t n_hops = 0;
    for (auto seg : path)
      {
        n_hops += seg->hops.size ();
      }
    num_hops = n_hops;
    num_expected_responses =
        n_hops * 2 -
        2; // probe passes through 2 border router per hop, except for src & target AS, there only 1
    probe_sent_time = Simulator::Now ();
  }

  uint16_t ecn;
  u_int32_t num_hops;
  u_int32_t num_expected_responses;
  std::vector<ProbeResp> probe_responses;
  Time probe_sent_time;
  double latency;
  double score = -INFINITY;
  bool activeLossMeasured = false;
  double activeLoss = 0.0;

  double GetLoss (double *additional_score);
};
class App
{
public:
  App (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
       host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
       bool enable_logging)
      : dst_ia (app_dst_ia),
        dst_host_addr (app_dst_host_addr),
        host (host),
        app_id (app_id),
        ia_addr (ia_addr),
        all_paths (all_paths),
        enable_logging (enable_logging)
  {
  }

  ia_t dst_ia;
  host_addr_t dst_host_addr;
  static const uint16_t scale = 1; // scaling factor for app data packets

  virtual void StartAppTraffic ();
  void StopAppTraffic ();
  void StartAppTrafficDelayed (Time delay);
  void ReceiveProbeResponse (ProbeResp probe_resp);
  void ReceiveAppResponse (AppResp app_resp);
  virtual void PrintResults ();
  virtual void PrintPathInfo ();

protected:
  uint32_t m_pktSize = 1470 * 15; // Size of packets
  ScionHost *host;
  uint32_t app_id;
  app_packet_id_t packet_id = 0;
  ia_t ia_addr;
  //std::vector<const PathSegment *> active_path;
  std::vector<std::vector<const PathSegment *>> all_paths;
  //std::vector<std::vector<ProbeResp *>> probe_responses;
  bool probes_pending = false;
  bool first_probe_returned = false;
  int32_t best_path_id = -1;
  std::vector<PathInfo> *path_infos = NULL;
  std::vector<PathInfo> *path_infos_old = NULL;
  std::vector<std::pair<Time, AppResp>> app_responses;
  // bonus is added to active paths' scores to create a margin, preventing too frequent path switching
  // depending on the score function, subclasses can define a different bonus
  double active_path_bonus = 50;
  double active_latency = INFINITY;
  double active_loss = 1.0;
  double acceptable_loss = 1.0;
  Time next_scoring = Time (0);
  bool stopped = false;
  bool enable_logging = false;

  virtual bool rescore (double active_loss);

  virtual void GenerateAppTraffic ();
  virtual uint32_t ComputeExpectedBandwidth (uint32_t path_id); // bytes per second
  virtual void SendProbes ();
  void CheckResendProbes ();
  virtual void ComputeAllScores (bool triggered_by_probes);
  virtual bool isActivePath (int32_t path_id);
  virtual double ComputeScore (double latency, double loss, double additional_scoring,
                               uint32_t path_id, bool was_active);
  std::vector<const ns3::PathSegment *> GetPath ();
  virtual void SendData (uint32_t size, std::vector<const ns3::PathSegment *> path);
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H
