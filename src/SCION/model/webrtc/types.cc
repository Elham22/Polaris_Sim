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


#include "types.h"

namespace ns3 {

  DataSize BitRate::operator*(const TimeDelta& delta) const {
    return DataSize(bps_ * delta.seconds());
  }

  BitRate DataSize::operator/(const TimeDelta& delta) const {
    return BitRate(bits_ / delta.seconds());
  }

  TimeDelta DataSize::operator/(const BitRate& rate) const {
    return TimeDelta(bits_ / static_cast<double>(rate.bps()));
  }

} // namespace ns3
