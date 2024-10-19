# Utils

This directory contains utilities for evaluation of Polaris / PAN (path-aware networking).

`gen_scenario.py` was used to create a multitude of scenarios for comparing BGP, naive and Polaris, and further to compare Polaris with itself using different parameter combinations.

The runner script `run_scenarios.py` runs generated simulation scenarios in a folder in parallel, because ns-3 simulations are single threaded for the most part.

The `plot.py` script can plot a single simulation result vs. time, or process a large number of simulation outputs and compare different methods or parameters. In that case, processed results are also cached.