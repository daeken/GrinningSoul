import struct, sys

MH_DYLIB = 0x6
MH_NO_REEXPORTED_DYLIBS = 0x100000

LC_SEGMENT_64 = 0x19
LC_REQ_DYLD = 0x80000000
LC_DYLD_INFO_ONLY = 0x22 | LC_REQ_DYLD
LC_ID_DYLIB = 0xD

REBASE_OPCODE_MASK =					0xF0
REBASE_IMMEDIATE_MASK =					0x0F
REBASE_OPCODE_DONE =					0x00
REBASE_OPCODE_SET_TYPE_IMM =				0x10
REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB =		0x20
REBASE_OPCODE_ADD_ADDR_ULEB =				0x30
REBASE_OPCODE_ADD_ADDR_IMM_SCALED =			0x40
REBASE_OPCODE_DO_REBASE_IMM_TIMES =			0x50
REBASE_OPCODE_DO_REBASE_ULEB_TIMES =			0x60
REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB =			0x70
REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB =	0x80

BIND_OPCODE_MASK =					0xF0
BIND_IMMEDIATE_MASK =					0x0F
BIND_OPCODE_DONE =					0x00
BIND_OPCODE_SET_DYLIB_ORDINAL_IMM =			0x10
BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB =			0x20
BIND_OPCODE_SET_DYLIB_SPECIAL_IMM =			0x30
BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM =		0x40
BIND_OPCODE_SET_TYPE_IMM =				0x50
BIND_OPCODE_SET_ADDEND_SLEB =				0x60
BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB =			0x70
BIND_OPCODE_ADD_ADDR_ULEB =				0x80
BIND_OPCODE_DO_BIND =					0x90
BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB =			0xA0
BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED =			0xB0
BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB =		0xC0

sizeof = struct.calcsize
machHeaderF = '<IIIIIIII'
loadCommandF = '<II'
segmentCommandF = '<II16sQQQQIIII'
dyldInfoCommandF = '<IIIIIIIIIIII'
dylibCommandF = '<IIIIII'

def rerebase(data, offset, size):
	def dec(i):
		return data[:i] + chr(ord(data[i]) - 1) + data[i + 1:]
	def skipUleb(i):
		while ord(data[i]) & 0x80:
			i += 1
		return i + 1
	i = offset
	while i < offset + size:
		opc = ord(data[i]) & REBASE_OPCODE_MASK
		if opc == REBASE_OPCODE_DONE:
			i += 1
		elif opc == REBASE_OPCODE_SET_TYPE_IMM:
			i += 1
		elif opc == REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			data = dec(i)
			i = skipUleb(i + 1)
		elif opc == REBASE_OPCODE_ADD_ADDR_ULEB:
			i = skipUleb(i + 1)
		elif opc == REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
			i += 1
		elif opc == REBASE_OPCODE_DO_REBASE_IMM_TIMES:
			i += 1
		elif opc == REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
			i = skipUleb(i + 1)
		elif opc == REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
			i = skipUleb(i + 1)
		elif opc == REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
			i = skipUleb(i + 1)
			i = skipUleb(i)
		else:
			print 'Unknown rebase opcode: 0x%02x' % opc
			sys.exit(1)
	return data

def rebind(data, offset, size):
	def dec(i):
		return data[:i] + chr(ord(data[i]) - 1) + data[i + 1:]
	def skipUleb(i):
		while ord(data[i]) & 0x80:
			i += 1
		return i + 1
	i = offset
	while i < offset + size:
		opc = ord(data[i]) & BIND_OPCODE_MASK
		if opc == BIND_OPCODE_DONE:
			i += 1
		elif opc == BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
			i += 1
		elif opc == BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
			i = skipUleb(i + 1)
		elif opc == BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
			i += 1
		elif opc == BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			i += 1
			while data[i] != '\0':
				i += 1
			i += 1
		elif opc == BIND_OPCODE_SET_TYPE_IMM:
			i += 1
		elif opc == BIND_OPCODE_SET_ADDEND_SLEB:
			i = skipUleb(i + 1)
		elif opc == BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			data = dec(i)
			i = skipUleb(i + 1)
		elif opc == BIND_OPCODE_ADD_ADDR_ULEB:
			i = skipUleb(i + 1)
		elif opc == BIND_OPCODE_DO_BIND:
			i += 1
		elif opc == BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
			i = skipUleb(i + 1)
		elif opc == BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
			i += 1
		elif opc == BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			i = skipUleb(i + 1)
			i = skipUleb(i)
		else:
			print 'Unknown bind opcode: 0x%02x' % opc
			sys.exit(1)
	return data

def dylibconv(iname, oname):
	fp = file(iname, 'rb')
	ofp = file(oname, 'wb')
	readStruct = lambda st: struct.unpack(st, fp.read(sizeof(st)))
	writeStruct = lambda st, *args: ofp.write(struct.pack(st, *args))
	magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = readStruct(machHeaderF)
	filetype = MH_DYLIB
	flags |= MH_NO_REEXPORTED_DYLIBS
	writeStruct(machHeaderF, magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved)

	offset = sizeof(machHeaderF)
	for i in xrange(ncmds):
		fp.seek(offset, 0)
		cmd, cmdsize = readStruct(loadCommandF)
		fp.seek(offset, 0)
		if cmd == 0x32:
			ofp.write(struct.pack('<III', cmd, cmdsize, 7))
			fp.read(12)
			ofp.write(fp.read(cmdsize - 12))
		else:
			ofp.write(fp.read(cmdsize))
		offset += cmdsize
	fp.seek(offset, 0)
	remainder = fp.read()
	ofp.write(remainder)

def dylibify(iname, oname):
	fp = file(iname, 'rb')
	ofp = file(oname, 'wb')
	readStruct = lambda st: struct.unpack(st, fp.read(sizeof(st)))
	writeStruct = lambda st, *args: ofp.write(struct.pack(st, *args))
	magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = readStruct(machHeaderF)
	filetype = MH_DYLIB
	flags |= MH_NO_REEXPORTED_DYLIBS
	writeStruct(machHeaderF, magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved)

	offset = sizeof(machHeaderF)
	rebaseOff = None

	for i in xrange(ncmds):
		fp.seek(offset, 0)
		cmd, cmdsize = readStruct(loadCommandF)
		fp.seek(offset, 0)
		if cmd == LC_SEGMENT_64:
			cmd, cmdsize, segname, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, flags = readStruct(segmentCommandF)
			if segname.rstrip('\0') == '__PAGEZERO':
				cmd = LC_ID_DYLIB
				writeStruct(dylibCommandF, LC_ID_DYLIB, cmdsize, sizeof(dylibCommandF), 0, 0, 0)
				dlname = 'foo.dylib'
				ofp.write(dlname + '\0' * (cmdsize - sizeof(dylibCommandF) - len(dlname)))
			else:
				writeStruct(segmentCommandF, cmd, cmdsize, segname, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, flags)
				ofp.write(fp.read(cmdsize - sizeof(segmentCommandF)))
		elif cmd == LC_DYLD_INFO_ONLY:
			cmd, cmdsize, rebaseOff, rebaseSize, bindOff, bindSize, weakBindOff, weakBindSize, lazyBindOff, lazyBindSize, exportOff, exportSize = readStruct(dyldInfoCommandF)
			fp.seek(offset, 0)
			ofp.write(fp.read(cmdsize))
		elif cmd == 0x32:
			ofp.write(struct.pack('<III', cmd, cmdsize, 7))
			fp.read(12)
			ofp.write(fp.read(cmdsize - 12))
		else:
			ofp.write(fp.read(cmdsize))
		offset += cmdsize
	fp.seek(offset, 0)
	remainder = fp.read()

	if rebaseOff is not None:
		if rebaseOff != 0:
			remainder = rerebase(remainder, rebaseOff - offset, rebaseSize)
		if bindOff != 0:
			remainder = rebind(remainder, bindOff - offset, bindSize)
		if weakBindOff != 0:
			remainder = rebind(remainder, weakBindOff - offset, weakBindSize)
		if lazyBindOff != 0:
			remainder = rebind(remainder, lazyBindOff - offset, lazyBindSize)

	ofp.write(remainder)

if __name__=='__main__':
	dylibify(*sys.argv[1:])
