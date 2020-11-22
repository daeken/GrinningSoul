import fnmatch, os, subprocess
from multiprocessing import Pool
import tqdm

sdkPath = subprocess.check_output('xcodebuild -version -sdk iphonesimulator Path', shell=True).strip()

def parseSymbols(fn):
	args = [
		'headerparser_output/headerparse', fn, 
		'-ObjC', 
		'-fmodules', 
		'-isysroot', sdkPath, 
		'-I%s/usr/include' % sdkPath, 
		'-I%s/usr/include/libxml2' % sdkPath, 
		'-F%s/System/Library/Frameworks' % sdkPath, 
		'-I/usr/local/lib/clang/9.0.1/include', 
		'-DTARGET_OS_SIMULATOR'
	]
	if '.framework' in fn:
		args.append('-framework')
		args.append(fn.split('.framework', 1)[0].rsplit('/', 1)[1])
	
	symsByFile = {}
	try:
		output = subprocess.check_output(args, stderr=subprocess.STDOUT).strip().split('\n')
		if len(output) == 1 and output[0] == '':
			return fn, {}
		for line in output:
			line = line.strip()
			if not line:
				continue
			if line.startswith('~~~'):
				print line[3:]
				continue
			fn, sym, encoding = line.split(':::', 2)
			if fn not in symsByFile:
				symsByFile[fn] = {}
			symsByFile[fn][sym] = encoding
	except Exception, e:
		#import traceback
		#traceback.print_exc()
		pass
		#print
		#print ' '.join(map(repr, args))
		#print `e.output`
	return fn, symsByFile

allFns = []
for root, dirnames, filenames in os.walk(sdkPath): # + '/usr/include'):
	for filename in fnmatch.filter(filenames, '*.h'):
		allFns.append(os.path.join(root, filename))

pool = Pool(20)

allSymsByFn = {}
for fn, symbols in tqdm.tqdm(pool.imap_unordered(parseSymbols, allFns), total=len(allFns)):
	for dfn, syms in symbols.items():
		if dfn not in allSymsByFn:
			allSymsByFn[dfn] = {}
		allSymsByFn[dfn].update(syms)

with file('funcdb', 'w') as fp:
	for fn, syms in allSymsByFn.items():
		print >>fp, fn
		for name, encoding in sorted(syms.items(), key=lambda x: x[0]):
			print >>fp, '\t' + name, '=', encoding
