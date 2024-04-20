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
 * Author: Patrick Wicki <patrick.wicki@inf.ethz.ch>
 */

#ifndef SCION_SIMULATOR_RTC_APP_H
#define SCION_SIMULATOR_RTC_APP_H

#include "app.h"
#include "src/SCION/model/border-router.h"

namespace ns3 {
/**
 * RTC like application with some path selection smarts
*/
class RTCApp : public App
{
public:
  RTCApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia,
          host_addr_t app_dst_host_addr, std::vector<std::vector<const PathSegment *>> all_paths,
          bool enable_logging)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths, enable_logging)
  {
  }

  void PrintResults () override;

  void HandleSCMP (ScmpReqOrResp scmp);
  void ReceiveAppResponse (AppResp app_resp) override;

  /**
   * Override to throw error, we don't use probes but keep the existing code
  */
  void ReceiveProbeResponse (ProbeResp probe_resp) override;
  void StartAppTraffic () override;
  void StopAppTraffic () override;
};

} // namespace ns3

#endif //SCION_SIMULATOR_RTC_APP_H
