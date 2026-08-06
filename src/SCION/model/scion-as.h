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

#ifndef SCION_SIMULATOR_SCION_AS_H
#define SCION_SIMULATOR_SCION_AS_H
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ns3/map-scheduler.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "src/SCION/model/beaconing/beacon.h"
#include "border-router.h"
#include "path-server.h"
#include "scion-host.h"
#include "scion-packet.h"
#include "user-defined-events.h"
#include "utils.h"

namespace ns3 {

enum NeighbourRelation { CORE = 0, PEER = 1, CUSTOMER = 2, PROVIDER = 3 };

class BeaconServer;

class ScionAs : public Node
{
public:
  ScionAs (uint32_t system_id, bool parallel_scheduler, uint16_t as_number,
            rapidxml::xml_node<> *xml_node, const YAML::Node &config, bool malicious_border_routers,
            Time local_time)
      : Node (system_id)
  {
    PropertyContainer p = ParseProperties (xml_node);

    if (p.HasProperty ("isd"))
      {
        isd_number = std::stoi (p.GetProperty ("isd"));
      }
    else
      {
        isd_number = 0;
      }

    this->as_number = as_number;
    ia_addr = (((uint32_t) isd_number) << 16) | ((uint32_t) as_number);

    this->local_time = local_time;

    this->malicious_border_routers = malicious_border_routers;

    if (malicious_border_routers)
      {
        border_routers_malicious_action =
            config["border_router"]["malicious_action"].as<std::string> ();
      }
    else
      {
        border_routers_malicious_action = "no";
      }

    InstantiateBeaconServer (parallel_scheduler, xml_node, config);
  }

  virtual ~ScionAs ()
  {
  }

  uint16_t isd_number;
  uint16_t as_number;
  ia_t ia_addr;

  Time local_time;
  int32_t as_max_bwd;

  std::vector<Time> latencies_between_hosts_and_path_server;
  std::vector<Time> latencies_between_interfaces_and_beacon_server;
  Time latency_between_path_server_and_beacon_server;

  std::vector<std::pair<uint16_t, NeighbourRelation>> neighbors;
  std::unordered_map<uint16_t, std::vector<uint16_t>> interfaces_per_neighbor_as;
  std::unordered_map<uint16_t, uint16_t> interface_to_neighbor_map;
  std::vector<std::pair<ld, ld>> interfaces_coordinates;
  std::multimap<std::pair<ld, ld>, uint16_t> coordinates_to_interfaces;
  std::vector<std::vector<ld>> latencies_between_interfaces;
  std::vector<int32_t> inter_as_bwds;

  void DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config, bool only_propagation_delay);

  void DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                          const YAML::Node &config);

  std::pair<uint16_t, ScionAs *> GetRemoteAsInfo (uint16_t egress_interface_no);

  void ReceiveBeacon (Beacon &the_beacon, uint16_t sender_as, uint16_t remote_if,
                      uint16_t local_if);

  void SetBeaconServer (BeaconServer *beacon_server);

  void SetPathServer (PathServer *path_server);

  BeaconServer *GetBeaconServer ();

  PathServer *GetPathServer ();

  ScionCapableNode *GetHost (host_addr_t host_addr);

  uint32_t GetNHosts ();

  void AdvanceTime (ns3::Time advance);

  void AddHost (ScionHost *host);

  BorderRouter *AddBr (double latitude, double longitude, Time processing_delay,
                       Time processing_throughput_delay);

  void AddToRemoteAsInfo (uint16_t remote_if, ScionAs *remote_as);

  friend class UserDefinedEvents;

protected:
  bool malicious_border_routers;
  std::string border_routers_malicious_action;

  BeaconServer *beacon_server;
  PathServer *path_server = NULL;
  std::vector<ScionHost *> hosts;
  std::vector<BorderRouter *> border_routers;

  std::vector<std::pair<uint16_t, ScionAs *>> remote_as_info;

  void ConnectInternalNodes (bool only_propagation_delay);
  void InitializeLatencies (bool only_propagation_delay);

  void InstantiateBeaconServer (bool parallel_scheduler, rapidxml::xml_node<> *xml_node,
                                  const YAML::Node &config);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_AS_H
