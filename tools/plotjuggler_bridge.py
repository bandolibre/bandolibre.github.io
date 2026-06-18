"""Sends live metrics to plotjuggler server via UDP.

Read live metrics from stdin as CSV.
Headers can be repeated periodically to allow catching up on a spending stream.

Usage:
    st-trace --clock=96m | python3 tools/plotjuggler_bridge.py
Or
    just plotjuggler_bridge
"""

import sys
import csv
import socket
import json
import time

# Destination of network UPD frames
UDP_IP = "127.0.0.1"
UDP_PORT = 9870

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def is_header_row(row):
    """True for periodically emited headers."""
    try:
        # Si le premier élément (hors temps vide) ne peut pas être converti en float, c'est un en-tête
        float(row[0])
        return False
    except ValueError:
        return True

def parse_csv_stream():
    reader = csv.reader(sys.stdin)
    headers = None
    row_count, sample_count = 0, 0
    start = time.time()

    for row in reader:
        now = time.time()
        delta = now - start
        if delta > 2:
            print(f"Revieved {row_count/delta:5.1f} rows/sec  {sample_count/delta:5.1f} sample/sec")
            start = now
            row_count, sample_count = 0, 0
        if not row:
            continue

        row_count += 1

        if is_header_row(row):
            new_headers = [h.strip() for h in row]
            if new_headers != headers:
                print(f"Header recieved {new_headers}")
            headers = [h.strip() for h in row]
            continue

        if headers is None:
            print("Sample dropped, awaiting for header")
            continue

        if len(headers) != len(row):
            print("Sample dropped, row and header mismatch")
            continue


        try:
            data_dict = {k: float(v) for k, v in zip(headers, row)}
        except ValueError as e:
            print(f"Sample dropped, {e}")
            continue


        # Convert integer ms to float s
        data_dict["timestamp"] /= 1000.0

        sample_count += 1

        # Push UDP.
        try:
            payload = json.dumps(data_dict).encode('utf-8')
            sock.sendto(payload, (UDP_IP, UDP_PORT))
        except Exception as e:
            print(f"Erreur d'envoi: {e}", file=sys.stderr)

if __name__ == "__main__":
    try:
        print(f"Streaming vers PlotJuggler sur {UDP_IP}:{UDP_PORT}...", file=sys.stderr)
        parse_csv_stream()
    except KeyboardInterrupt:
        sys.exit(0)