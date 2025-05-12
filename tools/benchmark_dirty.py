#github/tbwcjw
#quick and dirty benchmark tool for nxducominer using the nxlink server


#things this can't do:
#tell your switch model/firmware version
#tell you the cflags used in compilation
#tell you it loves you

import sys
import re
import statistics
import os
import time
import argparse
import io
import threading
import subprocess
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description="parse nxducominer through nxlink server for benchmarking")
parser.add_argument("-d", "--duration", type=str, default="1h", help="run duration (e.g., 10m, 1h, 30s)")
parser.add_argument("--nx", required=True, type=str, help="Path to NXLink")
parser.add_argument("--ip", required=True, type=str, help="Switch IP")
parser.add_argument("--nro", required=True, type=str, help="Path to application.nro")
parser.add_argument("-o", "--output", type=str, default="nxducominer_benchmark.txt", help="Path to save benchmark results")
parser.add_argument("-og", "--output-graph", type=str, default="nxducominer_benchmark_graph.png", help="Path to save benchmark results")
args = parser.parse_args()

duration = args.duration
nxlink_path = args.nx
switch_ip = args.ip
nro_path = args.nro
file_path = args.output
graph_file_path = args.output_graph

app_version_pattern = re.compile(r'(\d{2}\.\d{2}\.\d{2}_\d{2}\.\d{2})')
hashrate_pattern = re.compile(r'Hashrate\s*[:]\s*([\d.]+)\s*kH/s', re.IGNORECASE)
difficulty_pattern = re.compile(r'Difficulty\s*[:]\s*([\d.]+)', re.IGNORECASE)
total_pattern = re.compile(r'Total\s*[:\|]?\s*(\d+)', re.IGNORECASE)
cpu_boosted_pattern = re.compile(r'Hashrate\s*:\s*[\d.]+\s*\w+/s\s*\(CPU Boosted\)', re.IGNORECASE)
threads_pattern = re.compile(r'Threads\s*\((\d+)\)', re.IGNORECASE)
temp_pattern = re.compile(r'Temperature:\s*(\d+\.\d+)')

def parse_duration(duration_str):
    units = {'s': 1, 'm': 60, 'h': 3600}
    try:
        unit = duration_str[-1]
        value = int(duration_str[:-1])
        return value * units[unit]
    except (ValueError, KeyError):
        raise ValueError("Duration must be like '10m', '1h', or '30s'.")

duration_secs = parse_duration(duration)
hashrates = []
difficulties = []
total_sum = 0
start_time = None
elapsed = 0
cpu_boosted = False
app_version = None
threads = 0
temperatures = []  # New list for storing temperature data

# for graphing
timestamps = []
avg_hashrate_series = []
avg_difficulty_series = []
total_shares_series = []
share_deltas = []
temp_series = []  # Series for temperature data

command = [nxlink_path, "-a", switch_ip, nro_path, "-s"]

def run_nxlink():
    with open("nxlink_output.log", "w") as out_file:
        result = subprocess.run(command, stdout=out_file, stderr=subprocess.STDOUT)
        exit_code = result.returncode
        print(f"nxlink error code: {exit_code}")

nxlink_thread = threading.Thread(target=run_nxlink)
nxlink_thread.start()

def clean_console_output(text):
    ansi_escape = re.compile(r'\x1B[@-_][0-?]*[ -/]*[@-~]')
    text = ansi_escape.sub('', text)
    text = text.replace('\r', '')
    text = re.sub('.\x08', '', text)
    text = text.replace('|_', '')
    return text

try:
    with open("nxlink_output.log", "r") as f:
        f.seek(0, io.SEEK_END)

        while True:
            line = clean_console_output(f.readline())
            if not line:
                if nxlink_thread.is_alive():
                    time.sleep(0.1)
                    continue
                else:
                    break

            hr_match = hashrate_pattern.search(line)
            diff_match = difficulty_pattern.search(line)
            total_match = total_pattern.search(line)
            cpu_boosted_match = cpu_boosted_pattern.search(line)
            app_version_match = app_version_pattern.search(line)
            threads_match = threads_pattern.search(line)
            temp_match = temp_pattern.search(line)

            if any([hr_match, diff_match, total_match, cpu_boosted_match, app_version_match, temp_match, threads_match]):
                if start_time is None:
                    start_time = time.time()
                    print("benchmark started")
                    sys.stdout.flush()

                elapsed = time.time() - start_time
                if elapsed > duration_secs:
                    break

                timestamps.append(elapsed)

                if app_version_match:
                    app_version = str(app_version_match.group(1))

                if cpu_boosted_match:
                    cpu_boosted = True

                if hr_match:
                    hr = float(hr_match.group(1))
                    hashrates.append(hr)

                if diff_match:
                    difficulties.append(float(diff_match.group(1)))

                if total_match:
                    new_total = int(total_match.group(1))
                    if total_shares_series:
                        share_deltas.append(new_total - total_shares_series[-1])
                    total_shares_series.append(new_total)
                    total_sum = new_total

                if threads_match:
                    threads = int(threads_match.group(1))

                if temp_match:
                    temp = str(temp_match.group(1))
                    temperatures.append(temp)  # Store temperature data

                avg_hashrate = statistics.mean(hashrates) if hashrates else 0
                avg_difficulty = statistics.mean(difficulties) if difficulties else 0
                avg_hashrate_series.append(avg_hashrate)
                avg_difficulty_series.append(avg_difficulty)

                if os.name == 'nt':
                    os.system('cls')
                else:
                    os.system('clear')

                print(f"duration: {duration_secs}s\n"
                      f"elapsed time: {elapsed:.2f}s\n"
                      f"app version: {app_version}\n"
                      f"avg hashrate: {avg_hashrate:.2f} kH/s\n"
                      f"avg difficulty: {avg_difficulty:.2f}\n"
                      f"temperature: {temp}\n"
                      f"total shares: {total_sum}\n"
                      f"cpu boosted: {cpu_boosted}\n"
                      f"threads: {threads}")
                sys.stdout.flush()

except KeyboardInterrupt:
    print("Interrupted by user. Terminating...")
    sys.stdout.flush()

avg_hashrate = statistics.mean(hashrates) if hashrates else 0
avg_difficulty = statistics.mean(difficulties) if difficulties else 0

output = (
    f"duration: {duration_secs}s\n"
    f"elapsed time: {elapsed:.2f}s\n"
    f"app version: {app_version}\n"
    f"average hashrate: {avg_hashrate:.2f} kH/s\n"
    f"average difficulty: {avg_difficulty:.2f}\n"
    f"total shares: {total_sum}\n"
    f"cpu boosted: {cpu_boosted}\n"
    f"threads: {threads}"
)

with open(file_path, "w") as f:
    f.write(output)

print("bye!")

if os.name == 'nt':
    os.startfile(file_path)
else:
    os.system(f"xdg-open {file_path}")

if timestamps:
    plt.figure(figsize=(12, 10))

    plt.subplot(4, 1, 1)
    plt.plot(timestamps, avg_hashrate_series, label='Avg Hashrate (kH/s)', color='blue')
    plt.ylabel("kH/s")
    plt.title("Average Hashrate Over Time")
    plt.grid(True)

    plt.subplot(4, 1, 2)
    plt.plot(timestamps, avg_difficulty_series, label='Avg Difficulty', color='orange')
    plt.ylabel("Difficulty")
    plt.title("Average Difficulty Over Time")
    plt.grid(True)

    if share_deltas:
        plt.subplot(4, 1, 3)
        plt.plot(timestamps[1:len(share_deltas)+1], share_deltas, label='Share Deltas', color='green')
        plt.xlabel("Time (s)")
        plt.ylabel("Shares Gained")
        plt.title("Shares Per Snapshot")
        plt.grid(True)

    if temperatures:
        plt.subplot(4, 1, 4)
        plt.plot(timestamps, temperatures, label='Temperature (*C)', color='red')
        plt.xlabel("Time (s)")
        plt.ylabel("Temperature (*C)")
        plt.title("Temperature Over Time")
        plt.grid(True)

    plt.tight_layout()
    plt.savefig(graph_file_path)

    if os.name == 'nt':
        os.startfile(graph_file_path)
    else:
        os.system(f"xdg-open {graph_path}")

exit(0)
