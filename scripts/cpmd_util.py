from os import getenv, path, listdir, remove, makedirs
from scipy.stats import scoreatpercentile
import re
import random
import bisect
import numpy
import subprocess
import xml.dom.minidom as minidom
from cpmd_params import *

RESULTS_DIR = path.join(CPMD_DIR, 'results')
TRACES_DIR = path.join(RESULTS_DIR, 'traces')
COMPLETED_DIR = path.join(RESULTS_DIR, 'completed')
FILTERED_DIR = path.join(RESULTS_DIR, 'filtered')
MODEL_DIR = path.join(RESULTS_DIR, 'model')
OVSET_DIR = path.join(RESULTS_DIR, 'ovset')

def decode(name):
    params = {}
    parts = re.split('_(?!RESCHED|LATENCY|TIMER)', name) # Fix for event names with underscore
    for p in parts:
        kv = p.split('=')
        k = kv[0]
        v = kv[1] if len(kv) > 1 else None
        params[k] = v
    return params

def get_config(fname):
    return path.splitext(path.basename(fname))[0]  

def organize_by_wss(path):
    trace_files = {}
    for tfile in listdir(path):
        params = decode(get_config(tfile))
        wss = params['wss']

         # The list hasn't been created yet
        if not trace_files.has_key(wss):
            trace_files[wss] = []

        trace_files[wss].append(tfile)
    return trace_files

def organize_filtered_files(path):
    trace_files = {}
    for tfile in listdir(path):
        params = decode(get_config(tfile))
        migtype = params['type']
        wss = params['wss']

        while len(wss) < 5:
            wss = '0' + wss # Complete with 0s

         # The list hasn't been created yet
        if not trace_files.has_key(migtype):
            trace_files[migtype] = {}

        trace_files[migtype][wss] = tfile
    return trace_files

def create_dir(dirpath):
    try:
        if not path.exists(dirpath):
            makedirs(dirpath)
    except OSError as (msg):
        raise OSError ("Could not create overhead directory: %s", (msg))

# Basic binary search operations:
# Based on http://docs.python.org/library/bisect.html
def find_lt(a, x):
    'Find rightmost value less than x'
    i = bisect.bisect_left(a, x)
    if i:
        return i-1
    else:
        return None

def find_gt(a, x):
    'Find leftmost value greater than x'
    i = bisect.bisect_right(a, x)
    if i != len(a):
        return i
    else:
        return None

def apply_iqr(seq, extent = 1.5):
    # Apply IQR filter
    # Parameter seq is an ordered list of values

    q1 =  scoreatpercentile(seq, 25)
    q3 =  scoreatpercentile(seq, 75)
    iqr = q3 - q1

    start = 0 
    end = len(seq) - 1
        
    l = find_lt(seq, q1 - extent*iqr) # Rightmost element to exclude
    if l is not None:
        start = l + 1

    r = find_gt(seq, q3 + extent*iqr) # Leftmost element to exclude
    if r is not None:
        end = r - 1

    seq = seq[start:end+1] # Apply filter

    return (seq, q1 - extent*iqr, q3 + extent*iqr) # Return seq, mincutoff, maxcutoff

def start_background_tasks(num_cpus):
    memthrash_path = path.join(CPMD_DIR, 'memthrash')
    try:
        return [subprocess.Popen(memthrash_path, shell=False, stdout=None) for x in range(num_cpus)]
    except OSError as (msg):
        raise OSError("Could not start background tasks (memthrash): %s" % (msg))

def stop_background_tasks(bg_tasks):
    for t in bg_tasks:
        t.terminate()
    for t in bg_tasks:
        t.wait()

def cycles_to_ms(c):
    return c / (CLOCK * 1000.0)
