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

#ifndef SCION_SIMULATOR_CONTROLLER_STATE_H
#define SCION_SIMULATOR_CONTROLLER_STATE_H

enum class DetectorSignal {
  UNDERUSE,
  NORMAL,
  OVERUSE,
};

enum class ControllerState {
  DECREASE,
  HOLD,
  INCREASE,
};

/**
 * Struct to hold the state of the controller at a certain point in time for visualization
*/
struct ControllerStateSnapshot
{
  DetectorSignal signal;
  ControllerState state;
  double A_r;
  double kalman_gain;
  double treshold_hi;
  double treshold_lo;
  double m;
  double d_m;
  double z;
  double variance;
  double error;
};

#endif // SCION_SIMULATOR_CONTROLLER_STATE_H