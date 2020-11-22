import struct, sys

"""
auto fp = fopen((string(path) + "/gsinit.bin").c_str(), "rb");
    auto read32 = [&]() -> uint32_t { uint32_t val; fread(&val, sizeof(uint32_t), 1, fp); return val; };
    auto read64 = [&]() -> uint64_t { uint32_t val; fread(&val, sizeof(uint64_t), 1, fp); return val; };
    auto readString = [&](auto len) -> const char* {
        auto fn = new char[len];
        fread(fn, len, 1, fp);
        return (const char*) fn;
    };
    init->fileCount = read32();
    init->files = new TargetFile[init->fileCount];
    for(auto i = 0; i < init->fileCount; ++i) {
        auto file = &init->files[i];
        auto fnlen = read32();
        auto clscount = read32();
        auto impcount = read32();
        auto expcount = read32();
        file->main = read64();
        file->fn = readString(fnlen);
        file->objcClasses = new uint64_t[clscount + 1];
        file->objcClasses[clscount] = 0;
        for(auto j = 0; j < clscount; ++j)
            file->objcClasses[j] = read64();
        file->imports = new Import[impcount + 1];
        file->imports[impcount].addr = 0;
        for(auto j = 0; j < impcount; ++j) {
            auto imp = &file->imports[j];
            imp->addr = read64();
            auto dlen = read32();
            auto nlen = read32();
            imp->dylib = readString(dlen);
            imp->symbol = readString(nlen);
        }
        file->exports = new Export[expcount + 1];
        file->exports[expcount].addr = 0;
        for(auto j = 0; j < expcount; ++j) {
            auto exp = &file->exports[j];
            exp->addr = read64();
            exp->symbol = readString(read32());
        }
    }
    ginit = init;"""

def main(fn):
	with file(fn, 'rb') as fp:
		read32 = lambda: struct.unpack('<I', fp.read(4))[0]
		read64 = lambda: struct.unpack('<Q', fp.read(8))[0]
		readString = lambda s: fp.read(s).split('\0', 1)[0]

		fileCount = read32()
		for i in xrange(fileCount):
			fnlen, clscount, impcount, expcount, sectcount = read32(), read32(), read32(), read32(), read32()
			main = read64()
			fn = readString(fnlen)
			objcClasses = [read64() for i in xrange(clscount)]
			imports = {}
			for i in xrange(impcount):
				addr = read64()
				dlen, nlen = read32(), read32()
				dylib = readString(dlen)
				symbol = readString(nlen)
				if dylib == '':
					dylib = '<unknown>'
				if dylib not in imports:
					imports[dylib] = {}
				if symbol not in imports[dylib]:
					imports[dylib][symbol] = []
				imports[dylib][symbol].append(addr)
			exports = [(read64(), readString(read32())) for i in xrange(expcount)]
			sections = []
			for i in xrange(sectcount):
				addr, size, glen, clen = read64(), read64(), read32(), read32()
				sections.append((addr, size, readString(glen), readString(clen)))
			print 'File %s:' % fn
			if main != 0:
				print '\tMain: 0x%x' % main
			for dylib, symbols in sorted(imports.items(), key=lambda x: x[0]):
				print '\tImports from %s:' % dylib
				for symbol, addrs in sorted(symbols.items(), key=lambda x: x[0]):
					print '\t\t%s -- %s' % (symbol, ', '.join('0x%x' % addr for addr in addrs))
			print '\tExports:'
			for addr, symbol in sorted(exports, key=lambda x: x[1]):
				print '\t\t%s -- 0x%x' % (symbol, addr)
			print '\tSections:'
			for addr, size, segname, sectname in sorted(sections, key=lambda x: x[0]):
				print '\t\t%s::%s -- 0x%x-0x%x' % (segname, sectname, addr, addr + size)
			print '\tObj-C Classes:'
			for addr in objcClasses:
				print '\t\t0x%x' % addr
			print

if __name__=='__main__':
	main(*sys.argv[1:])
