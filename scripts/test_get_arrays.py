#!/usr/bin/python
#
# test_get_arrays: test program to verify and test C to Python pm interface
#
# recall: PYTHONPATH=".." python test_get_arrays.py :)
#
import sys, pm

args = sys.argv[1:]
if len(args) != 1:
    print "Filename required"
    sys.exit(-1)

pm.load(args[0],0,4)
x = pm.getPreemption()
y = pm.getOnChipMigration()
z = pm.getL2Migration()
w = pm.getOffChipMigration()
print "preemption: "
print x
print "samechip:"
print y
print "l2:"
print z
print "offchip"
print w
