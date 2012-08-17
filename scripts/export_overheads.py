from os import path, listdir
import numpy
import pickle
from cpmd_util import *

def main():
    create_dir(OVSET_DIR)

    overheads = {}

    for fname in listdir(MODEL_DIR):
        params = decode(get_config(path.join(MODEL_DIR, fname)))
        migtype = params['type']

        if not overheads.has_key(migtype):
            overheads[migtype] = {}

        xvalues = []
        yvalues = []
        try:
            f = open(path.join(MODEL_DIR, fname), 'r')

            line = f.readline()
            while line:
                split_line = line.split()
                xvalues.append(int(split_line[1]))
                yvalues.append(float(split_line[4]))
                if len(xvalues) > 1: # Force the sequence to be monotonically increasing
                    if yvalues[-1] < yvalues[-2]:
                        yvalues[-1] = yvalues[-2]

                line = f.readline()
            f.close()
        except IOError as (msg):
            raise IOError("Could not read model file '%s': %s" % (fname, msg))

        overheads[migtype] = dict([(xvalues[i],yvalues[i]) for i in range(len(xvalues))])

    for (migtype, ov) in overheads.items():
        try:
            outputfile = open(path.join(OVSET_DIR, '%s.ovset' % migtype), 'w')
            pickle.dump(ov, outputfile)
        except IOError as (msg):
            raise IOError("Could not write cpmd overheadset file '%s': %s" % (fname, msg))

if __name__ == '__main__':
    main()
