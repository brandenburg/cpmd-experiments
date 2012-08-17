from os import getenv, path, listdir, remove
from scipy.stats import scoreatpercentile
import random
import numpy
import subprocess
from cpmd_util import *
from cpmd_params import *

def obtain_traces():
    create_dir(TRACES_DIR)

    for wss in wss_values:
        for writecycle in writecycle_values:
            for (sleep_min, sleep_max) in sleep_values:
                output_name = 'pmo_host=%s_wss=%d_wcycle=%d_smin=%d_smax=%d.csv' % (host, wss, writecycle, sleep_min, sleep_max)
                cachecost_path = '%s -m%d -w%d -s%d -c%d -x%d -y%d -o %s' % (path.join(CPMD_DIR, 'cache_cost'), topo.cpus(), writecycle, wss, samples, sleep_min, sleep_max, path.join(TRACES_DIR, output_name))
                if path.exists(path.join(TRACES_DIR, output_name)):
                    print "Skipped: %s exists." % output_name
                else:
                    try:
                        bg_tasks = start_background_tasks(topo.cpus())

                        proc = subprocess.Popen(cachecost_path, shell=True, stdout=subprocess.PIPE)
                        proc.wait()

                        stop_background_tasks(bg_tasks)
                    except OSError as (msg):
                        raise OSError("Could not create trace '%s': %s" % (path.join(TRACES_DIR, output_name), msg))
                    print 'Completed %s.' % output_name

#
# group_traces()
# Group traces by type of migration
#
def group_traces():
    # topo is declared in cpmd_params
    types_of_migration = topo.migrationTypes()

    create_dir(COMPLETED_DIR)

    trace_files = organize_by_wss(TRACES_DIR)
    for wss in trace_files.keys():
        output_files = {}
        counter = {} # Number of samples in each output_file
        for migtype in types_of_migration:
            output_name = 'pmo_host=%s_type=%s_wss=%s.csv' % (host, migtype, wss)
            try:
                # Create a new file for each type of migration
                output_files[migtype] = open(path.join(COMPLETED_DIR, output_name), 'w')
                counter[migtype] = 0
            except IOError as (msg):
                raise IOError("Could not write output file '%s': %s" % (path.join(COMPLETED_DIR, output_name), msg))

        # Read trace files and separate lines in files according to the migration type 
        for trace_file in trace_files[wss]:
            try:
                f = open(path.join(TRACES_DIR, trace_file), 'r')

                line = f.readline()
                while line:
                    splitted_line = line.split(',')
                    splitted_line = [x.strip() for x in splitted_line]

                    source_cpu = int(splitted_line[4].strip())
                    dest_cpu = int(splitted_line[5].strip())
                    line_migtype = topo.migrationType(source_cpu, dest_cpu)

                    counter[line_migtype] += 1
                    splitted_line[0] = str(counter[line_migtype]) # Modify number of the line

                    output_line = '%6s, %3s, %6s, %6s, %3s, %3s, %8s, %8s, %8s, %8s, %8s\n' % tuple(splitted_line)

                    output_files[line_migtype].write(output_line) # Write in the file of this migration type

                    line = f.readline()
                f.close()
            except IOError as (msg):
                raise IOError("Could not read trace file '%s': %s" % (path.join(TRACES_DIR, trace_file), msg))

        for x in output_files.values():
            x.close()


def remove_outliers_and_create_model():
    create_dir(MODEL_DIR)

    trace_files = organize_filtered_files(COMPLETED_DIR)

    for migtype in trace_files.keys():
        fname = 'model_type=%s' % (migtype)
        outputfile = open(path.join(MODEL_DIR, fname), 'w')

        for wss in iter(sorted(trace_files[migtype].iterkeys())):
            try:
                f = open(path.join(COMPLETED_DIR, trace_files[migtype][wss]), 'r')

                seq = []

                line = f.readline()
                while line:
                    splitted_line = line.split(',')
                    splitted_line = [x.strip() for x in splitted_line]

                    cold = int(splitted_line[6])
                    hot1 = int(splitted_line[7])
                    hot2 = int(splitted_line[8])
                    hot3 = int(splitted_line[9])
                    post_pm = int(splitted_line[10])
                    min_hot = min(cold, hot1, hot2, hot3)
                    cpmd = post_pm - min_hot

                    seq.append(cpmd)
                    line = f.readline()
                f.close()
            except IOError as (msg):
                raise IOError("Could not read trace file '%s': %s" % (path.join(COMPLETED_DIR, trace_file), msg))

            seq.sort()

            # Remove outliers
            samples = len(seq)
            (seq, mincutoff, maxcutoff) = apply_iqr(seq, 1.5)
            filtered_samples = len(seq)

            output = {}

            output['type'] = migtype
            output['wss'] = wss
            output['number_of_samples'] = samples
            output['number_of_filtered_samples'] = filtered_samples
            output['maximum_overhead'] = cycles_to_ms(numpy.max(seq))
            output['average_overhead'] = cycles_to_ms(numpy.mean(seq))
            output['minimum_overhead'] = cycles_to_ms(numpy.min(seq))
            output['median_overhead'] = cycles_to_ms(numpy.median(seq))
            output['standard_deviation'] = cycles_to_ms(numpy.std(seq))
            output['variance'] = cycles_to_ms(cycles_to_ms(numpy.var(seq)))
            output['maximum_cutoff'] = cycles_to_ms(maxcutoff)
            output['minimum_cutoff'] = cycles_to_ms(mincutoff)

            outputfile.write('%s\t%d\t%d\t%d\t%.12e\t%.12e\t%.12e\t%.12e\t%.12e\t%.12e\t%.12e\t%.12e\n' % (output['type'],  int(output['wss']), output['number_of_samples'], output['number_of_filtered_samples'], output['maximum_overhead'], output['average_overhead'], output['minimum_overhead'], output['median_overhead'], output['standard_deviation'], output['variance'], output['maximum_cutoff'], output['minimum_cutoff']))

        outputfile.close()

if __name__ == '__main__':
    random.seed()

    obtain_traces()
    group_traces()
    remove_outliers_and_create_model()
