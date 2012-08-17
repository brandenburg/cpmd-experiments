import os
import multiprocessing
import subprocess
import pickle
     
class CacheTopology:
    """CacheTopology(): Access /sys files to collect cache
       topology and return the it as a new CacheTopology object.
       CacheTopology(topology_file): Same as above, but collects
       cache topology from a saved file."""
    def __init__(self, topology_file = None):
        if topology_file == None:
            self._collectTopology()
        else:
            self._collectTopologyFromFile(topology_file)
        self._createMigrationTable()
     
    def cpus(self):
        """CacheTopology.cpus(): Return the number of cpus."""
        return self._cpus
    
    def saveTopology(self, topology_file):
        """CacheTopology.saveTopology(topology_file): Saves cache topology object to a file."""
        try:
            f = open(topology_file, 'w')
            pickle.dump(self._cache_topology, f, 0)
        except IOError as (msg):
            raise IOError("Could not write topology file '%s': %s" % topology_file, msg)
     
    def migrationType(self, source_cpu, dest_cpu):
        """CacheTopology.migrationType(source_cpu, dest_cpu): Returns
           the memory level associated with the migration from
           source_cpu to dest_cpu. For example:
           (0,1) -> 'L3', (0,0) -> 'PREEMPTION'."""
     
        if source_cpu == dest_cpu:
            return 'PREEMPTION'
        else:
            for cache_index in range(len(self._cache_topology[source_cpu])): # For each cache of source_cpu
                if self._inCpuList(dest_cpu, self._cache_topology[source_cpu][cache_index]['shared_cpu_list']):
                    return 'L%s' % self._cache_topology[source_cpu][cache_index]['level']
    
            return 'MEMORY' # The processors don't communicate via cache, they use memory
     
    def migrationTable(self):
        """CacheTopology.migrationTable(): Return a processor->cache->other_processors correspondence in the following format:
           migration_table[0] = {'L1': [], 'L2': [], 'L3': [2,3]}"""
        return self._migration_table
           
    def migrationTypes(self):
        """CacheTopology.migrationTypes(): Returns a list with the types of migration of this architecture (minimal set)
           Assumes every processor can do to all types of migration"""
        return [x for x in self._migration_table[0].keys() if len(self._migration_table[0][x]) > 0]
     
    def _createMigrationTable(self):
        # Create migration table
        self._migration_table = [{} for x in range(self._cpus)]
     
        for cpu in range(self._cpus): # For each cpu
     
            remaining_cpus = [x for x in range(self._cpus) if x != cpu] # Cpus that don't share any cache with this one
     
            for cache_index in range(len(self._cache_topology[cpu])): # For each cache of this cpu
                if self._cache_topology[cpu][cache_index]['type'] not in ['Instruction', 'Unknown']: # Ignore instruction and unknown caches
     
                    cache_name = 'L' + str(self._cache_topology[cpu][cache_index]['level'])
                    self._migration_table[cpu][cache_name] = []
     
                    # Find other cpus that share this cache
                    for other_cpu in [x for x in range(self._cpus) if x != cpu]:
                        if other_cpu in remaining_cpus and self._inCpuList(other_cpu, self._cache_topology[cpu][cache_index]['shared_cpu_list']): # other_cpu shares this cache
                             self._migration_table[cpu][cache_name].append(other_cpu)
                             remaining_cpus.remove(other_cpu)
     
            # Cpus that don't share caches communicate via memory
            self._migration_table[cpu]['MEMORY'] = []
            for other_cpu in remaining_cpus:
                self._migration_table[cpu]['MEMORY'].append(other_cpu)
     
            self._migration_table[cpu]['PREEMPTION'] = [cpu] # Add PREEMPTION
     
    def _collectTopology(self):
        self._cpus = multiprocessing.cpu_count()
        self._cache_topology = [[] for x in range(self._cpus)] # _cache_topology[cpu][cache_index][attribute]
     
        # Collect cache data
        try:
            for cpu in range(self._cpus):
                for cache_dir in os.listdir('/sys/devices/system/cpu/cpu%d/cache/' % (cpu)):
                    proc = subprocess.Popen('grep . /sys/devices/system/cpu/cpu%d/cache/%s/*' % (cpu, cache_dir), shell=True, stdout=subprocess.PIPE)
                    lines = proc.communicate()[0].splitlines()
     
                    data = {}
                    for line in lines:
                        (attr_temp, value) = line.split(':')
                        attr = attr_temp.split('/')[-1]
                        data[attr] = value
    
                    self._cache_topology[cpu].append(data)
        except Exception as (msg):
            raise Exception(msg)
     
    def _collectTopologyFromFile(self, topology_file):
        try:
            f = open(topology_file, 'r')
            self._cache_topology = pickle.load(f)
            f.close()
        except IOError as (msg):
            raise IOError("Could not read topology file '%s': %s" % topology_file, msg)
     
        self._cpus = len(self._cache_topology)
     
    def _inCpuList(self, cpu, cpu_list):
        # cpu_list example: 1,2-4,8,10
        # This function assumes the list is well-formed
     
        elements = []
     
        splitted = cpu_list.split(',')
        for x in splitted:       
            if '-' not in x:
                elements.append(int(x.strip()))
            else:
                temp = x.split('-')
                elements += range(int(temp[0].strip()), int(temp[1].strip()) + 1)

        return cpu in elements
