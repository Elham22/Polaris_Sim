SCION beaconing simulator based on NS3. The source files are located under `src/SCION`.

# Getting Started:

There are currently two scripts you can run from the command line. `baseline_sim.cpp` and  `criteria_matching_sim.cpp`. Both those scripts
will automatically instantiate the correct types of SCION-nodes depending on the passed topology file (see below for more information on the 
expected formats of the topology files). Both scripts currently uniformly instantiate their respective strategies on each node. Mixed beacon_server
deployments are not yet supported. 

You first need to configure the simulator before running it:

`./waf distclean`

`CCFLAGS_EXTRA="-O3 -fopenmp -std=c++17" CXXFLAGS_EXTRA="-O3 -fopenmp -std=c++17"  ./waf configure --build-profile=debug|default|release|optimized --enable-asserts --enable-logs`

`./waf --run "main /path/to/config.yaml"`



Note that simulator has been tested and developed with GCC version 7 using cpp 17. Since NS3 is configured to treat all compilation warnings as
errors, trying to compile with different GCC versions will likely fail.

## Expected Topology file formats:

You can find current topologies and also put your own topology files in `topology/`. Topology should be be a fnss-like xml file.
The simulator expects the following properties:

### Node:
- type [string]: Can be either `core` or `non-core`. Marks if this node is a Core or Leaf AS.
- latency_coef [float]: Indicates the nodes path-preference in terms of latency. 
- bandwidth_coef [float]: Indicates the nodes path-preference in terms of bandwidth. 
- AS_level_diversity_coef [float]: Indicates the nodes path-preference in terms of AS-lvl diversity. 
- link_level_diversity_coef [float]: Indicates the nodes path-preference in terms of link-lvl diversity. 

TODO: More exact description on the preferences (what do high, low values indicate?)

### Link:
- rel [string]: Indicates the link relation. The possible transit types are `core` and `customer`. A link can also be a `peer` link.
- latitude [float]: The latitude of the border routers connected to this link.
- longitude [float]: The longitude of the border routers connected to this link.
- capacity [int]: The capacity on the link TODO: unit?

Note that we assume the border routers connecting to each other via the links to be in the same room. This is why there is only one
latitude/longitude pair per link. 

Note that for both the nodes and the links, all the properties listed here are required. If any are missing, the simulation will abort.
