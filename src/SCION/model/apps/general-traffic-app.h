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

#ifndef SCION_SIMULATOR_GENERAL_TRAFFIC_APP_H
#define SCION_SIMULATOR_GENERAL_TRAFFIC_APP_H

#include "ns3/random-variable-stream.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "app.h"

namespace ns3 {
/**
 * Realistic internet traffic generate according to a Poisson Pareto Burst Process (PPBP).
 * Propsed by and implemented by [1], implementation adapted for this use case, original
 * code at [2].
 * 
 * References:
 * - - - - - -
 * [1]	A new tool for generating realistic Internet traffic in NS-3,
 *		D. Ammar, T. Begin and I. Guerin Lassous.
 *		4th International ICST Conference on Simulation Tools and Techniques (SIMUTools),
 *		Barcelona, Spain, March 21-25, 2011, Poster.
 * [2]  http://perso.ens-lyon.fr/thomas.begin/NS3-PPBP.zip
*/
class GeneralTrafficApp : public App
{
public:
  GeneralTrafficApp (ScionHost *host, uint32_t app_id, ia_t ia_addr, ia_t app_dst_ia, host_addr_t app_dst_host_addr,
      std::vector<std::vector<const PathSegment *>> all_paths)
      : App (host, app_id, ia_addr, app_dst_ia, app_dst_host_addr, all_paths)
  {
  }
  void PrintResults () override;

protected:
  void GenerateAppTraffic () override;
  double ComputeScore (PathInfo path_info) override;

  // PPBP
  uint32_t		    m_pktSize = 1470;       // Size of packets	
  Ptr<RandomVariableStream> m_burstArrivals = CreateObjectWithAttributes <ConstantRandomVariable> ("Constant", DoubleValue (20)); // Mean rate of burst arrivals
	Ptr<RandomVariableStream> m_burstLength = CreateObjectWithAttributes <ConstantRandomVariable> ("Constant", DoubleValue (0.2)); // Mean burst time length
	DataRate m_cbrRate = DataRate ("1Mb/s");// Burst intensity (constant bit-rate)

	double			    m_h = 0.7;							// Hurst parameter	(Pareto distribution)
	double			    m_shape;						    // Shape			(Pareto distribution)
	Time			      m_timeSlot;						  // The time slot
	int				      m_activebursts;					// Number of active bursts at time t
	bool			      m_offPeriod = true;;
  
	/**
   * \ Functions that allows to keep track of the current number of active bursts at time t, nt,
	 * taking into account that their arrival process follows a Poisson process and that their
	 * length is determined by a Pareto distribution.
	 */
	void PPBP();
	void PoissonArrival();
	void ParetoDeparture();
	
	/**
	 * \ Function thet generates the packets departure at a constant bit-rate nt x r.
	 */
	void ScheduleNextTx();
	void SendPacket();
};

} // namespace ns3

#endif //SCION_SIMULATOR_GENERAL_TRAFFIC_APP_H