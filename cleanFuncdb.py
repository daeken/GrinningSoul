allSyms = {}

fn = None
for line in file('funcdb', 'r').read().strip().split('\n'):
	if not line:
		continue
	if line[0] != '\t':
		fn = line
		continue
	sym, encoding = line[1:].split(' = ', 1)
	if not encoding.startswith('~f{'):
		continue
	encoding = encoding[3:-1]
	if sym not in allSyms or len(allSyms[sym][0]) > len(fn):
		allSyms[sym] = fn, encoding

with file('funcdb2', 'w') as fp:
	for sym, (_, encoding) in sorted(allSyms.items(), key=lambda x: x[0]):
		print >>fp, '%s=%s' % (sym, encoding)
