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
    probe_sent_time = Simulator::Now ();
  }

  u_int32_t num_hops;
  std::unordered_map<ia_t, ProbeResp> probe_responses;
  Time probe_sent_time;
  Time latency;
  double score = -INFINITY;

};
class App
{
public:
  App (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia, host_addr_t app_dst_host_addr,
      std::vector<std::vector<const PathSegment *>> all_paths)
  : dst_ia (app_dst_ia),
    dst_host_addr (app_dst_host_addr),
    host (host),
    app_id (app_id),
    ia_addr (ia_addr),
    all_paths (all_paths)
  {
  }

  ia_t dst_ia;
  host_addr_t dst_host_addr;
  
  void StartAppTraffic ();
  void ReceiveProbeResponse (ia_t src_ia, host_addr_t src_addr, ProbeResp probe_resp);
  virtual void PrintResults ();

protected:
  ScionHost *host;
  uint32_t app_id;
  ia_t ia_addr;
  //std::vector<const PathSegment *> active_path;
  std::vector<std::vector <const PathSegment *>> all_paths;
  //std::vector<std::vector<ProbeResp *>> probe_responses;
  bool probes_pending = false;
  bool first_probe_returned = false;
  int32_t best_path_id = -1;
  int32_t best_path_id_old = -1;
  std::vector<PathInfo> *path_infos = NULL;
  std::vector<PathInfo> *path_infos_old = NULL;

  virtual void GenerateAppTraffic ();
  uint32_t ComputeExpectedBandwidth ();
  void SendProbes ();
  void CheckResendProbes ();
  void ComputeAllScores ();
  virtual double ComputeScore (PathInfo path_info);
  std::vector<const ns3::PathSegment *> GetPath ();
  void SendData (uint32_t size, std::vector<const ns3::PathSegment *> path);
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H