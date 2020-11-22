import lldb

def __lldb_init_module(debugger, internal_dict):
	for name in 'armdisassemble adisassemble adis adi'.split(' '):
		debugger.HandleCommand('command script add -f commands.armDis %s' % name)

def armDis(debugger, command, result, internal_dict):
	debugger.HandleCommand('disassemble -A arm64 ' + command)
