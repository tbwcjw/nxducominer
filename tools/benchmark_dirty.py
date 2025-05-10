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



parser = argparse.ArgumentParser(description="parse nxducominer through nxlink server for benchmarking")
parser.add_argument("-d", "--duration", type=str, default="1h", help="run duration (e.g., 10m, 1h, 30s)")
parser.add_argument("--nx", required=True, type=str, help="Path to NXLink")
parser.add_argument("--ip", required=True, type=str, help="Switch IP")
parser.add_argument("--nro", required=True, type=str, help="Path to application.nro")
parser.add_argument("-o", "--output", type=str, default="nxducominer_benchmark.txt", help="Path to save benchmark results")
args = parser.parse_args()

duration = args.duration
nxlink_path = args.nx
switch_ip = args.ip
nro_path = args.nro
file_path = args.output

app_version_pattern = re.compile(r'(\d{2}\.\d{2}\.\d{2}_\d{2}\.\d{2})')
hashrate_pattern = re.compile(r'Hashrate\s*[:]\s*([\d.]+)\s*kH/s', re.IGNORECASE)
difficulty_pattern = re.compile(r'Difficulty\s*[:]\s*([\d.]+)', re.IGNORECASE)
total_pattern = re.compile(r'Total\s*[:\|]?\s*(\d+)', re.IGNORECASE)
cpu_boosted_pattern = re.compile(r'Hashrate\s*:\s*[\d.]+\s*\w+/s\s*\(CPU Boosted\)', re.IGNORECASE)

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

            if any([hr_match, diff_match, total_match, cpu_boosted_match, app_version_match]):
                if start_time is None:
                    start_time = time.time()
                    print("benchmark started")
                    sys.stdout.flush()

                elapsed = time.time() - start_time
                if elapsed > duration_secs:
                    break

                if app_version_match:
                    app_version = str(app_version_match.group(1))

                if cpu_boosted_match:
                    cpu_boosted = True

                if hr_match:
                    hashrates.append(float(hr_match.group(1)))

                if diff_match:
                    difficulties.append(float(diff_match.group(1)))

                if total_match:
                    total_sum += int(total_match.group(1))

                avg_hashrate = statistics.mean(hashrates) if hashrates else 0
                avg_difficulty = statistics.mean(difficulties) if difficulties else 0

                if os.name == 'nt':
                    os.system('cls')
                else:
                    os.system('clear')

                print(f"duration: {duration_secs}s\n"
                      f"elapsed time: {elapsed:.2f}s\n"
                      f"app version: {app_version}\n"
                      f"avg hashrate: {avg_hashrate:.2f} kH/s\n"
                      f"avg difficulty: {avg_difficulty:.2f}\n"
                      f"total shares: {total_sum}\n"
                      f"cpu boosted: {cpu_boosted}")
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
    f"cpu boosted: {cpu_boosted}"
)

with open(file_path, "w") as f:
    f.write(output)

print("bye!");

if os.name == 'nt':
    os.startfile(file_path)
else:
    os.system(f"xdg-open {file_path}")

exit(0);