import os
import subprocess
import logging
import argparse
import time
import select
import re
from datetime import datetime

# ----------------------------
# CLI Argument Parsing
# ----------------------------
parser = argparse.ArgumentParser()
parser.add_argument('--app-name', help="Container image name, e.g. quay.io/nikesh_sar/logcollector:latest", required=True)
parser.add_argument('--dataset', help="Path to dataset directory", default=None)
parser.add_argument('--base', help="Path to base testbed directory", default=None)
parser.add_argument('--duration', type=int, help="Log streaming duration in seconds", default=20)
parser.add_argument('--log-file', help="Path to save container logs", default=None)
parser.add_argument('--testscript', help="Test script directory", default=None)
args = parser.parse_args()

app_name = args.app_name.strip()
dataset = args.dataset.strip() if args.dataset else None
base_dir = args.base.strip() if args.base else None
duration = args.duration
testscript_dir = args.testscript.strip() if args.testscript else None
log_file_name = args.log_file if args.log_file else f"{app_name.split('/')[-1].replace(':', '_')}_output.log"

container_name = None

# ----------------------------
# Logging Configuration
# ----------------------------
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s: %(message)s'
)

# ---- Logging Setup ----
LOG_DIR =  os.path.join(base_dir, "logs")
os.makedirs(LOG_DIR, exist_ok=True)

timestamp_str = datetime.now().strftime("%Y%m%d_%H%M%S")
user_validation_log_path = os.path.join(LOG_DIR, "testing.log")
testing_log_path = os.path.join(LOG_DIR, "validation.log")
log_file_path = os.path.join(LOG_DIR, log_file_name)

# Create timestamped master log
timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
master_log_path = os.path.join(LOG_DIR, f"master_{timestamp}.log")


logging.basicConfig(
    level=logging.DEBUG,
    format='[%(asctime)s] [%(levelname)s] %(message)s',
    handlers=[
        logging.FileHandler(master_log_path, mode='w'),  # unique log each time
        logging.StreamHandler()  # print to console
    ]
)

def log_detection(message):
    timestamp = datetime.now().isoformat()
    log_line = f"[{timestamp}] [INFO] {message}"
    print(log_line)

    with open(user_validation_log_path, "a") as f:
        f.write(log_line + "\n")

def test_logging(message):
    timestamp = datetime.now().isoformat()
    logging.info(message)
    with open(testing_log_path, "a") as f:
        f.write(message + "\n")

# ----------------------------
# Functions
# ----------------------------
def run_container(app_name):
    """Run the podman container"""
    global container_name
    container_name = f"{app_name.split('/')[-1].replace(':', '_')}-testbed"

    command = ["sudo", "podman", "run", "-d", "--rm",
               "-p", "60001:60001", "-p", "44821:44821"]

    if dataset and os.path.exists(dataset):
        command.extend(["-v", f"{dataset}:/dataset:Z"])

    if base_dir and os.path.exists(base_dir):
        command.extend(["-v", f"{base_dir}:/base:Z"])

    command.extend(["--name", container_name, app_name])

    try:
        logging.info(f"Running container: {' '.join(command)}")
        log_detection(f"Running container: {' '.join(command)}")

        result = subprocess.run(command, check=True, capture_output=True, text=True)

        logging.info(f"Container '{container_name}' started successfully.")
        log_detection(f"Container '{container_name}' started successfully.")
        test_logging(f"Container '{container_name}' started successfully.")

        return True  # success

    except subprocess.CalledProcessError as e:
        logging.error(f"❌ Failed to start container '{container_name}': {e.stderr or e}")
        test_logging(f"Testing failed for {container_name}.")
        log_detection(f"❌ Failed to start container '{container_name}': {e.stderr or e}")
        return False  # failure


def print_container_output(container_name, duration=20):
    start_time = time.time()
    command = ["sudo", "podman", "logs", "-f", container_name]
    logging.info(f"Streaming logs for {duration} seconds...")
    log_detection(f"Streaming logs for {duration} seconds...")

    with open(log_file_path, "w") as log_file:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        try:
            while time.time() - start_time < duration:
                # Wait at most 0.5s for new output
                rlist, _, _ = select.select([process.stdout], [], [], 0.5)
                if rlist:
                    line = process.stdout.readline()
                    if not line:
                        break
                    logging.info(line.strip())
                    log_file.write(line)
                # if no new logs, just loop and check time
        except Exception as e:
            logging.error(f"Error while streaming logs: {e}")
        finally:
            process.terminate()

    logging.info(f"Logs saved to: {log_file_path}")
    log_detection(f"Logs saved to: {log_file_path}")

# verify the output of the container from the log file
def verify_output(log_file_path): 
    """Verify the output of the container from the log file"""
    if not os.path.exists(log_file_path):
        logging.error(f"Log file {log_file_path} does not exist.")
        log_detection(f"Log file {log_file_path} does not exist.")
        return False

    with open(log_file_path, 'r') as log_file:
        content = log_file.read()
        # Flexible match (ignore exact spaces and trailing dots)
        if re.search(r"Waiting\s+for\s+Android\s+connection\s+on\s+port\s+60001\.{0,3}", content):
            logging.info(f"Testing completed successfully for {container_name}.")
            log_detection(f"Testing completed successfully for {container_name}.")
            test_logging(f"Testing completed successfully for {container_name}.")   
            return True
        else:
            logging.error("Test did not complete successfully. Check logs for details.")
            log_detection("Testing failed for {container_name}.")
            test_logging("Testing failed for {container_name}.")
            return False



def stop_container(container_name):
    """Stop the podman container"""
    command = ["sudo", "podman", "stop", container_name]
    logging.info(f"Stopping container '{container_name}'...")
    log_detection(f"Stopping container '{container_name}'...")
    try:
        subprocess.run(command, check=False)
        logging.info(f"Container '{container_name}' stopped successfully.")
        log_detection(f"Container '{container_name}' stopped successfully.")
    except subprocess.CalledProcessError as e:  
        logging.error(f"Failed to stop container '{container_name}': {e}")
        log_detection(f"Failed to stop container '{container_name}': {e}")


# ----------------------------
# Main Execution Flow
# ----------------------------
def main():
    logging.info(f"Starting test for app: {app_name}")
    if dataset:
        logging.info(f"Using dataset: {dataset}")
    if base_dir:
        logging.info(f"Using base directory: {base_dir}")

    try:
        passed = run_container(app_name)
        if not passed:
            logging.error("Failed to start the container. Exiting.")
            return 
        print_container_output(container_name, duration)
        passed = verify_output(log_file_path)
    finally:
        if container_name:
            stop_container(container_name)
            logging.info(f"Container '{container_name}' has been stopped.")
        if passed:
            logging.info("Test completed successfully.")
            log_detection("Test completed successfully.")
            test_logging("Test completed successfully.")
            exit(0)
        else:
            logging.error("Test failed. Check logs for details.")
            log_detection("Test failed. Check logs for details.")
            test_logging("Test failed. Check logs for details.")
            exit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        logging.info("Process interrupted by user. Stopping container...")
        if container_name:
            stop_container(container_name)
