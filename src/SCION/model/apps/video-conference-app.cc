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
#include "video-conference-app.h"

namespace ns3 {
double LaplaceRV::GetValue ()
{
  // Laplace can be computed as the difference of two i.i.d. samples of the exp distribution.
  double x = exp->GetValue ();
  double y = exp->GetValue ();
  return x - y;
}

/**
 * Video conference traffic modeled after rfc8593 ([1]), example implemetation at [2].
 * References:
 * - - - - - -
 * [1]	https://www.rfc-editor.org/rfc/rfc8593.html#section-5
 * [2]  https://www.rfc-editor.org/rfc/rfc8593.html#section-8
*/
void
VideoConferenceApp::GenerateAppTraffic ()
{
  if (stopped)
    {
      return;
    }

  double frameBytes = selected_bitrate / (fps * 8.);
  frameBytes += frame_size_noise.GetValue () * frameBytes;
  if (frameBytes <= 0)
    {
      frameBytes = 64; // small minimum, chosen arbitrarily
    }
  ScheduleSendData ((uint32_t) frameBytes, GetPath ());

  double interval = 1. / fps;
  interval += frame_interval_noise.GetValue () * interval;
  if (interval <= 0)
    {
      interval = 0.001; // minimal interval of 1ms
    }
  Simulator::Schedule (Seconds (interval), &VideoConferenceApp::GenerateAppTraffic, this);
  //std::cout << "VCA sent packet " << packet_id-1 << " at " << Simulator::Now ().ToInteger (Time::Unit::MS) - 6274000 << " with " << frameBytes << " bytes, next frame in " << interval * 1000 << " ms" << std::endl;
}

uint32_t
VideoConferenceApp::ComputeExpectedBandwidth (uint32_t path_id)
{
  uint32_t quality = path_id / num_paths;
  return bitrates.at (quality) / 8;
}

bool
VideoConferenceApp::rescore (double active_loss)
{
  return active_loss > acceptable_loss || 
    (active_loss == 0 && best_path_id / num_paths < 2); // also rescore when loss is zero and not highest quality to select an increased quality.
}

bool
VideoConferenceApp::isActivePath (int32_t path_id)
{
  return path_id % num_paths == best_path_id % num_paths && best_path_id >= 0;
}

double
VideoConferenceApp::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id, bool was_active)
{
  //std::cout << " VCA::ComputeScore (" << latency << ", " << loss << ", " << additional_scoring << "), ";
  //if (path_id == 51) return 10000; // force a specific path for debugging purposes
  uint32_t quality = path_id / num_paths;
  double penalty_latency = latency < 50 ? 0 : latency * 4 - 200;
  double penalty_loss = loss < acceptable_loss ?  loss * 5e3 : loss * 1e4;
  if ((loss > acceptable_loss && quality > 0) ||
      (best_path_id != -1 &&
        ((quality > 1 + best_path_id / num_paths) ||
        (quality == 1 + best_path_id / num_paths && path_infos->at(best_path_id).activeLoss > 0) ||
        (quality > 0 && quality >= best_path_id / num_paths && path_infos->at(best_path_id).activeLoss > acceptable_loss)
        )
      ))
    {
      penalty_loss += 10000; // backoff if congestion detected and high sending rate
    }
  return quality * 300 + additional_scoring - penalty_latency - penalty_loss;
}

void
VideoConferenceApp::ComputeAllScores (bool triggered_by_probes)
{
  App::ComputeAllScores (triggered_by_probes);
  uint32_t quality = best_path_id / num_paths;
  selected_bitrate = bitrates.at (quality);
  selected_qualities.push_back (std::make_tuple (host->GetLocalTime ().ToInteger (Time::Unit::MS), quality, best_path_id));
}

double
VideoConferenceApp::MetricScore (double latency, double loss, uint32_t quality)
{
  double penalty_latency = latency < 50 ? 0 : latency * 4 - 200;
  double penalty_loss = loss < acceptable_loss ?  loss * 5e3 : loss * 1e4;
  return quality * 300 - penalty_latency - penalty_loss;
}

void
VideoConferenceApp::PrintResults ()
{
  std::cout << "----- " << InfoString () << " id " << app_id << " dst " << dst_ia << ":" << dst_host_addr << "-------" << std::endl
            << "----- Timestamp, latency, loss, bytes, path, quality, score ------- " << std::endl;
  uint i = 0;
  for (auto entry : app_responses)
  {
    int64_t timestamp = entry.first.ToInteger (Time::Unit::MS);
    if (i+1 < selected_qualities.size () && timestamp > std::get<0> (selected_qualities.at (i+1)))
      {
        ++i;
      }
    uint32_t quality = std::get<1> (selected_qualities.at (i));
    int32_t selected_path = std::get<2> (selected_qualities.at (i));
    double score = VideoConferenceApp::MetricScore (entry.second.avg_latency / 1000., entry.second.loss, quality);
    std::cout << timestamp << "(" << entry.first.ToDouble (Time::Unit::MIN) << " min), " << entry.second.avg_latency / 1000. << ", "
              << entry.second.loss << ", " << entry.second.bytes_received << ", " 
              << selected_path << ", " << quality << ", " 
              << score << std::endl;
  }
  std::cout << "----- End of app " << app_id << " results ------" << std::endl;
}

void 
VideoConferenceApp::ScheduleSendData (uint32_t size, std::vector<const ns3::PathSegment *> path)
{
  Time delay = TimeStep (1);
  Time increment = MilliSeconds (1);
  // split up into multiple packets if larger than pktSize
  while (size > m_pktSize)
    {
      Simulator::Schedule (delay, &VideoConferenceApp::SendData, this, m_pktSize, path);
      size -= m_pktSize;
      delay += increment;
    }
  if (size > 0)
    {
      Simulator::Schedule (delay, &VideoConferenceApp::SendData, this, size, path);
    }
}

std::string
VideoConferenceApp::InfoString ()
{
  return "VCA active";
}

double
VCAPassive::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id, bool was_active)
{
  //std::cout << " VCAPassive::ComputeScore (" << latency << ", " << loss << ", " << additional_scoring << ", " << was_active << "), ";
  double penalty_loss = 0.;
  // only do loss sensitive computation based on active loss but not on probing
  if (was_active)
    {
      penalty_loss = loss < acceptable_loss ?  loss * 5e3 : loss * 1e4;
    }
  uint32_t quality = path_id / num_paths;

  // start new paths at low quality only
  if ((quality > 0 && !isActivePath(path_id)) || 
      (quality > 1 + best_path_id / num_paths) ||
      (quality == 1 + best_path_id / num_paths && path_infos->at(best_path_id).activeLoss > 0))
    {
      additional_scoring -= 10000;
      //std::cout << "Path quality disqualified, "; 
    }
  double penalty_latency = latency < 50 ? 0 : latency * 4 - 200;
  return quality * 300 - penalty_loss - penalty_latency + additional_scoring;
}

std::string
VCAPassive::InfoString ()
{
  return "VCA passive";
}

double
VCANaive::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id, bool was_active)
{
  //std::cout << " VCANaive::ComputeScore (" << latency << ", " << loss << ", " << additional_scoring << "), ";
  uint32_t quality = path_id / num_paths;
  uint32_t path_bonus = path_id % num_paths == chosen_path ? 5000 : 0;
  // start new paths at low quality only
  bool current_path = best_path_id >= 0 && path_id % num_paths == best_path_id % num_paths;
  if ((quality > 0 && !current_path) ||
      (quality > 1 + best_path_id / num_paths) ||
      (quality == 1 + best_path_id / num_paths && path_infos->at(best_path_id).activeLoss > 0) ||
      (quality > 0 && was_active && loss > acceptable_loss))
    {
      path_bonus = 0;
    }
  return quality * 300 + path_bonus;
}

std::string
VCANaive::InfoString ()
{
  return "VCA naive";
}

double
VCAGiven::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id, bool was_active)
{
  //std::cout << " VCAGiven::ComputeScore (" << latency << ", " << loss << ", " << additional_scoring << "), ";
  uint32_t quality = path_id / num_paths;
  uint32_t path_bonus = path_id % num_paths == chosen_path ? 5000 : 0;
  // start new paths at low quality only
  bool current_path = best_path_id >= 0 && path_id % num_paths == best_path_id % num_paths;
  if ((quality > 0 && !current_path) || 
      (quality > 1 + best_path_id / num_paths) ||
      (quality == 1 + best_path_id / num_paths && path_infos->at(best_path_id).activeLoss > 0) ||
      (quality > 0 && was_active && loss > acceptable_loss))
    {
      path_bonus = 0;
    }
  return quality * 300 + path_bonus;
}

std::string
VCAGiven::InfoString ()
{
  return "VCA given";
}

void
VCAGiven::ParseGivenPath (std::string given_path)
{
  std::vector<std::vector<uint16_t>> given_hops;
  size_t start = 1;
  auto open = given_path.find ("(", start);
  auto close = given_path.find (")", start);
  while (open != std::string::npos && close != std::string::npos)
    {
      std::string seg = given_path.substr (open + 1, close - open - 1);

      size_t num_start = 0;
      auto num_end = seg.find (",");
      std::vector<uint16_t> hop;
      hop.push_back (std::stoi (seg.substr (num_start, num_end)));
      num_start = num_end + 1;
      num_end = seg.find (",", num_start);
      hop.push_back (std::stoi (seg.substr (num_start, num_end)));
      num_start = num_end + 1;
      num_end = seg.find (",", num_start);
      hop.push_back (std::stoi (seg.substr (num_start, num_end)));
      hop.push_back (std::stoi (seg.substr (num_end + 1)));

      given_hops.push_back (hop);

      start = close + 1;
      open = given_path.find ("(", start);
      close = given_path.find (")", start);
    }

  for (uint32_t i = 0; i < num_paths; i++)
  {
    // only works for paths consisting of 1 segment, but we only ever have 1 segment in core only topologies.
    auto hops = all_paths.at (i).at (0)->hops;
    if (given_hops.size () != hops.size ())
      {
        continue;
      }
    bool found = true;
    for (uint16_t j = 0; j < hops.size (); j++)
      {
        auto given_hop = given_hops.at (j);
        auto hop = hops.at (j);
        if (
          GET_HOP_ISD (hop) != given_hop.at (0) ||
          GET_HOP_AS (hop) != given_hop.at (1) ||
          GET_HOP_ING_IF (hop) != given_hop.at (2) ||
          GET_HOP_EG_IF (hop) != given_hop.at (3)
        )
          {
            found = false;
            break;
          }

      }
    
    if (found)
      {
        chosen_path = i;
        break;
      }
  }

  if (chosen_path == (uint32_t)-1)
    {
      std::cout << "VCA given error: given path not found" << std::endl;
    }
}
} // namespace ns3