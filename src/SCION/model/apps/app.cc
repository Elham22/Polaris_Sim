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

double
PathInfo::GetLoss (double *additional_scoring)
{
  if (activeLossMeasured)
    {
      return activeLoss;
    }

  double noloss = 1.;
  for (auto probe : probe_responses)
    {
      noloss = noloss * (1. - probe.expected_loss);
    }
  uint32_t missing_probes = num_expected_responses - probe_responses.size ();
  *additional_scoring -= missing_probes * 50; // punish missing probes
  return 1. - noloss;
}

void
App::StartAppTrafficDelayed (Time delay)
{
  Simulator::Schedule (delay, &App::StartAppTraffic, this);
}

void
App::StartAppTraffic ()
{
  if (dst_ia == ia_addr)
    {
      GenerateAppTraffic ();
      return;
    }

  if (best_path_id != -1)
    {
      // path selection ran once, can start to generate traffic.
      GenerateAppTraffic ();
    }
  else
    {
      if (!probes_pending)
        {
          SendProbes ();
        }
      Simulator::Schedule (Seconds (1), &App::StartAppTraffic, this);
    }
}

void
App::StopAppTraffic ()
{
  stopped = true;
}

void
App::GenerateAppTraffic ()
{
  // std::cout << "GenerateAppTraffic called" << std::endl;
  for (uint i = 0; i < 10; i++)
    {
      Simulator::Schedule (MilliSeconds (i), &App::SendData, this, 1024, GetPath ());
    }
}

void
App::SendData (uint32_t size, std::vector<const ns3::PathSegment *> path)
{
  AppData app_data;
  // set to 0 for backwards compatibility, existing code didn't keep state per path
  app_data.path_id = 0;
  app_data.app_id = app_id;
  app_data.frame_no = 0;
  app_data.seq_no = packet_id++;
  app_data.timestamp = Simulator::Now ().ToInteger (Time::Unit::US);
  Payload payload = app_data;
  PayloadType payload_type = PayloadType::APPLICATION_DATA;
  host->SendAppPacket (this, payload, payload_type, size * scale, path);
}

/**
 * Computes the expected bandwidth that is advertised in the probes.
 * Subclasses should override this function.
*/
uint32_t
App::ComputeExpectedBandwidth (uint32_t path_id)
{
  return 10 * 1024;
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
  path_infos = new std::vector<PathInfo> ();

  std::vector<uint8_t> shortcuts;

  for (uint i = 0; i < all_paths.size (); i++)
    {
      PayloadType payload_type = PayloadType::QOS_PROBE_REQ;
      ProbeReq probe_req;
      probe_req.app_id = app_id;
      probe_req.probe_id = i; // TODO use unique probe_ids and map them to the paths
      probe_req.expected_bandwidth = ComputeExpectedBandwidth (i);
      Payload payload = probe_req;

      path_infos->push_back (PathInfo (all_paths.at (i)));
      host->SendAppPacket (this, payload, payload_type, sizeof (ProbeReq), all_paths.at (i));
    }

  Simulator::Schedule (Seconds (3), &App::CheckResendProbes, this);
}

void
App::ReceiveProbeResponse (ProbeResp probe_resp)
{
  if (probe_resp.src_ia == dst_ia && probe_resp.src_host_addr == dst_host_addr)
    {
      path_infos->at (probe_resp.probe_id).latency =
          probe_resp.time_recv -
          path_infos->at (probe_resp.probe_id).probe_sent_time.ToInteger (Time::Unit::MS);
    }
  else
    {
      path_infos->at (probe_resp.probe_id).probe_responses.push_back (probe_resp);
    }

  if (!App::first_probe_returned)
    {
      if (probe_resp.src_ia == dst_ia && probe_resp.src_host_addr == dst_host_addr)
        {
          // wait a bit for other probes before computing the score
          Simulator::Schedule (Seconds (1), &App::ComputeAllScores, this, true);
          first_probe_returned = true;
        }
    }
}

void
App::CheckResendProbes ()
{
  // resend probes if none returned
  if (App::probes_pending && !App::first_probe_returned)
    {
      SendProbes ();
    }
}

bool
App::isActivePath (int32_t path_id)
{
  return best_path_id == path_id;
}

void
App::ComputeAllScores (bool triggered_by_probes)
{
  /*bool printScore = host->GetLocalAddress() == 2 && app_id <= 2;
  if (printScore)
    {
      std::cout << "app " << app_id << " computing all scores at " << Simulator::Now ().ToInteger (Time::Unit::MS)
                << "(" << Simulator::Now ().ToDouble (Time::Unit::MIN) << " min), previous best path " << best_path_id << std::endl;
    }*/
  double max_score = -INFINITY;
  int32_t max_id = 0;
  auto path_info_container = path_infos;
  if (probes_pending && !triggered_by_probes)
    {
      path_info_container =
          path_infos_old; // probing is currently ongoing, current path_infos might be incomplete so use old path_infos.
    }

  for (uint32_t i = 0; i < path_info_container->size (); i++)
    {
      auto path_info = path_info_container->at (i);
      /*if (printScore)
      {
        std::cout << "path_id " << i;
      }*/
      if (isActivePath (i))
        {
          path_info.score = ComputeScore (path_info.latency, path_info.activeLoss, 0, i, true);
        }
      else
        {
          double additional_score = -300; // punishment for choosing a different path
          double loss = path_info.GetLoss (&additional_score);
          path_info.score = ComputeScore (path_info.latency, loss, additional_score, i,
                                          path_info.activeLossMeasured);
        }
      /*if (printScore)
      {
        std::cout << "score: " << path_info.score;
      }*/

      if (path_info.score > max_score)
        {
          /*if (printScore)
          {
            std::cout << " new max";
          }*/
          max_score = path_info.score;
          max_id = i;
        }
      /*if (printScore)
      {
        std::cout << std::endl;
      }*/
    }

  if (best_path_id != max_id)
    {
      best_path_id = max_id;
      active_loss = 0.;
      active_latency = 0.;
    }

  if (probes_pending && triggered_by_probes)
    {
      probes_pending = false;
      Simulator::Schedule (Seconds (180), &App::SendProbes, this);
    }
  // Backoff to avoid too frequent score computation. When path is switched, packets over old path might still arrive for a short time.
  //next_scoring = Simulator::Now () + Seconds (3);

  // randomize delay time to introduce non-determinism. Breaks cycles where apps jump to same paths every time
  auto var = CreateObjectWithAttributes<UniformRandomVariable> ("Min", DoubleValue (0), "Max",
                                                                DoubleValue (6));
  auto randDelay = Seconds (var->GetValue (0.0, 6.0));
  next_scoring = Simulator::Now () + Seconds (3) + randDelay;
}

/**
 * Computes the score for a path given it's latency, loss & additional scoring.
 * Additional scoring can be negative e.g. if not all probes returned (i.e. incomplete loss estimation),
 * bonus for being the active path etc.
 * Basic implementation gives highest score for path with least latency.
 * Subclasses should implement score functions that work best for their applications.
*/
double
App::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id,
                   bool wasActive)
{
  std::cout << "(" << latency << ", " << loss << ", " << additional_scoring << ")";
  return additional_scoring - latency;
}

/**
 * Gets the path for which the best score was computed. If probes are pending, it will return the best path of the previous set of probes.
 * Currently does not allow multipath.
*/
std::vector<const ns3::PathSegment *>
App::GetPath ()
{
  return all_paths.at (best_path_id);
}

bool
App::rescore (double active_loss)
{
  return active_loss > acceptable_loss; // loss too high, recompute scores.
}

void
App::ReceiveAppResponse (AppResp app_resp)
{
  active_latency = app_resp.avg_latency;
  active_loss = app_resp.loss;
  auto path_info = path_infos->at (best_path_id);
  path_info.activeLoss = app_resp.loss;
  path_info.activeLossMeasured = true;
  path_info.latency = app_resp.avg_latency / 1000;
  path_infos->at (best_path_id) = path_info;
  app_responses.push_back (std::make_pair (Simulator::Now (), app_resp));

  if (rescore (active_loss) && Simulator::Now () > next_scoring)
    {
      ComputeAllScores (false);
    }
}

void
App::PrintResults ()
{
  std::cout << "----- App id " << app_id << ": Timestamp, latency, loss, bytes ------- "
            << std::endl;
  for (auto entry : app_responses)
    {
      std::cout << entry.first.ToInteger (Time::Unit::MS) << ", "
                << entry.second.avg_latency / 1000. << ", " << entry.second.loss << ", "
                << entry.second.bytes_received << std::endl;
    }
}

void
App::PrintPathInfo ()
{
  std::cout << host->GetAddressAsString () << " " << app_id << std::endl;
  for (uint64_t i = 0; i < all_paths.size (); ++i)
    {
      std::cout << "path_id " << i << ", ";
      host->PrintPath (all_paths.at (i));
    }
  std::cout << "End of app path info" << std::endl;
}

void
App::ReceiveScmp (ScmpReqOrResp scmp)
{
  // Implement in subclass
}

} // namespace ns3