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

double
VideoConferenceApp::ComputeScore (PathInfo path_info)
{
  // TODO
  return App::ComputeScore (path_info);
}

void
VideoConferenceApp::PrintResults ()
{
  std::cout << "VCA app" << std::endl;
  App::PrintResults ();
}
} // namespace ns3