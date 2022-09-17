This repository is a fork of the ns-3-dev (at version 3.30.1) that implements the SCION next generation Internet
architecture. Using this simulator, one can simulate the control plane and the data plane of SCION internet architecture. This means that the simulator first constructs inter-domain paths between ASes, and then uses such paths to forward traffic between ASes.

This simulator also provides a useful framework to study new services and algorithms in the SCION Internet architecture. 
For example, one can develop new routing algorithms by extending the `BeaconServer` class (e.g., see `/src/SCION/model/beaconing/latency-optimized-beaconing.h`), 
or simulate any type of application by extending `ScionHost` class (e.g., `/src/SCION/model/time-server.h`).

# Structure:
The SCION simulator models the Internet as a topology of ASes (Autonomous Systems). 
Therefore, it declares the `ScionAS` class in `/src/SCION/model/scion-as.h` as the highest-level entity in the simulator.
This class is derived from the ns-3's `Node` class. As the SCION architecture categorizes ASes in two category of core and 
non-core ASes, the SCION simulator derives the `ScionCoreAS` class from `ScionAS` class.

Each `ScionAS` instance has an instance `BeaconServer`, and any number of `ScionCapableNode`s. 

The `BeaconServer` performs path discovery (routing)
among ASes. The `BeaconServer` of each AS directly talks to the `BeaconServer`s of the neighboring ASes.

`ScionCapableNode` represents any device that is SCION-aware, i.e., can communicate over SCION. There are three classes 
derived from `ScionCapableNode`: 
1) `PathServer`, which registers paths discovered by the `BeaconServer` and serves 
`ScionHost`s by resolving their query for path-segments,
2) `BorderRouter`, which forwards SCION packets according to the paths specified by packets,
3) `ScionHost`, any general purpose host capable of talking using SCION

# Requirements:
The SCION simulator uses the following libraries. Make sure to have them installed before running the simulator,
1) `yaml-cpp` to read configuration files
2) `libgomp` as the simulator uses openmp to parallelize tasks.

It also uses the follwoing libraries, however, they are included in the repository, 
so there is no need to take any actions regarding them:

1) `json` available at `/src/SCION/model/json.hpp` required to 
   1) save the state of simulator that can be used to run 
   successive simulations from the saved state
   2) specify user defined events
2) `rapid_xml` available at `/src/fnss/model/` required to read CAIDA topologies

# Input:
The minimal input to the simulator is the configuration file and the topology file.

1) The configuration file is a `.yaml` file that specifies the characteristics of the simulations. Examples available at 
`/configs/`. New configuration files can be specified in `/configs/`.
2) The topology file is a `.xml` file that specifies ASes and links between them. 
This file is the result of running 
[fnss](https://fnss.readthedocs.io/en/latest/apidoc/generated/fnss.topologies.parsers.parse_caida_as_relationships.html)' 
`parse_caida_as_relationships` over the [CAIDA](https://www.caida.org)'s AS-geo-rel data set.

# Getting Started:

You first need to configure the simulator before running it:

`./waf distclean`

`CCFLAGS_EXTRA="-O3 -fopenmp -std=c++17" CXXFLAGS_EXTRA="-O3 -fopenmp -std=c++17"  ./waf configure --build-profile=debug|default|release|optimized --enable-asserts --enable-logs`

`./waf --run "scion /path/to/config.yaml"`



Note that simulator has been tested and developed with GCC version 7 using cpp 17. Since NS3 is configured to treat all compilation warnings as
errors, trying to compile with different GCC versions will likely fail.