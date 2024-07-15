import json
import random
import argparse

def generate_random_event(app_id, time):
    source_node = random.randint(0, 3)
    target_node = random.randint(0, 3)
    
    while source_node == target_node:
        target_node = random.randint(0, 3)
    
    args = ["0", str(source_node), "2", "0", str(target_node), "2", "rtc", "0.0", str(app_id), "0"]
    
    event = {
        "time": f"{time:.2f}min",
        "type": "start_application",
        "args": args
    }

    print(f"Application {app_id} sending from {source_node} to {target_node} starting at {time:.2f}min")
    
    return event

def main():
    parser = argparse.ArgumentParser(description='Generate events for a JSON file.')
    parser.add_argument('-e', '--events', type=int, default=4, help='number of events to generate (default: 4)')
    parser.add_argument('-o', '--output', default='configs/traffic-engineering/toy.json', help='output path (default: events.json)')
    
    args = parser.parse_args()
    
    number_of_events = args.events
    events = []
    time = 30.0  # starting time in minutes
    
    for i in range(number_of_events):
        event = generate_random_event(i, time)
        events.append(event)
        time += random.uniform(0.0, 0.1)  # increment time by a random amount for variety
    
    output = {
        "events": events
    }
    
    with open(args.output, 'w') as f:
        json.dump(output, f, indent=4)
    
    print(f"Generated {number_of_events} events and saved to {args.output}")

if __name__ == "__main__":
    main()
