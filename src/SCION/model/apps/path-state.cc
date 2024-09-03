#include "path-state.h"

namespace ns3 {

bool
PathState::HasFreshProbeResultsSince (Time t)
{
  // For the result to be fresh, it must be from a probe initiated after t +
  // latency to account for any potential changes to the bottleneck share
  // incurred by a path switch
  return last_probe_echo_time > t + MicroSeconds (latency);
}

} // namespace ns3