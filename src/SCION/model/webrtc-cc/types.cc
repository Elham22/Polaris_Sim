/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 ETH Zuerich
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

#include "src/SCION/model/webrtc-cc/types.h"
#include <vector>

namespace ns3 {
const char *
BandwidthUsageToString (BandwidthUsage usage)
{
  switch (usage)
    {
    case BandwidthUsage::kBwNormal:
      return "kBwNormal";
    case BandwidthUsage::kBwUnderusing:
      return "kBwUnderusing";
    case BandwidthUsage::kBwOverusing:
      return "kBwOverusing";
    }
  return "Unknown";
}

} // namespace ns3
