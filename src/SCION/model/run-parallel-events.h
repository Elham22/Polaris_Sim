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

#ifndef SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H
#define SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H

#include "externs.h"
#include "scion-as.h"
#include "scion-packet.h"
#include <omp.h>
#include <yaml-cpp/yaml.h>

namespace ns3 {

template <typename MEM, typename OBJ>
void
RunParallelEvents (host_addr_t host_addr, MEM mem_ptr)
{
  omp_set_num_threads (num_core);
#pragma omp parallel for schedule(dynamic)
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (nodes.Get (i)));
      ((dynamic_cast<OBJ> (node->GetHost (host_addr)))->*mem_ptr) ();
    }
}

template <typename MEM>
void
RunParallelEvents (MEM mem_ptr)
{
  omp_set_num_threads (num_core);
#pragma omp parallel for schedule(dynamic)
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (nodes.Get (i)));
      ((node->GetBeaconServer ())->*mem_ptr) ();
    }
}
} // namespace ns3
#endif //SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H
