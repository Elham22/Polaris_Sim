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
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch
 */

#include <chrono>
#include <omp.h>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"

#include "src/SCION/model/beaconing/beacon-server.h"
#include "schedule-periodic-events.h"
#include "scion-as.h"
#include "scion-host.h"
#include "time-server.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("GlobalScheduling");

void
SchedulePeriodicEvents (YAML::Node &config)
{
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Ptr<ScionAs> node = DynamicCast<ScionAs> (nodes.Get (i));

      if (config["beacon_service"])
        {
          node->GetBeaconServer ()->ScheduleBeaconing (
              Time (config["beacon_service"]["last_beaconing"].as<std::string> ()));
        }

      if (config["time_service"])
        {
          dynamic_cast<TimeServer *> (node->GetHost (2))->ScheduleListOfAllASesRequest ();
          dynamic_cast<TimeServer *> (node->GetHost (2))->ScheduleSnapShots ();
          dynamic_cast<TimeServer *> (node->GetHost (2))->ScheduleTimeSync ();
        }
    }

  if (config["beacon_service"])
    {
      for (Time t = Seconds (0.0);
           t <= Time (config["beacon_service"]["last_beaconing"].as<std::string> ());
           t += Time (config["beacon_service"]["period"].as<std::string> ()))
        {
          Simulator::Schedule (t + TimeStep (2), &PeriodicBeaconingCheckPoint);
        }
    }
}

void
PeriodicBeaconingCheckPoint ()
{
  std::cout << "################################## "
            << DynamicCast<ScionAs> (nodes.Get (0))->GetBeaconServer ()->GetCurrentTime ()
            << " #########################################" << std::endl;
  uint32_t node_number = nodes.GetN ();

  // print number of connected pairs after each beaconing round
  uint32_t all_connected_pairs = 0;
  for (uint32_t i = 0; i < node_number; ++i)
    {
      all_connected_pairs += DynamicCast<ScionAs> (nodes.Get (i))
                                 ->GetBeaconServer ()
                                 ->GetValidBeaconsCountPerDstAs ()
                                 .size ();
    }
  std::cout << all_connected_pairs << std::endl;
}
} // namespace ns3
