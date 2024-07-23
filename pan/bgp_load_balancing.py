import networkx as nx
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import random
import hashlib
import xml.etree.ElementTree as ET
import sys


class Node:
    def __init__(self, node_id):
        self.node_id = node_id
        self.interfaces = {}
        self.interface_count = 1
        self.remote_as_info = {}

    def add_interface(self, to_node):
        interface_number = self.interface_count
        self.interfaces[interface_number] = to_node.node_id
        self.remote_as_info[interface_number] = to_node
        self.interface_count += 1
        return interface_number


def assign_interfaces(nodes, links):
    link_id = 0
    xml_edges = []
    for from_node_id, to_node_id in links:
        from_node = nodes[from_node_id]
        to_node = nodes[to_node_id]
        bandwidth = 1
        from_interface = from_node.add_interface(to_node)
        to_interface = to_node.add_interface(from_node)

        if from_node_id < to_node_id:
            xml_edges.append((link_id, from_node_id, to_node_id, from_interface, to_interface))
        else:
            xml_edges.append((link_id, to_node_id, from_node_id, to_interface, from_interface))

        # print(f"Node {from_node_id} Interface {from_interface} <-> Node {to_node_id} Interface {to_interface}")
        link_id += 1

    # print(xml_edges)
    return xml_edges


def find_interface_level_path(G, source_as, destination_as, use_ecmp: bool):
    shortest_as_path = shortest_path_multigraph(G, source_as, destination_as)
    interface_path = []
    current_interface = 0

    for i in range(len(shortest_as_path) - 1):
        current_node = shortest_as_path[i]
        next_node = shortest_as_path[i + 1]
        selected_key = select_egress_interface(G, current_interface, current_node, next_node, use_ecmp)
        edge_data = G[current_node][next_node][selected_key]

        # Append the current egress interface and the corresponding ingress interface at the next AS
        if current_node < next_node:
            egress_interface = edge_data['from_interface']
            ingress_interface = edge_data['to_interface']
        else:
            egress_interface = edge_data['to_interface']
            ingress_interface = edge_data['from_interface']

        interface_path.append((current_node, current_interface, egress_interface))
        current_interface = ingress_interface

    # End at the destination AS with the special interface (0)
    interface_path.append((destination_as, current_interface, 0))

    return interface_path


def shortest_path_multigraph(G, source, target):
    all_simple_paths = []
    unique_paths_set = set()

    def find_paths(current_node, target, path, visited):
        if current_node == target:
            unique_key = tuple(path)
            if unique_key not in unique_paths_set:
                unique_paths_set.add(unique_key)
                all_simple_paths.append(list(path))
            return

        for neighbor in G.neighbors(current_node):
            if neighbor not in visited:
                path.append(neighbor)
                visited.add(neighbor)
                find_paths(neighbor, target, path, visited)
                path.pop()
                visited.remove(neighbor)

    find_paths(source, target, [source], {source})
    shortest_path = sorted(all_simple_paths, key=lambda x: len(x))[0]
    return shortest_path


def select_egress_interface(G, curr_interface, current_node, next_node, use_ecmp: bool):
    edgess = G[current_node][next_node]
    edge_keys = list(edgess.keys())

    # For full diversity flow-based load balancing, we allow all egresses
    # independently of the IGP cost
    if not use_ecmp:
        selected__key = edge_keys[TUPLE_HASH % len(edge_keys)]
        return selected__key

    n = G.degree(current_node) + 1
    k = int(0.5 * n)
    rewiring_prob = 0.5
    if k % 2 != 0:
        k += 1  # Ensure k is even
    seed = int(hashlib.sha256(str(current_node).encode()).hexdigest(), 16)
    ws_graph = nx.watts_strogatz_graph(n, k, rewiring_prob, seed=seed)
    ok = 0
    while not nx.is_connected(ws_graph):
        ok += 1
        ws_graph = nx.watts_strogatz_graph(n, k, rewiring_prob, seed=seed+ok)
    ws_hops_matrix = nx.floyd_warshall_numpy(ws_graph)

    if current_node < next_node:
        values = [(key, ws_hops_matrix[curr_interface, edgess[key]['from_interface']]) for key in edge_keys]
    else:
        values = [(key, ws_hops_matrix[curr_interface, edgess[key]['to_interface']]) for key in edge_keys]

    min_value = min(values, key=lambda x: x[1])[1]
    # Select all egress interfaces that have the minimum value
    min_value_keys = [key for key, value in values if value == min_value]

    selected__key = min_value_keys[TUPLE_HASH % len(min_value_keys)]
    return selected__key


def find_min_cost_egress_nodes(cost_matrix, node, egress_nodes, margin=0.0):
    """
    Find the egress node(s) from a specified node with the least IGP cost.
    Args:
        cost_matrix (ndarray): The IGP cost matrix.
        node (int): The node of interest (0-based index).
        egress_nodes (list): List of egress node indices (0-based index).
        margin (float): The acceptable cost difference margin for considering multiple nodes.
    Returns:
        list: List of egress nodes with the least IGP cost within the margin.
    """
    costs = cost_matrix[node, egress_nodes]
    print(costs)
    min_cost = costs.min()
    print(min_cost)
    min_nodes = [egress_nodes[i] for i, cost in enumerate(costs) if cost <= min_cost + margin]
    return min_nodes


# Function to assign bandwidth to edges and calculate OSPF cost
def assign_bandwidth_and_calculate_ospf_cost(graph, bandwidth_range=(1, 10)):
    # Assign random bandwidth to edges
    for u, v in graph.edges():
        bandwidth = np.random.uniform(*bandwidth_range)
        graph[u][v]['bandwidth'] = bandwidth
        graph[u][v]['weight'] = 1 / bandwidth  # OSPF cost


# Function to compute the IGP cost matrix
def compute_IGP_cost_matrix(graph):
    length = dict(nx.all_pairs_dijkstra_path_length(graph, weight='weight'))
    nodes = list(graph.nodes())
    matrix = np.zeros((len(nodes), len(nodes)))
    for i, u in enumerate(nodes):
        for j, v in enumerate(nodes):
            matrix[i, j] = length[u][v]
    return np.round(matrix, 1)


# Function to display the matrices side by side
def display_matrices_side_by_side(matrices, titles):
    fig, axs = plt.subplots(1, 3, figsize=(20, 6))  # Create subplots

    for i, (matrix, title) in enumerate(zip(matrices, titles)):
        df = pd.DataFrame(matrix)
        axs[i].axis('tight')
        axs[i].axis('off')
        table = pd.plotting.table(axs[i], df, loc='center', cellLoc='center', colWidths=[0.04] * len(df.columns))
        table.auto_set_font_size(False)
        table.set_fontsize(8)
        axs[i].set_title(title)

    plt.show()


def parse_topology(xml_file):
    tree = ET.parse(xml_file)
    root = tree.getroot()

    nodes = {}
    links = []

    # Parse nodes
    for node in root.findall('node'):
        node_id = int(node.get('id'))
        nodes[node_id] = Node(node_id)

    # Parse links
    for link in root.findall('link'):
        from_node = int(link.find('from').text)
        to_node = int(link.find('to').text)
        links.append((from_node, to_node))

    return nodes, links


def pretty_print_path(path):
    transformed_path = []

    # Process each tuple in the path
    for i, (as_number, ingress, egress) in enumerate(path):
        if i == 0:  # Host_AS (first element)
            transformed_path.append(f"-{0}->{{AS {as_number}}}->{egress-1}")
        elif i == len(path) - 1:  # Destination_AS (last element)
            transformed_path.append(f"-{ingress-1}->{{AS {as_number}}}->{0}")
        else:  # Intermediate_AS
            transformed_path.append(f"-{ingress-1}->{{AS {as_number}}}->{egress-1}")

    print('['+", ".join(transformed_path)+', ]')


def transform_path_json(path):
    """
    Convert path into a dict of hops for easy JSON output
    Args:
        path: an interface-level path as returned by get_path()
    """
    hops = []
    for (as_number, ingress, egress) in path:
        hops.append({
            "as_no": as_number,
            "ingress": ingress - 1,
            "egress": egress - 1
        })
    return {
        "hops": hops
    }


def get_path(input_file, source, s_host, destination, d_host, flow_id, it, use_ecmp=False):
    global TUPLE_HASH, IT

    flow_tuple = str((source, s_host, destination, d_host, flow_id))
    TUPLE_HASH = int(hashlib.sha256(flow_tuple.encode()).hexdigest(), 16)
    IT = it

    nodes, links = parse_topology(input_file)
    xml_edges = assign_interfaces(nodes, links)
    G = nx.MultiGraph()
    for edge in xml_edges:
        G.add_edge(edge[1], edge[2], key=edge[0], bandwidth=1, from_interface=edge[3], to_interface=edge[4])

    interface_level_path = find_interface_level_path(G, source, destination, use_ecmp)

    return interface_level_path

TUPLE_HASH = 0
IT = 0
if __name__ == "__main__":
    # output is the interface_level_path:
    # [(Host_AS, 0, egress_interface), (Intermediate_AS, ingress_interface, egress_interface), ... , (Destination_AS, ingress_interface, 0)]

    if len(sys.argv) > 1:
        # Read inputs from args
        if len(sys.argv) != 8:
            print("Usage: python script.py source_as source_host destination_as destination_host flow_id xml_file iteration")
            sys.exit(1)
        source_AS = int(sys.argv[1])
        source_host = int(sys.argv[2])
        destination_AS = int(sys.argv[3])
        destination_host = int(sys.argv[4])
        flow_id = sys.argv[5]
        xml_file = sys.argv[6]
        it = sys.argv[7]
    else:
        # Use default values
        it = 0
        source_AS = 0
        destination_AS = 2
        source_host = 1
        destination_host = 1
        flow_id = 0
        xml_file = r"C:\Users\eehsa\Downloads\toy-topology.xml"  # Replace with the path to your XML file
    
    path = get_path(xml_file, source_AS, source_host, destination_AS, destination_host, flow_id, it)
    pretty_print_path(path)

