Help("""
=============================================
Cache-related preemption migration delays

There are a number of user-configurable build
variables. These can either be set on the
command line (e.g., scons ARCH=x86) or read
from a local configuration file (.config).

Run 'scons --dump-config' to see the final
build configuration.

""")

import os
(ostype, _, _, _, arch) = os.uname()

# sanity check
if ostype != 'Linux':
    print 'Error: Building liblitmus is only supported on Linux.'
    Exit(1)


# #####################################################################
# Internal configuration.
DEBUG_FLAGS  = '-Wall -g -Wdeclaration-after-statement'
API_FLAGS    = '-D_XOPEN_SOURCE=600 -D_GNU_SOURCE'
X86_32_FLAGS = '-m32'
X86_64_FLAGS = '-m64'
V9_FLAGS     = '-mcpu=v9 -m64'

SUPPORTED_ARCHS = {
    'sparc64'	: V9_FLAGS,
    'x86'	: X86_32_FLAGS,
    'x86_64'	: X86_64_FLAGS,
}

ARCH_ALIAS = {
    'i686'      : 'x86'
}

# name of the directory that has the arch headers in the Linux source
INCLUDE_ARCH = {
    'sparc64'   : 'sparc',
    'x86'       : 'x86',
    'x86_64'    : 'x86',
}

INCLUDE_DIRS = [
    # cpmd headers
    'include/',
    # library headers
    '${LIBLITMUS}/include/',
    # Linux kernel headers
    '${LITMUS_KERNEL}/include/',
    # Linux architecture-specific kernel headers
    '${LITMUS_KERNEL}/arch/${INCLUDE_ARCH}/include',
    # Python headers
    '${PYTHON_HEADERS}'
    ]

# #####################################################################
# User configuration.

vars = Variables('.config', ARGUMENTS)

vars.AddVariables(
    PathVariable('LIBLITMUS',
                 'Where to find the LibLITMUS library source.',
                 '../liblitmus2010'),

    PathVariable('LITMUS_KERNEL',
                 'Where to find the LITMUS^RT kernel.',
                 '../litmus2010'),

    PathVariable('PYTHON_HEADERS',
                 'Where to find Python headers.',
                 '/usr/include/python2.5'),

    EnumVariable('ARCH',
                 'Target architecture.',
                 arch,
                 SUPPORTED_ARCHS.keys() + ARCH_ALIAS.keys()),

    ('WSS', 'Working set size for pm analysis', 3072),
)

AddOption('--dump-config',
          dest='dump',
          action='store_true',
          default=False,
          help="dump the build configuration and exit")

# #####################################################################
# Build configuration.

env  = Environment(variables = vars)

# Check what we are building for.
arch = env['ARCH']

# replace if the arch has an alternative name
if arch in ARCH_ALIAS:
    arch = ARCH_ALIAS[arch]
    env['ARCH'] = arch

# Get include directory for arch.
env['INCLUDE_ARCH'] = INCLUDE_ARCH[arch]

arch_flags = Split(SUPPORTED_ARCHS[arch])
dbg_flags  = Split(DEBUG_FLAGS)
api_flags  = Split(API_FLAGS)

# Set up environment
env.Replace(
    CC = 'gcc',
    CPPPATH = INCLUDE_DIRS,
    CCFLAGS = dbg_flags + api_flags + arch_flags,
    LINKFLAGS = arch_flags,
)

def dump_config(env):
    def dump(key):
        print "%15s = %s" % (key, env.subst("${%s}" % key))

    dump('ARCH')
    dump('LITMUS_KERNEL')
    dump('CPPPATH')
    dump('CCFLAGS')
    dump('LINKFLAGS')
    dump('PYTHON_HEADERS')

if GetOption('dump'):
    print "\n"
    print "Build Configuration:"
    dump_config(env)
    print "\n"
    Exit(0)

# #####################################################################
# Build checks.

def abort(msg, help=None):
    print "Error: %s" % env.subst(msg)
    print "-" * 80
    print "This is the build configuration in use:"
    dump_config(env)
    if help:
        print "-" * 80
        print env.subst(help)
    print "\n"
    Exit(1)

# Check compile environment
if not (env.GetOption('clean') or env.GetOption('help')):
    print env.subst('Building ${ARCH} binaries.')
    # Check for kernel headers.
    conf = Configure(env)

    conf.CheckCHeader('linux/unistd.h') or \
        abort("Cannot find kernel headers in '$LITMUS_KERNEL'",
              "Please ensure that LITMUS_KERNEL in .config is set to a valid path.")

    conf.CheckCHeader('litmus/rt_param.h') or \
        abort("Cannot find LITMUS^RT headers in '$LITMUS_KERNEL'",
              "Please ensure that the kernel in '$LITMUS_KERNEL'"
              " is a LITMUS^RT kernel.")

    conf.CheckCHeader('litmus.h') or \
        abort("Cannot find liblitmus headers in '$LIBLITMUS'",
              "Please ensure that LIBLITMUS in .config is a valid path'")

    conf.CheckCHeader('Python.h') or \
        abort("Cannot find Python headers in '$PYTHON_HEADERS'",
              "Please ensure that PYTHON_HEADERS in .config is set to a valid path.")

    env = conf.Finish()

# #####################################################################
# Derived environments

# link with liblitmus
rt = env.Clone(
    LIBS     = Split('litmus rt'),
    LIBPATH  = '${LIBLITMUS}'
)
rt.Append(LINKFLAGS = '-static')


# link with math lib
rtm = rt.Clone()
rtm.Append(LIBS = ['m'])

# preemption and migration overhead analysis
pmrt = rtm.Clone()
pmrt.Replace(CCFLAGS = '-Wall -O2')
pmrt.Append(CPPDEFINES = {'WSS' : '${WSS}'})

# Shared pm.so library for C2Python interaction
pmpy = pmrt.Clone()
pmpy.Replace(LINKFLAGS = '')

# #####################################################################
rt.Program('cache_cost', 'bin/cache_cost.c')

# #####################################################################
# Preemption and migration overhead analysis

pmrt.Program('pm_task', ['bin/pm_task.c', 'bin/pm_common.c'])
pmrt.Program('pm_polluter', ['bin/pm_polluter.c', 'bin/pm_common.c'])

pmpy.SharedLibrary('pm', ['c2python/pmmodule.c', 'bin/pm_common.c'])

Command("pm.so", "libpm.so", Move("$TARGET", "$SOURCE"))
# #####################################################################
# Additional Help

Help("Build Variables\n")
Help("---------------\n")
Help(vars.GenerateHelpText(env))

