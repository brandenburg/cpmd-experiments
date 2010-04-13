#!/usr/bin/env python

import cPickle
import csv

# http://en.wikipedia.org/wiki/Picolit
def pickl_it(vector, filename):
    try:
        f = open(filename, 'w')
    except IOError:
        print "Cannot open " + filename
        raise
    else:
        cPickle.dump(vector, f)
        f.close()

def unpickl_it(filename):
    try:
        f = open(filename, 'r')
    except IOError:
        print "Cannot open " + filename
        raise
    else:
        vector = cPickle.load(f)
        f.close()
        return vector

# TODO function -> class for writing comments etc.
#      MyWriter: csv, comments etc
def csv_it(fstream, strlist):
    out = ''
    for i in strlist:
        out = out + i + ', '
    out = out[0:-2]
    out += '\n'
    fstream.write(out)

def uncsv_it(file, string):
    return None
