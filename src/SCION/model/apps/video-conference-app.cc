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
  double frameBytes = selected_bitrate / (fps * 8.);
  frameBytes += frame_size_noise.GetValue () * frameBytes;
  if (frameBytes <= 0)
    {
      frameBytes = 64; // small minimum, chosen arbitrarily
    }
  SendData ((uint32_t) frameBytes / scale, GetPath ());

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

double
VideoConferenceApp::ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id)
{
  uint32_t quality = path_id / num_paths;
  double penalty_latency = latency < 50 ? 0 : latency;
  double penalty_loss = loss < 0.02 ? loss * 1e4 : loss * 5e4;
  return quality * 1000 + additional_scoring - penalty_latency - penalty_loss;
}

void
VideoConferenceApp::ComputeAllScores ()
{
  App::ComputeAllScores ();
  uint32_t quality = best_path_id / num_paths;
  selected_bitrate = bitrates.at (quality);
  selected_qualities.push_back (std::make_pair (host->GetLocalTime ().ToInteger (Time::Unit::MS), quality));
}

void
VideoConferenceApp::PrintResults ()
{
  std::cout << "----- VCA id " << app_id << ": Timestamp, latency, loss, bytes, quality, score ------- " << std::endl;
  uint i = 0;
  for (auto entry : app_responses)
  {
    int64_t timestamp = entry.first.ToInteger (Time::Unit::MS);
    if (i+1 < selected_qualities.size () && timestamp > selected_qualities.at (i+1).first)
      {
        ++i;
      }
    uint32_t quality = selected_qualities.at (i).second;
    double score = ComputeScore (entry.second.avg_latency / 1000., entry.second.loss, 0, quality * num_paths);
    std::cout << timestamp << ", " << entry.second.avg_latency / 1000. << ", "
              << entry.second.loss << ", " << entry.second.bytes_received << ", " << quality << ", " 
              << score << std::endl;
  }
}
} // namespace ns3