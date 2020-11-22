import fnmatch, json, os, subprocess
from multiprocessing import Pool
import tqdm

sdkPath = subprocess.check_output('xcodebuild -version -sdk iphonesimulator Path', shell=True).strip()

def reframe(data):
	if isinstance(data, dict):
		if 'Str' in data:
			return data['Str']
		elif 'IType' in data:
			if data['IType'] == 'trivia':
				return None
			val = {'!type' : data['IType']}
			if 'Prop' in data:
				val.update({k : reframe(v) for k, v in data['Prop'].items()})
			return {k : v for k, v in val.items() if v is not None}
		elif 'List' in data:
			return [x for x in [reframe(x) for x in data['List']] if x is not None]
		else:
			print data
			assert False
	assert False

unhandled = set()
class Walker(object):
	def __init__(self):
		self.parents = []

	@property
	def parent(self):
		return self.parents[-1]

	def walk(self, node):
		if not isinstance(node, dict):
			print node
		assert isinstance(node, dict)
		type = node['!type']
		if not hasattr(self, type):
			if type not in unhandled:
				print 'Unhandled node type:', type
				print 'With members:', ', '.join(k for k in node if k != '!type')
				unhandled.add(type)
			return
		func = getattr(self, type)
		co = func.func_code
		hasStar = (co.co_flags & 4) == 4
		argCount = co.co_argcount - 1
		mandatory = argCount - len(func.func_defaults if func.func_defaults else [])
		optional = func.func_defaults
		argNames = co.co_varnames[1:argCount+1]
		args = []
		for i, name in enumerate(argNames):
			if name in node:
				args.append(node[name])
			elif i >= mandatory:
				args.append(optional[i - mandatory])
			else:
				print 'Mandatory argument for %r missing: %r' % (type, name)
				assert False
		if hasStar and '__children__' in node:
			args += node['__children__']
		return func(*args)

	def walkAll(self, lst):
		value = None
		for elem in lst:
			value = self.walk(elem)
		return value

	def SourceFileSyntax(self, statements):
		#print 'Got SourceFileSyntax!'
		self.walk(statements)

	def CodeBlockItemListSyntax(self, *statements):
		#print 'Got CodeBlockItemListSyntax'
		self.walkAll(statements)

	def CodeBlockItemSyntax(self, item):
		self.walk(item)

	def ImportDeclSyntax(self, path):
		print 'Import:', `path`

	def ExtensionDeclSyntax(self, extendedType, inheritanceClause, members, genericWhereClause):
		pass

def parseSymbols(fn):
	args = ['SwiftHeaderParser/build/Debug/SwiftHeaderParser', fn]
	
	try:
		output = subprocess.check_output(args, stderr=subprocess.STDOUT)
		data = json.loads(output)
	except Exception, e:
		import traceback
		traceback.print_exc()
		return None, {}
	data = reframe(data)
	json.dump(data, file('/Users/daeken/projects/swiftmodules_json/%s.json' % fn.split('/')[-2].rsplit('.', 1)[0], 'w'), indent=2, sort_keys=True)
	walker = Walker()
	walker.walk(data)
	return None, {}

allFns = []
for root, dirnames, filenames in os.walk(sdkPath):
	for filename in fnmatch.filter(filenames, 'x86_64.swiftinterface'):
		allFns.append(os.path.join(root, filename))

#pool = Pool(1)

allSymsByFn = {}
#for fn, symbols in tqdm.tqdm(pool.imap_unordered(parseSymbols, allFns), total=len(allFns)):
for fn in allFns: #tqdm.tqdm(allFns):
	fn, symbols = parseSymbols(fn)

	for dfn, syms in symbols.items():
		if dfn not in allSymsByFn:
			allSymsByFn[dfn] = {}
		allSymsByFn[dfn].update(syms)

with file('swiftfuncdb', 'w') as fp:
	for fn, syms in allSymsByFn.items():
		print >>fp, fn
		for name, encoding in sorted(syms.items(), key=lambda x: x[0]):
			print >>fp, '\t' + name, '=', encoding
