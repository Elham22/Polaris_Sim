#!/usr/bin/env python3
import argparse
import matplotlib.pyplot as plt
from matplotlib import gridspec
import json
import os

import numpy as np

DEFAULT_START_TIME = 1800  # When actual flows start in the simulation


def parse_args():
    parser = argparse.ArgumentParser(prog="SCION simulator host QOE plotter",
                                     description="Plotting of host QOE simulation results from SCION simulator")
    parser.add_argument(
        "filepath", help="Input file (result file of simulation) to parse")
    parser.add_argument("-l", "--loss", action='store_true', help="Plot the loss")
    parser.add_argument("-d", "--latency", action='store_true', help="Plot the latency")
    parser.add_argument("-t", "--total", action='store_true', help="Plot the total send rate")
    parser.add_argument("-g", "--gradient",
                        action="store_true", help="Plot the GCC gradient")
    parser.add_argument("-p", "--paths",
                        action="store_true", help="Plot the active paths")
    parser.add_argument("-s", "--state", action="store_true",
                        help="Plot the GCC state")
    parser.add_argument("-a", "--active-flows", action="store_true",
                        help="Plot the number of active flows")
    parser.add_argument("--latex", action="store_true",
                        help="Output the plot in LaTeX format")
    parser.add_argument("--avg", type=int, default=1,
                        help="Average every n values together")
    parser.add_argument("--min-share", action="store_true",
                        help="Plot the minimum share")
    parser.add_argument("--total-received", action="store_true",
                        help="Plot the total bytes received (sent & acked)")
    parser.add_argument("--start-time", type=int, default=DEFAULT_START_TIME,
                        help="Start time of the simulation (default: 1800)")
    return parser.parse_args()


def getStateAttributeList(app: dict, attribute: str):
    states = app['states']
    return [state[attribute] for state in states]


def transformLatexLabel(label):
    label = label.replace('TcpCubic', 'TCP')
    label = label.replace('TcpNewReno', 'TCP')

    # Escape characters for LaTeX
    label = label.replace('#', '\\#')
    return label


def printLatexBasicplot(x, y, label, sharp=False):
    if not args.latex:
        return
    label = transformLatexLabel(label)
    coordinates = ' '.join([f'({round(xi, 2)},{round(yi, 2)})' for xi, yi in zip(x, y) if xi is not None and yi is not None])
    extra_args = ", sharp plot" if sharp else ""
    print(f"\\addplot+[{label}{extra_args}] coordinates {{{coordinates}}};")


def printLatexBoxplot(data, label):
    if not args.latex:
        return
    median = np.median(data)
    # TODO: This is not the standard boxplot format, just the only one that looks good so far...
    lower_quartile = np.percentile(data, 5)
    upper_quartile = np.percentile(data, 95)
    lower_whisker = np.percentile(data, 1)
    upper_whisker = np.percentile(data, 99)

    print(f"\\addplot+[\n"
          f"    fill,\n"
          f"    fill opacity=0.7,\n"
          f"    draw=linecolor_before,\n"
          f"    thick,\n"
          f"    boxplot prepared={{\n"
          f"        median={median},\n"
          f"        upper quartile={upper_quartile},\n"
          f"        lower quartile={lower_quartile},\n"
          f"        upper whisker={upper_whisker},\n"
          f"        lower whisker={lower_whisker},\n"
          f"    }},\n"
          f"] coordinates {{}};")


def printLatexErrorbar(x, y, yerr, label):
    if not args.latex:
        return

    label = transformLatexLabel(label)
    print("\\addplot+[error bars/.cd, y dir=both, y explicit] coordinates {")
    for xi, yi, yerri in zip(x, y, yerr):
        print(f"({xi},{yi}) +- (0,{yerri})")
    print("};")


def plotSingleScenario(scenario: dict):
    # colors = sns.color_palette("tab10")
    # linestyles = cycle(['-', '--', ':', '-.'])  # Cycle through these styles
    # markers = cycle(['o', 's', '^', 'd'])  # Cycle through these markers

    # # Create a combined cycler with equal length cycles
    # default_cycler = cycler(color=colors) + cycler(linestyle=linestyles) + cycler(marker=markers)
    # plt.rc('axes', prop_cycle=default_cycler)

    # max number of apps before we stop showing the legend
    max_apps_legend = 10

    plot_loss = args.loss
    plot_latency = args.latency
    plot_total_send_rate = args.total
    plot_active_paths = args.paths
    plot_gcc_gradient = args.gradient
    plot_gcc_state = args.state
    plot_gcc_params = False
    plot_active_flows = args.active_flows

    # For which app to plot extra details if there are multiple applications
    detail_app = 0

    no_plots = 1
    size_y = 1
    ratios = [1]

    # Lazy hack to make plot sizing dynamic
    for var in list(locals()):
        if var.startswith('plot_') and locals()[var]:
            no_plots += 1
            size_y += 1
            ratios.append(1)

    fig = plt.figure(figsize=(16, 9))
    plot_number = 0
    gs = gridspec.GridSpec(no_plots, 1, height_ratios=ratios)

    # plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=-0.5 / no_plots)
    plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=0.05)

    apps = scenario['apps']
    timestamps = scenario['timestamps']

    lines_dict = dict()

    def on_pick(event):
        legend_text = None
        # Check if the pick event is on the legend text
        if event.artist in legend.get_texts():
            legend_text = event.artist.get_text()
        # Check if the pick event is on the legend line
        elif event.artist in legend.get_lines():
            line_index = legend.get_lines().index(event.artist)
            legend_text = legend.get_texts()[line_index].get_text()

        if legend_text:
            # Find the original lines associated with this legend entry
            origlines = lines_dict.get(legend_text, [])
            if origlines:
                visibility = origlines[0].get_visible()
                for origline in origlines:
                    origline.set_visible(not visibility)
                plt.gcf().canvas.draw()

    fig.canvas.mpl_connect('pick_event', on_pick)

    ax = None

    if plot_loss:
        if ax is None:
            ax = plt.subplot(gs[plot_number])
        else:
            plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['loss'], label=app['name'])

        if args.latex:
            print("\nLaTeX Loss:")
            for app in apps:
                printLatexBasicplot(app['time'], app['loss'], f"label={app['name']}")
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("Loss [%]")

    if plot_latency:
        if ax is None:
            ax = plt.subplot(gs[plot_number])
        else:
            plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['latency'], label=app['name'])

        if args.latex:
            print("\nLaTeX Latency:")
            for app in apps:
                printLatexBasicplot(app['time'], app['latency'], f"label={app['name']}")

        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("Latency [ms]")

    if ax is None:
        ax = plt.subplot(gs[plot_number])
    else:
        plt.subplot(gs[plot_number], sharex=ax)
    plot_number += 1
    if len(apps) == 1:  # If there is only one application, plot everything for it
        app = apps[0]
        plt.plot(app['time'], app['sendrate'],
                 label=app['name'], linewidth=1.5)
        if 'A_s' in app:
            plt.plot(app['time'], app['A_s'], label="A_s")
        if 'A_r' in app:
            plt.plot(app['time'], app['A_r'], label="A_r")
        plt.plot(app['time'], app['bottleneck_share'], label="fair share",
                 linewidth=0.5, color='black', linestyle='--')
        print(f"Median send rate:        {np.median(app['sendrate'])}")
    else:
        # Plot the sending rate for all applications
        for app in apps:
            lines = ()
            line_sendrate, = plt.plot(app['time'], app['sendrate'], label=app['name'], picker=5)
            lines += (line_sendrate,)
            # Plot bottleneck share and transition rates without a label to exclude them from the legend
            if 'bottleneck_share' in app:
                line_fair_share, = plt.plot(app['time'], app['bottleneck_share'], linewidth=0.5, color='black', linestyle='--', picker=5)
                # if not the detail app, hide the fair share line
                if app['name'] != apps[detail_app]['name']:
                    line_fair_share.set_visible(False)
                lines += (line_fair_share,)
            if 'oldrate' in app:
                line_oldrate, = plt.plot(app['time_transition'], app['oldrate'], linewidth=0.5, color='red', linestyle=':', picker=5)
                line_oldrate.set_visible(False)
                lines += (line_oldrate,)
            if 'newrate' in app:
                line_newrate, = plt.plot(app['time_transition'], app['newrate'], linewidth=0.5, color='blue', linestyle='-.', picker=5)
                line_newrate.set_visible(False)
                lines += (line_newrate,)

            # Store references to the lines for toggling
            lines_dict[app['name']] = lines

        if args.latex:
            print("\nLaTeX Sendrate:")
            for app in apps:
                printLatexBasicplot(app['time'], app['sendrate'], f"label={app['name']}")

        # Print highest median send rate
        highest_median = 0
        highest_median_app = ""
        lowest_median = 1000
        lowest_median_app = ""
        for app in apps:
            median = np.median(app['sendrate'])
            if median > highest_median:
                highest_median = median
                highest_median_app = app['name']
            if median < lowest_median:
                lowest_median = median
                lowest_median_app = app['name']
        print(f"Highest median send rate: {highest_median} by app {highest_median_app})")
        print(f"Lowest median send rate:  {lowest_median} by app {lowest_median_app})")
    # Draw a gridline through 0
    plt.axhline(y=0, color='black', linewidth=0.5, linestyle='--')
    if len(apps) <= max_apps_legend:
        legend = plt.legend()
        # Make legend entries pickable
        for legline, origline in zip(legend.get_lines(), legend.get_texts()):
            legline.set_picker(5)  # 5 pts tolerance
            origline.set_picker(5)  # Also make the text pickable
    plt.ylabel("Send rate [MB/s]")

    if plot_total_send_rate:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1

        # Get times and total send rates as list, sendrates are keyed under ['total_sendrate']
        times = list(timestamps.keys())
        sendrates = [r['total_sendrate'] for r in timestamps.values()]

        plt.plot(times, sendrates, label="Total send rate")
        plt.axhline(y=0, color='black', linewidth=0.5, linestyle='--')

        # Draw some lines for reference. Since all links are 1MB/s, a simple
        # upper bound for the total min(no_apps, no_paths)
        upper_bound = min(len(apps), max(apps[0]['active_path']) + 1)

        if upper_bound < max(sendrates) * 1.5 and upper_bound > 0.5 * max(sendrates):
            plt.axhline(y=upper_bound, color='black', linewidth=0.5, linestyle='--', label="Upper bound")

        print(f"Max total send rate:     {max(sendrates)}")
        print(f"Median total send rate:  {np.median(sendrates)}")
        print(f"Mean total send rate:    {np.mean(sendrates)}")

        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.legend()
        plt.ylabel("Total Send Rate [MB/s]")

    if plot_active_paths:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['active_path'], label=app['name'])

        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("Paths")

        if args.latex:
            print("\nLaTeX Active Paths:")
            for app in apps:
                printLatexBasicplot(app['time'], app['active_path'], f"label={app['name']}", sharp=True)

    if plot_gcc_state:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['gcc_state'], label="State",
                 drawstyle='steps-post', linestyle='--', linewidth=1)
        # plt.plot(app['time'], app['gcc_signal'], label="Signal",
        #          drawstyle='steps-post', linestyle='-.', linewidth=1)
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("GCC State")

    if plot_gcc_gradient:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['gradient'], label="Gradient",
                 color='red', linestyle='-', linewidth=1)
        plt.plot(app['time'], app['threshold_hi'], label="threshold γ",
                 color='black', linestyle=':', linewidth=0.5)
        plt.plot(app['time'], app['threshold_lo'], label="threshold -γ",
                 color='black', linestyle=':', linewidth=0.5)
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("GCC Gradient")

    if plot_gcc_params:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['kalman_gain'], label="Kalman gain",
                 color='red', linestyle='-', linewidth=1)
        plt.plot(app['time'], app['variance'], label="Variance",
                 color='green', linestyle='--', linewidth=1)
        plt.plot(app['time'], app['error'], label="Error",
                 color='black', linestyle=':', linewidth=1)

        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("GCC Parameters")

    if plot_active_flows:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1

        plt.plot(list(timestamps.keys()), [r['active_apps'] for r in timestamps.values()], label="Active flows")
        plt.ylabel("Active flows")

    xlabel = "Time (s)"
    plt.xlabel(xlabel)

    settings = scenario['settings']

    if 'time_slots' in settings:
        # show vertical grid lines at time slots
        for i in range(1, settings['time_slots']):
            plt.axvline(x=i * settings['slot_size'], color='gray', linestyle='--', linewidth=0.5)

    plt.show()


def parseScenarioResults(file, inputs: dict) -> dict:
    apps = []
    while (True):
        line = file.readline().strip()
        if "End Apps evaluation" in line:
            break
        elif "Host at" in line:
            host_tokens = line.split(' ')
            host = host_tokens[2]
        elif "{" in line:
            app = json.loads(line)
            app['host'] = host
            # print(f"Found results for host {host} app {
            #       app['app_id']} type {app['app_type']}")

            if 'states' not in app or app['states'] is None:
                print(f"    No states found for app {app['app_id']}")
                print(json.dumps(app, indent=4))
                continue

            # Select only states from args.start_time onwards
            app['states'] = [state for state in app['states'] if state['time'] >= args.start_time * 1000]

            # Remove startup from time and convert to seconds
            app['time'] = [state['time'] / 1000 -
                           args.start_time for state in app['states']]

            # Convert loss to percentage
            app['loss'] = [state['loss'] * 100 for state in app['states']]

            # Optional metrics for path transitions
            if 'in_transition' in app['states'][0]:
                app['oldrate'] = []
                app['newrate'] = []
                app['time_transition'] = []
                for i, state in enumerate(app['states']):
                    if state['in_transition']:
                        app['oldrate'].append(state['oldrate'])
                        app['newrate'].append(state['newrate'])
                        app['time_transition'].append(app['time'][i])
                    else:
                        app['oldrate'].append(None)
                        app['newrate'].append(None)
                        app['time_transition'].append(None)

            # For ever other field in states, generate a list of values
            for field in app['states'][0]:
                if field not in app:
                    app[field] = getStateAttributeList(app, field)

            if args.avg > 1:
                for field in ['sendrate', 'loss', 'latency']:
                    # Average args.avg many values together
                    app[field] = [np.mean(app[field][i:i + args.avg])
                                  for i in range(0, len(app[field]), args.avg)]
                app['time'] = [app['time'][i] for i in range(0, len(app['time']), args.avg)]
                app['active_path'] = [app['active_path'][i] for i in range(0, len(app['active_path']), args.avg)]
                if 'bottleneck_share' in app:
                    app['bottleneck_share'] = [app['bottleneck_share'][i] for i in range(0, len(app['bottleneck_share']), args.avg)]

            if not 'name' in app:
                app['name'] = f"{app['app_type']}#{app['app_id']} "

            #     print(f"    Bytes sent:     {app['bytes_sent']}")
            #     print(f"    Bytes received: {app['bytes_received']}")

            app['total_running_time'] = app['time'][-1] - app['time'][0]
            app['average_bandwidth'] = app['bytes_received'] / app['total_running_time'] / 1e6

            app['bytes_lost'] = app['bytes_sent'] - app['bytes_received']
            if app['bytes_sent'] > 0:
                app['total_loss_percentage'] = app['bytes_lost'] / app['bytes_sent'] * 100
            else:
                print(f"App {app['name']} has 0 bytes sent")
                app['total_loss_percentage'] = -1

            apps.append(app)

    if len(apps) == 0:
        print(f"No app results found in {file.name}")
        return {}
    else:
        print(f"Loaded {len(apps):2} apps from {file.name}")

    # Pre-process a dict with total sendrate and number of active apps at any point in time
    timestamps = {}
    for app in apps:
        for state in app['states']:
            t = state['time']
            if t not in timestamps:
                timestamps[t] = {'active_apps': 0, 'total_sendrate': 0}

            # if (state['sendrate'] == 0):
            #     continue

            timestamps[t]['active_apps'] += 1
            timestamps[t]['total_sendrate'] += state['sendrate']

    median_total_sendrate = np.median([r['total_sendrate'] for r in timestamps.values()])

    timestamps = {t / 1000 - args.start_time: r for t, r in timestamps.items()}
    timestamps = dict(sorted(timestamps.items()))

    settings = {}
    time_slots = {}

    if 'settings' in inputs:
        settings = inputs['settings']

        time_slots = {}
        for t, r in timestamps.items():
            slot = int(t // settings['slot_size'])
            if slot not in time_slots:
                time_slots[slot] = {}
            if 'total_sendrates' not in time_slots[slot]:
                time_slots[slot]['total_sendrates'] = []
            time_slots[slot]['total_sendrates'].append(r['total_sendrate'])

        for slot, results in time_slots.items():
            time_slots[slot]['median_total_sendrate'] = np.median(results['total_sendrates'])

        # For each app, sum up the average sendrate in every time slot
        for app in apps:
            for t, s in zip(app['time'], app['sendrate']):
                slot = int(t // settings['slot_size'])
                if 'sendrate' not in time_slots[slot]:
                    time_slots[slot]['sendrate'] = []
                time_slots[slot]['sendrate'].append(s)
        for slot, results in time_slots.items():
            time_slots[slot]['median_sendrate'] = np.median(results['sendrate'])
            time_slots[slot]['mean_sendrate'] = np.mean(results['sendrate'])

    scenario = {
        'apps': apps,
        'timestamps': timestamps,
        'time_slots': time_slots,
        'median_total_sendrate': median_total_sendrate,
        'settings': settings,
    }

    return scenario


def plotMultiScenario(scenarios: list[dict]) -> None:
    # Map containing the global results per flow type, per number of flows
    global_results = {}

    # Process only for these number of flows
    selected_flow_nums = [4, 8, 16, 20, 24]

    # Pre-process all scenarios by grouping all data by flow type and number of flows
    for scenario in scenarios:
        # Use description as flow type
        flow_type = scenario['settings']['description']
        if flow_type not in global_results:
            global_results[flow_type] = {}

        num_flows = scenario['settings']['num_flows']

        # Uncomment to process only for selected flow numbers
        # if num_flows not in selected_flow_nums:
        #     continue

        if num_flows not in global_results[flow_type]:
            global_results[flow_type][num_flows] = {}

        # Median of the total sendrate throughout the entire simulation
        if 'median_total_sendrates' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['median_total_sendrates'] = []
        global_results[flow_type][num_flows]['median_total_sendrates'].append(scenario['median_total_sendrate'])

        # Compute the sum of the average bandwidths of each flow
        # The avg bandwidth is computed as the total bytes received divided by the total running time
        if 'cumulative_avg_bandwidths' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['cumulative_avg_bandwidths'] = []
        cumulative_avg_bandwidth = sum([app['average_bandwidth'] for app in scenario['apps']])
        global_results[flow_type][num_flows]['cumulative_avg_bandwidths'].append(cumulative_avg_bandwidth)

        # Compute the minimum share as the minimum avg sendrate of each (flow,timeslot) pair
        # The values per timeslots are computed during individual simulation output processing
        if 'min_shares' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['min_shares'] = []
        min_share = float('inf')
        for slot, results in scenario['time_slots'].items():
            if results['mean_sendrate'] < min_share:
                min_share = results['mean_sendrate']
        global_results[flow_type][num_flows]['min_shares'].append(min_share)

        # Lists containing loss/latency piled up over all flows AND all iterations
        # Up to n are averaged together (within a flow). Override using the --avg n argument
        if 'all_losses' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['all_losses'] = []
        if 'all_latencies' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['all_latencies'] = []
        all_losses = []
        all_latencies = []
        for app in scenario['apps']:
            # To remove noise, consider only the averages across some interval for loss and latency
            # Since the actual values are recorded in 50ms intervals internally, we we average
            # 6 values together to get the values within a 300ms interval. Override with --avg n
            if args.avg > 1:
                # If --avg was used, we already average the values in the individual results parsing
                average_num = 1
            else:
                average_num = 6

            if args.loss:
                losses = app['loss']
                averaged_losses = [np.mean(losses[i:i+average_num]) for i in range(0, len(losses), average_num)]
                all_losses.extend(averaged_losses)

            if args.latency:
                latencies = app['latency']
                averaged_latencies = [np.mean(latencies[i:i+average_num]) for i in range(0, len(latencies), average_num)]
                all_latencies.extend(averaged_latencies)

        # NOTE: Extend, not append, this is simply a flat list
        global_results[flow_type][num_flows]['all_losses'].extend(all_losses)
        global_results[flow_type][num_flows]['all_latencies'].extend(all_latencies)

        if 'total_loss_percentages' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['total_loss_percentages'] = []
        # List of loss percentages for each flow
        # The loss for each flow is the percentage of (bytes_lost / bytes_sent)
        loss_percs = []
        for app in scenario['apps']:
            if 'total_loss_percentage' in app:
                loss_percs.append(app['total_loss_percentage'])
        global_results[flow_type][num_flows]['total_loss_percentages'].append(loss_percs)

        if 'latencies' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['latencies'] = []
        # List of lists of latencies for each flow
        lats = [app['latency'] for app in scenario['apps']]
        global_results[flow_type][num_flows]['latencies'].append(lats)

        # List of variances of latency for each flow
        if 'latency_variances' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['latency_variances'] = []
        lat_vars = [np.var(app['latency']) for app in scenario['apps']]
        global_results[flow_type][num_flows]['latency_variances'].append(lat_vars)

        if 'total_bytes_received' not in global_results[flow_type][num_flows]:
            global_results[flow_type][num_flows]['total_bytes_received'] = []
        # List of total bytes received for each flow
        tot = [app['bytes_received'] for app in scenario['apps']]
        global_results[flow_type][num_flows]['total_bytes_received'].append(tot)

    # Compute the mean and the 95% confidence interval of all medians of the total send rates per flow type and per number of flows
    for flow_type, flow_results in global_results.items():
        for num_flows, results in flow_results.items():
            if args.total_received:
                results['mean_total'] = np.mean(results['total_bytes_received'])
                results['ci_total'] = 1.96 * np.std(results['total_bytes_received'], ddof=1) / np.sqrt(len(results['total_bytes_received']))
            elif args.min_share:
                results['mean_total'] = np.mean(results['min_shares'])
                results['ci_total'] = 1.96 * np.std(results['min_shares'], ddof=1) / np.sqrt(len(results['min_shares']))
            else:  # CumAvgBw
                results['mean_total'] = np.mean(results['cumulative_avg_bandwidths'])
                results['ci_total'] = 1.96 * np.std(results['cumulative_avg_bandwidths'], ddof=1) / np.sqrt(len(results['cumulative_avg_bandwidths']))

                # Alternative where we use the mean of the medians of total sendrate in every time slot
                # results['mean_total'] = np.mean(results['median_total_sendrates'])
                # results['ci_total'] = 1.96 * np.std(results['median_total_sendrates'], ddof=1) / np.sqrt(len(results['median_total_sendrates']))

    # Dump the global results to a file
    # with open('global_results.json', 'w') as f:
    #     json.dump(global_results, f, indent=4)

    # Plot the results
    fig, ax = plt.subplots(figsize=(10, 6))

    # Create a mapping from original x values to evenly spaced integers
    flow_types = list(global_results.keys())
    all_num_flows = sorted(global_results[flow_types[0]].keys())
    x_mapping = {num_flows: i * 6 for i, num_flows in enumerate(all_num_flows)}

    markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p', '*', 'h']
    colors = plt.cm.tab10(np.linspace(0, 1, len(global_results)))

    ax.set_xlabel("# of Flows", fontsize=14)

    # Calculate dodge positions to avoid overlap of the boxes
    dodge_width = 0.9
    total_dodge_space = 1.0

    # Use the highest number of flows for the boxplots
    num_flows_for_boxplot = len(all_num_flows) - 2

    if args.loss:
        ax.set_ylabel("Loss (%)", fontsize=14)
        for i, (flow_type, color, marker) in enumerate(zip(flow_types, colors, markers)):
            num_flows = sorted(global_results[flow_type].keys())
            losses = [global_results[flow_type][n]['losses'] for n in num_flows]
            dodge_positions = [x_mapping[n] + (i - len(flow_types)/2) * total_dodge_space for n in num_flows]

            ax.boxplot(losses, positions=dodge_positions, widths=dodge_width, showfliers=False, patch_artist=True,
                       boxprops=dict(facecolor=color, color=color), medianprops=dict(color='black'),
                       whiskerprops=dict(color=color), capprops=dict(color=color))

            ax.plot([], label=flow_type, color=color, marker=marker, markersize=8, linewidth=2)

            if args.latex:
                printLatexBoxplot(losses[num_flows_for_boxplot], f"label={flow_type}")

    elif args.latency:
        ax.set_ylabel("Latency (ms)", fontsize=14)
        for i, (flow_type, color, marker) in enumerate(zip(flow_types, colors, markers)):
            num_flows = sorted(global_results[flow_type].keys())
            latencies = [global_results[flow_type][n]['latencies'] for n in num_flows]

            # TODO: Plot the variance of the latencies
            # latencies = [global_results[flow_type][n]['latency_variances'] for n in num_flows]
            # for i, ll in enumerate(latencies):
            #     # Flatten the inner lists
            #     latencies[i] = [item for sublist in ll for item in sublist]
            print(latencies)
            dodge_positions = [x_mapping[n] + (i - len(flow_types)/2) * total_dodge_space for n in num_flows]

            # print dimensions of both
            print(len(latencies), len(dodge_positions))

            ax.boxplot(latencies, positions=dodge_positions, widths=dodge_width, showfliers=False, patch_artist=True,
                       boxprops=dict(facecolor=color, color=color), medianprops=dict(color='black'),
                       whiskerprops=dict(color=color), capprops=dict(color=color))

            ax.plot([], label=flow_type, color=color, marker=marker, markersize=8, linewidth=2)

            if args.latex:
                printLatexBoxplot(latencies[num_flows_for_boxplot], f"label={flow_type}")
    elif args.min_share:
        ax.set_ylabel("MinShare (MB/s)", fontsize=14)

        for (flow_type, color, marker), flow_results in zip(zip(global_results.keys(), colors, markers), global_results.values()):
            num_flows = sorted(flow_results.keys())
            mean_totals = [flow_results[n]['mean_total'] for n in num_flows]
            ci_totals = [flow_results[n]['ci_total'] for n in num_flows]

            ax.errorbar(num_flows, mean_totals, yerr=ci_totals, fmt='-o', label=flow_type, color=color, marker=marker, markersize=8, linewidth=2, capsize=5)

            if args.latex:
                printLatexErrorbar(num_flows, mean_totals, ci_totals, f"label={flow_type}")
    else:
        ax.set_ylabel("CumAvgBW (MB/s)", fontsize=14)

        for (flow_type, color, marker), flow_results in zip(zip(global_results.keys(), colors, markers), global_results.values()):
            num_flows = sorted(flow_results.keys())
            mean_totals = [flow_results[n]['mean_total'] for n in num_flows]
            ci_totals = [flow_results[n]['ci_total'] for n in num_flows]

            ax.errorbar(num_flows, mean_totals, yerr=ci_totals, fmt='-o', label=flow_type, color=color, marker=marker, markersize=8, linewidth=2, capsize=5)

            if args.latex:
                printLatexErrorbar(num_flows, mean_totals, ci_totals, f"label={flow_type}")
    if args.loss or args.latency:
        ax.set_xticks(list(x_mapping.values()))
        ax.set_xticklabels(list(x_mapping.keys()))
    else:
        ax.set_xticks(all_num_flows)

    ax.grid(True, which='both', linestyle='--', linewidth=0.5)

    ax.tick_params(axis='both', which='major', labelsize=12)

    # ax.grid(True, which='both', axis='x', linestyle='--', linewidth=0.5)
    # ax.set_title("Average Bandwidth by Flow Type", fontsize=16)

    # Customize tick parameters
    # ax.tick_params(axis='both', which='major', labelsize=12)
    # ax.set_xticks(num_flows)

    # Add gridlines
    # ax.grid(True, which='both', linestyle='--', linewidth=0.5)

    # Render the legend above the plot with a shadow and a frame
    ax.legend(loc='upper center', bbox_to_anchor=(0.5, -0.1), fancybox=True, shadow=True, ncol=3, fontsize=12)

    plt.tight_layout()  # Adjust the layout to make room for the legend

    plt.show()


def main():
    global args

    args = parse_args()
    paths = []
    if os.path.isdir(args.filepath):
        for file in os.listdir(args.filepath):
            if file.endswith(".out.txt"):
                paths.append(os.path.join(args.filepath, file))
    else:
        paths.append(args.filepath)

    print(f"Found {len(paths)} files")

    scenarios = []
    for filepath in paths:
        # First, load the corresponding inputs file to get meta information like time slots
        inputs_json_path = filepath.replace(".out.txt", ".json")
        with open(inputs_json_path) as f:
            inputs = json.load(f)
            # print(f"Loaded inputs from {inputs_json_path}")

        # Then, load the app metrics from the simulation output file
        with open(filepath) as file:
            while True:
                line = file.readline()
                if line == '':
                    break
                if "# Host Apps evaluation" in line:
                    s = parseScenarioResults(file, inputs)
                    if s:
                        scenarios.append(s)

    print("Processed a total of", len(scenarios), "scenarios")

    if len(scenarios) == 1:
        plotSingleScenario(scenarios[0])
    elif len(scenarios) > 1:
        plotMultiScenario(scenarios)

    print("Done. Exiting.")


main()
