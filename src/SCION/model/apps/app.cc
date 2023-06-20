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


#include "src/SCION/model/scion-core-as.h"
#include "src/SCION/model/scion-host.h"
#include "app.h"

namespace ns3 {

void
App::StartAppTraffic ()
{
  if (dst_ia == ia_addr)
    {
      GenerateAppTraffic();
      return;
    }

  if (best_path_id != -1)
    {
      GenerateAppTraffic();
    }
  else
    {
      if (!probes_pending)
        {
          SendProbes();
        }
      Simulator::Schedule (MilliSeconds (100), &App::StartAppTraffic, this);
    }
}

void 
App::GenerateAppTraffic ()
{
  // std::cout << "GenerateAppTraffic called" << std::endl;
  for (uint i = 0; i < 10; i++)
    {
      Simulator::Schedule(MilliSeconds(i), &App::SendData, this, 1024, GetPath ());
    }
}

void
App::SendData (uint32_t size, std::vector<const ns3::PathSegment *> path)
{
  // sends the data as a single packet. Large data should be split into multiple packets in this function.
  Payload payload;
  PayloadType payload_type = PayloadType::APPLICATION_DATA;
  host->SendAppPacket(this, payload, payload_type, size, path);
}

/**
 * Computes the expected bandwidth that is advertised in the probes.
 * Subclasses should override this function.
*/
uint32_t
App::ComputeExpectedBandwidth ()
{
  return 10*1024;
}

void
App::SendProbes ()
{
  if (dst_ia == ia_addr)
    {
      return;
    }
  if (path_infos_old)
    {
      delete path_infos_old;
    }

  path_infos_old = path_infos;
  probes_pending = true;
  first_probe_returned = false;
  path_infos = new std::vector<PathInfo>();
  
  std::vector<uint8_t> shortcuts;
  auto expected_bandwidth = ComputeExpectedBandwidth ();

  for (uint i = 0; i < all_paths.size(); i++)
    {
      PayloadType payload_type = PayloadType::QOS_PROBE_REQ;
      Payload payload;
      payload.probe_req.app_id = app_id;
      payload.probe_req.probe_id = i; // TODO use unique probe_ids and map them to the paths
      payload.probe_req.expected_bandwidth = expected_bandwidth;
      
      path_infos->push_back(PathInfo(all_paths.at (i)));
      host->SendAppPacket(this, payload, payload_type, sizeof (ProbeReq), all_paths.at (i));
    }
  
  Simulator::Schedule (Seconds(3), &App::CheckResendProbes, this);
}

void
App::ReceiveProbeResponse (ProbeResp probe_resp) 
{
  if (probe_resp.src_ia == dst_ia && probe_resp.src_host_addr == dst_host_addr)
    {
      path_infos->at(probe_resp.probe_id).latency = Time::FromInteger(probe_resp.time_recv, Time::Unit::MS) - path_infos->at(probe_resp.probe_id).probe_sent_time;
    }
  else
    {
      path_infos->at(probe_resp.probe_id).probe_responses.push_back(probe_resp);
    }

  if (!App::first_probe_returned)
    {
      if (probe_resp.src_ia == dst_ia && probe_resp.src_host_addr == dst_host_addr)
        {
          // wait a bit for other probes before computing the score
          Simulator::Schedule(Seconds(3), &App::ComputeAllScores, this);
          first_probe_returned = true;
        }
    }
  else if (!probes_pending)
    {
      // recompute path scores. If probes are pending and this probe is not the first, no need to recompute as ComputeAllScores should be scheduled.
      path_infos->at(probe_resp.probe_id).score = ComputeScore (path_infos->at(probe_resp.probe_id));
    }
}

void
App::CheckResendProbes ()
{
  // resend probes if none returned
  if (App::probes_pending && !App::first_probe_returned)
    {
      SendProbes();
    }
}

bool
App::isActivePath (int32_t path_id)
{
  if (probes_pending)
    {
      return best_path_id_old == path_id;
    }
  else
    {
      return best_path_id == path_id;
    }
}

void
App::ComputeAllScores ()
{
  double max_score = - INFINITY;
  uint32_t max_id = 0;
  for (uint32_t i = 0; i < path_infos->size (); i++)
  {
    auto path_info = path_infos->at (i);
    path_info.score = ComputeScore (path_info);
    if (isActivePath (i))
      {
        path_info.score += active_path_bonus;
      }
    std::cout << "path_id " << i << " score: " << path_info.score;

    if (path_info.score > max_score)
      {
        std::cout << " new max";
        max_score = path_info.score;
        max_id = i;
      }
    std::cout << std::endl;
  }
  best_path_id = max_id;
  if (probes_pending)
    {
      probes_pending = false;
      Simulator::Schedule (Minutes(5), &App::SendProbes, this);
    }
}

/**
 * Computes the score for a path given it's path info.
 * Basic implementation gives highest score for path with least latency.
 * Subclasses should implement score functions that work best for their applications.
*/
double
App::ComputeScore (PathInfo path_info)
{
  double punish = 0.0;
  if (path_info.num_expected_responses > path_info.probe_responses.size())
    {
      /*std::cout << path_info.probe_responses.size() << "/" << path_info.num_expected_responses << " probe responses arrived [";
      for (auto probe : path_info.probe_responses)
        {
          std::cout << probe.src_ia << ":" << probe.src_host_addr << ", ";
        }
      std::cout << "]" << std::endl;*/
      punish = -100.0;
    }

  if (path_info.latency != 0)
    {
      return punish - path_info.latency.ToDouble(Time::Unit::MS);
    }
  return - INFINITY;
}

/**
 * Gets the path for which the best score was computed. If probes are pending, it will return the best path of the previous set of probes.
 * Currently does not allow multipath.
*/
std::vector<const ns3::PathSegment *>
App::GetPath ()
{
  if (probes_pending)
    {
      return all_paths.at (best_path_id_old);
    }
  else
    {
      return all_paths.at (best_path_id);
    }
}

void
App::PrintResults ()
{
  std::cout << "App id " << app_id << ": Basic app has no evaluation." << std::endl;
}

} // namespace ns3