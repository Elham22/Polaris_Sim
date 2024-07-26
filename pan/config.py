# config.py

# Defaults variants for which we generate scenarios
DEFAULT_FLOW_NUMBERS = [4, 8, 12, 16, 20, 24]
DEFAULT_SCENARIO_TYPES = ['Ciao', 'Naive', 'BGP_FD', 'BGP_ECMP']

# Time at which target flows start sending data
STARTUP_PHASE_SECONDS = 1800