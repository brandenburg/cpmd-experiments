#!/usr/bin/env python
"""
Usage: %prog [options] filename

FILENAME is where the .raw overhead data are. Filename
and the path to it also gives the base path and filename for the
files that contains already processed overheads and the directory
where to save the output data.
FILENAME should be something like: "res_plugin=GSN-EDF_wss=WSS_tss=TSS.raw".
Also, take a look at the "compact_results" script
"""

import defapp

from optparse import make_option as o
from os.path import splitext, basename, dirname

import sys
import numpy as np

# preemption and migration C data exchanger
import pm
import pmserialize as pms
import statanalyzer as pmstat

options = [
    o("-l", "--cores-per-l2", dest="coresL2", action="store", type="int",
        help="Number of cores per L2 cache; if all cores share the same \
L2 (i.e., no L3) set this to 0 (default = 2)"),
    o("-p", "--phys-cpu", dest="pcpu", action="store", type="int",
        help="Number of physical sockets on this machine (default 4)"),
    o(None, "--limit-preempt", dest="npreempt", action="store", type="int",
        help="Limit the number of preemption sample used in statistics \
to NPREEMPT"),
    o(None, "--limit-l2", dest="nl2cache", action="store", type="int",
        help="Limit the number of l2cache sample used in statistics \
to NL2CACHE"),
    o(None, "--limit-onchip", dest="nonchip", action="store", type="int",
        help="Limit the number of onchip sample used in statistics \
to NONCHIP"),
    o(None, "--limit-offchip", dest="noffchip", action="store", type="int",
        help="Limit the number of offchip sample used in statistics \
to NOFFCHIP"),
    o("-a", "--autocap", dest="autocap", action="store_true",
        help="Autodetect the minimum number of samples to use for statistics"),
    o("-r", "--read-valid-data", dest="read_valid", action="store_true",
        help="read already processed data from file"),
    o("-v", "--verbose", dest="verbose", action="store_true"),
    o("-d", "--debug", dest="debug", action="store_true"),
    o("-u", "--microsec", dest="cpufreq", action="store", type="float",
        help="Print overhead results in microseconds; \
CPUFREQ is the cpu freq in MHz (cat /proc/cpuinfo)"),
    ]
# this cores per chip parameter implies a different topology model not fully
# supported atm
#    o("-c", "--cores-per-chip", dest="coresC",
#                      action="store", type="int", default="6",
#            help="number of cores per chip (default = 6)")

defaults = {
        'coresL2'   : 2,
        'pcpu'      : 4,
        'npreempt'  : 0,
        'nl2cache'  : 0,
        'nonchip'   : 0,
        'noffchip'  : 0,
        'read_valid': False,
        'verbose'   : False,
        'debug'     : False,
        'cpufreq'   : 0,
        }

# from Bjoern's simple-gnuplot-wrapper
def decode(name):
    params = {}
    parts = name.split('_')
    for p in parts:
        kv = p.split('=')
        k = kv[0]
        v = kv[1] if len(kv) > 1 else None
        params[k] = v
    return params

class Overhead:
    def __init__(self):
        self.overheads = []
        self.index = 0

    def __iter__(self):
        return self

    def next(self):
        if self.index == len(self.overheads):
            self.index = 0
            raise StopIteration
        self.index += 1
        return self.overheads[self.index - 1]

    def add(self, ovd_vector, label):
        self.overheads.append([ovd_vector, label])

class Analyzer(defapp.App):
    def __init__(self):
        defapp.App.__init__(self, options, defaults, no_std_opts=True)
        self.last_conf = {}
        self.valid_ovds_list = {}
        self.min_sample_tss = {}
        self.lsamples = {}
        if self.options.npreempt:
            self.lsamples['preemption'] = self.options.npreempt
        if self.options.nl2cache:
            self.lsamples['l2cache'] = self.options.nl2cache
        if self.options.nonchip:
            self.lsamples['onchip'] = self.options.nonchip
        if self.options.noffchip:
            self.lsamples['offchip'] = self.options.noffchip

    # read previously saved overhead data
    def read_valid_data(self, filename):
        valid_ovds = Overhead()
        nf = filename + '_preemption.vbin'
        if self.options.debug:
            print "Reading '%s'" % nf
        valid_ovds.add(pms.unpickl_it(nf), 'preemtion')

        nf = filename + '_onchip.vbin'
        if self.options.debug:
            print "Reading '%s'" % nf
        valid_ovds.add(pms.unpickl_it(nf), 'onchip')

        nf = filename + '_offchip.vbin'
        if self.options.debug:
            print "Reading '%s'" % nf
        valid_ovds.add(pms.unpickl_it(nf), 'offchip')

        if self.options.coresL2 != 0:
            nf = filename + '_l2cache.vbin'
            if self.options.debug:
                print "Reading '%s'" % nf
            valid_ovds.add(pms.unpickl_it(nf), 'l2cache')
        return valid_ovds

    def process_raw_data(self, datafile, conf):
        coresL2 = self.options.coresL2
        pcpu = self.options.pcpu
        # initialize pmmodule
        pm.load(datafile, coresL2, pcpu, int(conf['wss']), int(conf['tss']))
        # raw overheads
        ovds = Overhead()
        # valid overheads
        valid_ovds = Overhead()
        # get overheads
        ovds.add(pm.getPreemption(), 'preemption')
        ovds.add(pm.getOnChipMigration(), 'onchip')
        ovds.add(pm.getOffChipMigration(), 'offchip')
        if coresL2 != 0:
            ovds.add(pm.getL2Migration(), 'l2cache')

        if self.options.debug:
            for i in ovds:
                print i[0], i[1]

        # instance the statistical analizer to remove outliers
        sd = pmstat.InterQuartileRange(25,75, True)

        for i in ovds:
            if len(i[0]) != 0:
                # just add overheads, "forget" preemption length
                # FIXME: is really needed?
                # valid_ovds.add(sd.remOutliers(i[0][:,0]), i[1])
                valid_ovds.add(i[0][:,0], i[1])
            else:
                print "Warning: no valid data collected..."
                valid_ovds.add([], i[1])

        if self.options.debug:
            # check outliers removals
            print "Before outliers removal"
            for i in ovds:
                print "samples(%(0)s) = %(1)d" % {"0":i[1], "1":len(i[0])}
            print "After outliers removal"
            for i in valid_ovds:
                print "samples(%(0)s) = %(1)d" % {"0":i[1], "1":len(i[0])}

        count_sample = {}
        if self.options.autocap or self.options.verbose:
            for i in valid_ovds:
                if self.options.verbose:
                    print "samples(%(0)s) = %(1)d" % {"0":i[1], "1":len(i[0])}
                count_sample[i[1]] = len(i[0])

            if self.options.autocap:
                if self.min_sample_tss == {}:
                    self.min_sample_tss = {
                            'preemption':count_sample['preemption'],
                            'onchip':count_sample['onchip'],
                            'offchip':count_sample['offchip'],
                            'l2cache':count_sample['l2cache']}
                else:
                    # it is normally sufficient to check num samples for
                    # preemptions to get tss with min num samples in wss
                    if self.min_sample_tss['preemption'] > \
                            count_sample['preemption']:
                        self.min_sample_tss = {
                            'preemption':count_sample['preemption'],
                            'onchip':count_sample['onchip'],
                            'offchip':count_sample['offchip'],
                            'l2cache':count_sample['l2cache']}

        # serialize valid overheads
        for i in valid_ovds:
            dname = dirname(datafile)
            fname, ext = splitext(basename(datafile))

            curf = dname + '/' + fname + '_' + i[1] + '.vbin'
            pms.pickl_it(i[0], curf)

        del ovds
        return valid_ovds

    # The output is one csv WSS file per ovhd type, "tss, max_ovd, avg_ovd"
    # Filename output format:
    # pm_wss=2048_ovd=preemption.csv
    # ovd: preemption, onchip, offchip, l2cache

    def analyze_data(self, dname, conf):
        csvbname = dname + '/pm_wss=' + conf['wss']

        for tss in sorted(self.valid_ovds_list.keys(), key=int):
            vohs = self.valid_ovds_list[tss]

            if self.options.verbose:
                print "\n(WSS = %(0)s, TSS = %(1)s)" % {"0":conf['wss'], \
                    "1":tss}

            for i in vohs:
                csvfname = csvbname + '_ovd=' + i[1] + '.csv'
                if self.options.debug:
                    print "Saving csv '%s'" % csvfname

                csvf = open(csvfname, 'a')
                csvlist = [tss]

                # data (valid_ovds already have only overheads, not length)
                # vector = i[0][:,0]
                #
                # Check if we need to limit the number of samples
                # that we use in the computation of max and avg.
                # Statistically, this is more sound than other choices
                if i[1] in self.lsamples:
                    if self.lsamples[i[1]] > 0:
                        nsamples = min(self.lsamples[i[1]], len(i[0]))
                        if self.options.verbose:
                            print "Computing %(0)s stat only on %(1)d samples" % \
                                {"0":i[1],
                                "1":nsamples}
                        vector = i[0][0:nsamples]
                elif self.options.autocap: # we can also autocompute the cap
                    nsamples = self.min_sample_tss[i[1]]
                    if self.options.verbose:
                        print "Computing %(0)s stat only on %(1)d samples" % \
                            {"0":i[1], "1":nsamples}
                    vector = i[0][0:nsamples]
                else:
                    vector = i[0]

                if vector != []:
                    # FIXME if after disabling prefetching there are
                    # still negative value, they shouldn't be considered
                    max_vec = np.max(vector)
                    avg_vec = np.average(vector)
                    std_vec = np.std(vector)
                else:
                    max_vec = 0
                    avg_vec = 0
                    std_vec = 0

                if self.options.cpufreq == 0:
                    max_vec_str = "%5.5f" % max_vec
                    avg_vec_str = "%5.5f" % avg_vec
                    std_vec_up = "%5.5f" % (avg_vec + std_vec)
                    std_vec_down = "%5.5f" % (avg_vec - std_vec)

                else:
                    max_vec_str = "%5.5f" % (max_vec / self.options.cpufreq)
                    avg_vec_str = "%5.5f" % (avg_vec / self.options.cpufreq)
                    std_vec_up = "%5.5f" % ((avg_vec + std_vec) / self.options.cpufreq)
                    std_vec_down = "%5.5f" % ((avg_vec - std_vec) / self.options.cpufreq)

                csvlist.append(max_vec_str)
                csvlist.append(avg_vec_str)
                csvlist.append(std_vec_down)
                csvlist.append(std_vec_up)
                pms.csv_it(csvf, csvlist)
                csvf.close()

                if self.options.verbose:
                    if self.options.cpufreq == 0:
                        print i[1] + " overheads (ticks)"
                        print "Max = %5.5f" % max_vec
                        print "Avg = %5.5f" % avg_vec
                        print "Std = %5.5f" % std_vec
                    else:
                        print i[1] + " overheads (us)"
                        print "Max = %5.5f" % (max_vec / self.options.cpufreq)
                        print "Avg = %5.5f" % (avg_vec / self.options.cpufreq)
                        print "Std = %5.5f" % (std_vec / self.options.cpufreq)

                del vector
            del vohs

    def process_datafile(self, datafile, dname, fname, conf):
        if self.options.verbose:
            print "\nProcessing: " + fname
        if self.options.read_valid:
            # .vbin output should be in same directory as input filename
            readf = dname + '/' + fname
            self.valid_ovds_list[conf['tss']] = self.read_valid_data(readf)
        else:
            self.valid_ovds_list[conf['tss']] = \
                    self.process_raw_data(datafile, conf)

    def default(self, _):
        # TODO: to support this combination we should store also the min
        # number of samples in the .vbin file
        if self.options.read_valid and self.options.autocap:
            self.err("Read stored values + autocap not currently supported")
            return None

        for datafile in self.args:
            dname = dirname(datafile)
            bname = basename(datafile)
            fname, ext = splitext(bname)
            if ext != '.raw':
                self.err("Warning: '%s' doesn't look like a .raw file"
                        % bname)

            conf = decode(fname)

            if datafile == self.args[-1]:
                # manage single file / last of list
                if ('wss' in self.last_conf) and (conf['wss'] != \
                        self.last_conf['wss']):
                    # we have already analyzed at least one file,
                    # this is the first file of a new set of WSS,
                    # and it is also the last file of the list
                    self.analyze_data(dname, self.last_conf)
                    # reinit dictionaries
                    del self.valid_ovds_list
                    del self.min_sample_tss
                    self.valid_ovds_list = {}
                    self.min_sample_tss = {}
                    # analyze this file
                    self.process_datafile(datafile, dname, fname, conf)
                    self.analyze_data(dname, conf)
                    del self.valid_ovds_list
                    del self.min_sample_tss
                else:
                    # just the end of a list of wss files or 1 single file
                    self.process_datafile(datafile, dname, fname, conf)
                    if self.args[0] == self.args[-1]:
                        self.analyze_data(dname, conf)
                    else:
                        self.analyze_data(dname, self.last_conf)
                    del self.valid_ovds_list
            else:
                # assume WSS are anayzed in order (all 1024s, all 256s, etc.)
                if ('wss' in self.last_conf) and (conf['wss'] != \
                        self.last_conf['wss']):
                    # we have already analyzed at least one file,
                    # this is the first file of a new set of WSS,
                    # analyze tss for previous wss
                    self.analyze_data(dname, self.last_conf)
                    # reinit dictionary
                    del self.valid_ovds_list
                    del self.min_sample_tss
                    self.valid_ovds_list = {}
                    self.min_sample_tss = {}

                # add tss to valid ovds list for this wss
                self.process_datafile(datafile, dname, fname, conf)
                # save previously analyzed configuration
                self.last_conf = conf

if __name__ == "__main__":
    Analyzer().launch()
