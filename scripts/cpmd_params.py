from cachetopology import *

# Parameters
CPMD_DIR = '..'

CLOCK = 2266.0 # MHz
host = 'litmus'

wss_values = [2**x for x in range(0,10)]
writecycle_values = [2,3,4,5]
sleep_values = [(0,1000)]
samples = 4

topo = CacheTopology()
