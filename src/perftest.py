import itertools
import random

import serial
import time
from tqdm import tqdm
import string


def sample_chars(n):
    return ''.join(random.choices(string.ascii_uppercase + string.digits, k=n))


def wait_flush(suppress=False, wait_for_input=True):
    start = time.time()
    if ser.in_waiting == 0 and wait_for_input:
        while ser.in_waiting == 0 and time.time()-start < 3:
            pass
    while ser.in_waiting > 0:
        if not suppress:
            print('\t{', ser.readline().decode('utf-8').strip(), '}')
        else:
            ser.readline()
            time.sleep(0.1)


def mk(name_size, data_size, is_dir=False):
    t0 = time.time()
    name = sample_chars(name_size)
    data = sample_chars(data_size)

    if not is_dir:
        ser.write(f'mkfile {name} >{data}\n'.encode('utf-8'))
    else:
        ser.write(f'mkdir {name}\n'.encode('utf-8'))

    wait_flush(suppress=True)

    return name, data, time.time()-t0


def rm(name):
    ser.write(f'rm {name}\n'.encode('utf-8'))
    wait_flush()


def read(name):
    ser.write(f'cat {name}\n'.encode('utf-8'))
    data = ser.readline().decode('utf-8').strip()

    return data


actual_iters = 0
total_time = 0
files = []
writecycles = 0
with serial.Serial('/dev/ttyUSB1', 2000000, timeout=1) as ser:
    ser.write(f'wipe\n'.encode('utf-8'))
    wait_flush()
    ser.write(f'mkfs 1024\n'.encode('utf-8'))
    wait_flush()
    ser.write(f'readfs\n'.encode('utf-8'))
    wait_flush()

    for i in itertools.count(0, 1):
        name, data, t = mk(random.randint(4, 8), random.randint(3, 45))
        files.append(name)
        data_hat = read(name)
        print(data == data_hat)
        if data != data_hat:
            for i in range(len(files)//2):
                print(data, data_hat, i)
                random.shuffle(files)
                rm(files.pop())
                print('\t forcepop')
                continue

        total_time += t
        actual_iters += 1

        if random.random() < 0.7:
            random.shuffle(files)
            rm(files.pop())

        if i % 10 == 0:
            print(actual_iters, total_time/actual_iters)
            ser.write('writecycles\n'.encode('utf-8'))
            try:
                writecycles = int(ser.readline().decode().strip())
                print('writecycles: ', writecycles, i)
                time.sleep(0.1)
                wait_flush(wait_for_input=False)
            except ValueError:
                ser.readline()
                pass
