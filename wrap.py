import glob, os, shutil, struct, subprocess, sys, tempfile
from bplist import BPListReader
from plistlib import readPlist, writePlist, readPlistFromString, writePlistToString
from dylibify import dylibify, dylibconv

def main(iname, oname):
	iname, oname = iname.rstrip('/'), oname.rstrip('/')

	assert oname.endswith('.app')

	try:
		shutil.rmtree(oname)
	except:
		pass
	shutil.copytree(iname, oname)

	try:
		with file(iname + '/Info.plist', 'rb') as ipfp:
			reader = BPListReader(ipfp.read())
			data = reader.parse()
	except:
		with file(iname + '/Info.plist', 'rb') as ipfp:
			data = readPlist(ipfp)

	origbin = binfile = data['CFBundleExecutable']
	data['CFBundleExecutable'] = 'GSWrapper'
	data['CFBundleName'] += ' ARM'
	#data['CFBundleIdentifier'] += '.arm'
	writePlist(data, oname + '/Info.plist')

	swiftDylibs = []
	for fn in glob.glob('/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Library/Developer/CoreSimulator/Profiles/Runtimes/iOS.simruntime/Contents/Resources/RuntimeRoot/usr/lib/swift/*.dylib'):
		fn = fn.rsplit('/', 1)[1]
		swiftDylibs.append(fn)

	filenames = [binfile, 'libc--.1.dylib', 'libc--abi.1.dylib', 'libarmruntime.dylib']
	for fn in glob.glob(iname + '/Frameworks/*.dylib'):
		fn = fn.rsplit('/', 1)[1]
		if fn in swiftDylibs:
			continue
		filenames.append('Frameworks/' + fn)
	for fn in glob.glob(iname + '/Frameworks/*.framework'):
		name = fn.rsplit('/', 1)[1][:-10]
		filenames.append('Frameworks/' + name + '.framework/' + name)
		for fn in glob.glob(iname + '/Frameworks/' + name + '.framework/*.dylib'):
			print 'Found dylib in framework!', fn
			filenames.append(fn)
	allFiles = []
	for binfile in filenames:
		if binfile == 'libc--.1.dylib' or binfile == 'libc--abi.1.dylib' or binfile == 'libarmruntime.dylib':
			ifile = binfile
		else:
			ifile = iname + '/' + binfile
		ofb = file(ifile, 'rb').read()
		if ofb[4:8] == '\x07\x00\x00\x01':
			print binfile, 'is already x86-64; hopefully this is a test binary!'
			continue
		if ofb.startswith('\xca\xfe\xba\xbe'):
			print 'FAT binary!'
			subprocess.check_call(['lipo', ifile, '-thin', 'arm64', '-output', 'thintemp.bin'])
			ifile = 'thintemp.bin'
			ofb = file('thintemp.bin', 'rb').read()
		with file('machtemp.bin', 'wb') as fp:
			fp.write(struct.pack('<IIII', 0xfeedfacf, 0x01000007, 3, 6 if binfile != origbin else 2))
			fp.write(ofb[0x10:0x1C])
			fp.write(chr(len(allFiles)) + '\xbe\xad\xde')
			contents = ofb[0x20:]
			contents = contents.replace('libc++.', 'libc--.')
			contents = contents.replace('libc++abi.', 'libc--abi.')
			if binfile != 'libc--.1.dylib':
				contents = contents.replace('/usr/lib/libc--.1.dylib', '@rpath/libc--.1.dylib' + '\0' * 2)
				contents = contents.replace('/usr/lib/libc--.dylib', '@rpath/libc--.1.dylib' + '\0' * 0)
			if binfile != 'libc--abi.1.dylib':
				contents = contents.replace('/usr/lib/libc--abi.1.dylib', '@rpath/libc--abi.1.dylib' + '\0' * 2)
				contents = contents.replace('/usr/lib/libc--abi.dylib', '@rpath/libc--abi.1.dylib' + '\0' * 0)
			contents = contents.replace('__FE_DFL_DISABLE_DENORMS_ENV', '_pthread_attr_setdetachstate')
			#contents = contents.replace('/usr/lib/swift', '/foo/bar/bazzz')
			fp.write(contents)
		if binfile == origbin:
			dylibify('machtemp.bin', oname + '/' + binfile + '.dylib')
		else:
			dylibconv('machtemp.bin', oname + '/' + binfile)

		subprocess.check_call(['codesign', '--remove-signature', oname + '/' + binfile + ('' if binfile != origbin else '.dylib')])

		print ifile
		lines = subprocess.check_output(['converter_output/converter', ifile]).strip().split('\n')
		fn = oname + '/' + binfile + ('' if binfile != origbin else '.dylib')
		contents = file(fn, 'rb').read()
		with file(fn, 'wb') as fp:
			fp.write(contents.replace('___chkstk_darwin', 'dyld_stub_binder'))
		base = main = None
		classes = []
		imports = []
		exports = []
		sections = []
		for line in lines:
			if line.startswith('base:'):
				base = int(line.split(':', 1)[1], 16)
			elif line.startswith('entrypoint:'):
				main = int(line.split(':', 1)[1], 16)
			elif line.startswith('objcClass:'):
				classes.append(int(line.split(':', 1)[1], 16))
			elif line.startswith('import:'):
				off, dylib, name = line.split(':', 1)[1].split(';')
				if name == '___CFConstantStringClassReference' or name == '_OBJC_CLASS_$_NSObject':
					continue
				imports.append((int(off, 16), dylib, name))
			elif line.startswith('export:'):
				off, name = line.split(':', 1)[1].split(';')
				exports.append((int(off, 16), name))
			elif line.startswith('segment:'):
				curSegment = line.split(':', 1)[1]
			elif line.startswith('section:'):
				name, off, size = line.split(':', 1)[1].split(';')
				sections.append((curSegment, name, int(off, 16), int(size, 16)))

		allFiles.append((binfile, main, classes, imports, exports, sections))

	with file(oname + '/gsinit.bin', 'wb') as fp:
		fp.write(struct.pack('<I', len(allFiles)))
		for binfile, main, classes, imports, exports, sections in allFiles:
			binfile = binfile + ('' if binfile != origbin else '.dylib') + '\0'
			fp.write(struct.pack('<IIIIIQ', len(binfile), len(classes), len(imports), len(exports), len(sections), main))
			fp.write(binfile)

			for addr in classes:
				fp.write(struct.pack('<Q', addr))
			for addr, dylib, name in imports:
				fp.write(struct.pack('<QII', addr, len(dylib) + 1, len(name) + 1))
				fp.write(dylib + '\0')
				fp.write(name + '\0')
			for addr, name in exports:
				fp.write(struct.pack('<QI', addr, len(name) + 1))
				fp.write(name + '\0')
			for segname, sectname, addr, size in sections:
				fp.write(struct.pack('<QQII', addr, size, len(segname) + 1, len(sectname) + 1))
				fp.write(segname + '\0')
				fp.write(sectname + '\0')

	print 'Building wrapper'

	wfn = tempfile.NamedTemporaryFile(suffix='.mm').name
	fpath = os.path.dirname(os.path.realpath(__file__))
	with file(wfn, 'w') as fp:
		fp.write(file('wrapTemplate.mm', 'r').read().replace('%PATH%', fpath))

	subprocess.check_call([
		'/usr/bin/clang++', '--std=c++2a', '-o', oname + '/GSWrapper', '-Wl,-image_base,0x500000000', '-fno-pic',
		'-mios-simulator-version-min=10.0', #'-fsanitize=address',
		'-isysroot', '/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator13.2.sdk',
		#'-I/Users/daeken/Downloads/libcxx-9.0.1.src/include',
		'-framework', 'Foundation',
		wfn])
	subprocess.check_call([
		'install_name_tool', '-add_rpath', '@executable_path/Frameworks', oname + '/GSWrapper'
	])
	subprocess.check_call([
		'install_name_tool', '-add_rpath', '@executable_path/', oname + '/GSWrapper'
	])

	with file(os.devnull, 'w') as devnull:
		epl = subprocess.check_output(['codesign', '-d', '--entitlements', ':-', iname], stderr=devnull)
		epl = readPlistFromString(epl)
		npl = {
			'com.apple.security.app-sandbox' : False,
			'com.apple.security.cs.allow-dyld-environment-variables' : True,
			'com.apple.security.cs.allow-jit' : True,
			'com.apple.security.cs.allow-unsigned-executable-memory' : True,
			'com.apple.security.cs.debugger' : True,
			'com.apple.security.cs.disable-executable-page-protection' : True,
			'com.apple.security.cs.disable-library-validation' : True,
			'com.apple.security.device.audio-input' : True,
			'com.apple.security.files.user-selected.read-only' : True,
			'com.apple.security.get-task-allow' : True,
			'com.apple.security.network.client' : True,
			'com.apple.security.network.server' : True,
		}
		if 'application-identifier' in epl:
			npl['com.apple.application-identifier'] = npl['application-identifier'] = epl['application-identifier']
		for sub in ('com.apple.developer.team-identifier', 'keychain-access-groups'):
			if sub in epl:
				npl[sub] = epl[sub]
		xfn = tempfile.NamedTemporaryFile(suffix='.xml').name
		with file(xfn, 'w') as fp:
			xx = writePlistToString(npl)
			fp.write(xx.replace('</dict>', '<key>com.apple.security.get-task-allow</key><true/></dict>'))
		print xfn
		subprocess.check_call(['codesign', '--entitlements', xfn, '-f', '-s', '-', oname], stderr=devnull)

if __name__=='__main__':
	main(*sys.argv[1:])
