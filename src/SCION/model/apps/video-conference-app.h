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

#ifndef SCION_SIMULATOR_VIDEO_CONFERENCE_APP_H
#define SCION_SIMULATOR_VIDEO_CONFERENCE_APP_H

#include <algorithm>

#include "app.h"
#include "ns3/random-variable-stream.h"
#include "ns3/double.h"

namespace ns3 {

/**
 * Laplace random variable with mean 0.
 * There does not seem to exist a Laplace random variable in ns3 by default.
*/
class LaplaceRV
{
public:
  LaplaceRV (double scale)
  : scale (scale)
  {
    exp = CreateObjectWithAttributes<ExponentialRandomVariable> ("Mean", DoubleValue (scale));
  }

  double GetValue ();

protected:
  double scale;
  Ptr<RandomVariableStream> exp;
};

class VideoConferenceApp : public App
{
public:
  VideoConferenceApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia, host_addr_t app_dst_host_addr,
      std::vector<std::vector<const PathSegment *>> all_paths)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths),
      num_paths (all_paths.size ())
  {
    // triples the path vector so we get one path entry per path/quality pair.
    std::vector<std::vector<const PathSegment *>> copies;
    copies.resize (3 * num_paths);
    for (uint i = 0; i < copies.size (); ++i)
      {
        copies[i] = all_paths.at (i % num_paths);
      }
    
    App::all_paths = copies;
    for (uint32_t i = 0; i < copies.size (); ++i)
      {
        std::cout << "Path_id " << i << ", ";
        ScionHost::PrintPath (copies.at (i));
      }
  }
  void PrintResults () override;

protected:
  uint32_t num_paths;
  std::vector<std::tuple<int64_t, uint32_t, int32_t>> selected_qualities;

  // parameters
  std::vector<double> bitrates {0.7e6, 1.5e6, 5e6};
  const uint16_t fps = 30;
  double selected_bitrate = bitrates.at (1);
  LaplaceRV frame_size_noise = LaplaceRV (0.15); // noise of the packet sizes, modeled after rfc8593
  LaplaceRV frame_interval_noise = LaplaceRV (0.15); // noise of the interval between packets, modeled after rfc8593
  
  void GenerateAppTraffic () override;
  uint32_t ComputeExpectedBandwidth (uint32_t path_id) override;
  double ComputeScore (double latency, double loss, double additional_scoring, uint32_t path_id) override;
  void ComputeAllScores () override;
};

} // namespace ns3

#endif //SCION_SIMULATOR_VIDEO_CONFERENCE_APP_H