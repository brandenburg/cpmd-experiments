#!/usr/bin/env python

"""
A basic Python application shell, for copy&paste development.
"""

import optparse
import cmd
import sys

o = optparse.make_option

class App(cmd.Cmd):
    def __init__(self, opts=None, defaults=None, no_std_opts=False,
                 stdout=sys.stdout, stderr=sys.stderr, default_cmd=None):
        cmd.Cmd.__init__(self, None, stdout, stderr)
        self.default_cmd = default_cmd
        if not opts:
            opts = []
        if not defaults:
            defaults = {}
        defaults["_App_file"] = None
        self.f               = None
        if not no_std_opts:
            opts += [ o('-o', '--output', action='store', dest='_App_file',
                        help='store output in FILE', metavar='FILE')]
        (self.options, self.args) = self.__parse(opts, defaults)

    def __parse(self, opts, defaults):
        parser = optparse.OptionParser(option_list=opts)
        parser.set_defaults(**defaults)
        return parser.parse_args()

    def launch(self, args=None):
        if args:
            self.args = args
        try:
            if self.options._App_file:
                self.f  = open(self.options._App_file, 'w')
            self.onecmd(' '.join(self.args))
        except IOError, msg:
            self.err("I/O Error:", msg)
        except KeyboardInterrupt:
            self.err("Interrupted.")
        if self.f:
            self.f.close()

    def outfile(self):
        if self.f:
            return self.f
        else:
            return sys.stdout

    def emptyline(self):
        if self.default_cmd:
            self.onecmd(self.default_cmd)

    def default(self, line):
        self.err("%s: Command not recognized." % line)

    def do_dump_config(self, key):
        """Display the configuration as parsed on the console."""
        def is_private(k): return k[0] == '_'
        def show(k): print "%20s : %10s" % (k, str(self.options.__dict__[k]))
        if not key:
            for x in sorted(self.options.__dict__.keys()):
                if not is_private(x):
                    show(x)
        elif not is_private(key) and key in self.options.__dict__:
            show(key)
        else:
            self.err("%s: unknown option." % key)

    @staticmethod
    def __write(stream, *args, **kargs):
        stream.write(" ".join([str(a) for a in args]))
        if not ('omit_newline' in kargs and kargs['omit_newline']):
            stream.write("\n")
        stream.flush()

    def err(self, *args, **kargs):
        self.__write(sys.stderr, *args, **kargs)

    def msg(self, *args, **kargs):
        self.__write(sys.stdout, *args, **kargs)

    def out(self, *args, **kargs):
        if self.f:
            self.__write(self.f, *args, **kargs)
        else:
            self.__write(sys.stdout, *args, **kargs)

if __name__ == "__main__":
    a = App()
    a.launch()
