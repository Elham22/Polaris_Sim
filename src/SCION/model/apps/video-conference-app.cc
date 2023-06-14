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
void
VideoConferenceApp::GenerateAppTraffic ()
{
  // TODO
  App::GenerateAppTraffic ();
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
  // TODO
  std::cout << "VCA print results not implemented" << std::endl;
}
} // namespace ns3