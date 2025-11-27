#!/usr/bin/env python3

import queue
import sys
import serial
import time
import hashlib
import argparse
import os
import threading
import select

stop_threads = False

def stdin_reader(data_queue):
    """Read data from stdin and put it into the queue"""
    global stop_threads
    while not stop_threads:
        try:
            # Read data from stdin in chunks with a timeout
            data = sys.stdin.buffer.read(1)
            if not data:
                break
            data_queue.put(data)
        except Exception as e:
            print(f"Error reading from stdin: {e}")
            break
def send_data(ser, input_file, data_queue):
    """Send data to the serial port"""
    global stop_threads
    bytes_total = 0
    start_time = time.time()
    hash_md5 = hashlib.md5()
    f = open(input_file, "rb") if input_file else None

    while not stop_threads:
        if data_queue:
            try:
                data = data_queue.get(timeout=0.1)
            except queue.Empty:
                continue
        elif f:
            data = f.read(8192)
            if not data:
                break
        ser.write(data)
        hash_md5.update(data)
        bytes_total += len(data)

    time_cost = time.time() - start_time
    send_rate = bytes_total / time_cost if bytes_total > 0 else 0
    print(f"Send: bytes: {bytes_total}, time: {int(time_cost)} seconds, rate: {int(send_rate)} bytes/s, MD5: {hash_md5.hexdigest()}")
def receive_data(ser, output_file, data_queue):
    """Receive data from the serial port and save to a file"""
    global stop_threads
    bytes_total = 0
    start_time = 0
    end_time = 0
    hash_md5 = hashlib.md5()
    show_timeout = False
    f = open(output_file, "wb") if output_file else None
    is_stdout = not output_file and not data_queue
 
    while not stop_threads:
        data = ser.read(16 if is_stdout else 8192)
        if not data:
            if show_timeout:
                print("Timeout, no data received")
                show_timeout = False
            continue
        show_timeout = True
        f.write(data) if f else None
        if is_stdout:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        data_queue.put(data) if data_queue else None
        hash_md5.update(data)
        bytes_total += len(data)
        if not start_time:
            start_time = time.time()
        end_time = time.time()

    f.close() if output_file else None
    time_cost = end_time - start_time
    send_rate = bytes_total / time_cost if bytes_total > 0 else 0
    print(f"Recv: bytes: {bytes_total}, time: {int(time_cost)} seconds, rate: {int(send_rate)} bytes/s, MD5: {hash_md5.hexdigest()}")

def serial_transfer(port, input_file, output_file, loopback_mode, baudrate, rtscts):
    global stop_threads
    ser = None
    try:
        # Open the serial port
        ser = serial.Serial(port, baudrate=baudrate, timeout=1, rtscts=rtscts)
        print(f"Successfully opened serial port {port}, baudrate {baudrate}, hardware flow control {'enabled' if rtscts else 'disabled'}")

        data_queue = None
        if loopback_mode:
            print("Loopback mode enabled")
            data_queue = queue.Queue()
            output_file = None
            input_file = None
        else:
            if not input_file:
                print("No input file specified, sending data from stdin")
                data_queue = queue.Queue()
                stdin_thread = threading.Thread(target=stdin_reader, args=(data_queue,))
                stdin_thread.start()               
            if not output_file:
                print("No output file specified, saving received data to stdout")
            
        receive_thread = threading.Thread(target=receive_data, args=(ser, output_file, data_queue if loopback_mode else None))
        receive_thread.start()
        send_thread = threading.Thread(target=send_data, args=(ser, input_file, data_queue))
        send_thread.start()
        receive_thread.join()
        send_thread.join()    
    except serial.SerialException as e:
        print(f"Serial port error: {e}")
    except KeyboardInterrupt:
        print("Program terminated by user")
        stop_threads = True  
        receive_thread.join()
        send_thread.join()                
    finally:
        if ser and ser.is_open:
            print(f"Closed serial port {port}")
            ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Serial port file transfer program")
    parser.add_argument('-d', '--device', required=True, help='Serial port name')
    parser.add_argument('-i', '--input', help='File to send')
    parser.add_argument('-o', '--output', help='File to save received data')
    parser.add_argument('-s', '--speed', type=int, default=115200, help='Baud rate, default 9600')
    parser.add_argument('-c', '--hardware-flow-control', action='store_true', help='Enable hardware flow control')
    parser.add_argument('-l', '--loopback-mode', action='store_true', help='Enable loopback mode')
    args = parser.parse_args()

    # List of common baud rates
    common_baud_rates = [9600, 14400, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1500000, 2000000, 3000000, 4000000, 6000000]

    # Check if the baud rate is a common baud rate
    if args.speed not in common_baud_rates:
        print(f"Error: Baud rate {args.speed} is not a common baud rate. Please use one of the following: {common_baud_rates}")
        exit(1)

    serial_transfer(args.device, args.input, args.output, args.loopback_mode, args.speed, args.hardware_flow_control)
