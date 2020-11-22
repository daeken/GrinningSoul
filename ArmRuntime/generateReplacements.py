import glob

with file('../Runtime/replacements.generated.h', 'w') as fp:
    nfuncs = []
    sels = []
    for fn in glob.glob('*.cpp') + glob.glob('*.m') + glob.glob('*.mm') + glob.glob('*.h'):
        source = file(fn, 'r').read().split('\n')
        for line in source:
            line = line.strip()
            if not line.startswith('/// REPLACE'):
                continue
            args = line[11:].strip().split(' ')
            if len(args) == 1:
                if ':' not in args[0]:
                    nfuncs.append(args[0])
                else:
                    sels.append((args[0], args[0].replace(':', '_')))
            else:
                print 'Unhandled replacement:', args
    print >>fp, 'vector<tuple<string, string>> armReplacements = {'
    for func in nfuncs:
        print >>fp, '{ "_%s", "replace_%s" }, ' % (func, func)
    print >>fp, '};'
    print >>fp, 'vector<tuple<const char*, const char*>> armSelReplacements = {'
    for sel, func in sels:
        print >>fp, '{ "%s", "replace_%s" }, ' % (sel, func)
    print >>fp, '};'
