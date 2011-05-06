###########################################################
#
# generate_heap_traces.gdb
#
# Defines GDB commands that interface with the gdbformat
# script in order to do automatic pretty-printed heap
# traces while running the CSO malloc lab.
#
# by Michael Bartnett and Nabil hassein
#
###########################################################



###########################################################
# traceinfo
# 
# Prints the current trace
#
# Assumes there is a heap_start and heap_end variable
###########################################################
define traceinfo
	printf "Current trace file: %s\n", current_trace_name
	printf "\tOperation index: %d\n", traceop_index
	printf "\tBlock pointer id: %d\n", traceop_ptr
	output trace_operations[traceop_index]
	echo \n
end



###########################################################
# examine_addr
#
# Command for variable memory examination
#
# Parameters:
# $arg0 - The start address
# $arg1 - The number of bytes to examine
###########################################################
define examine_addr
	set variable $hptr = $arg0
	while $hptr <= ($arg0 + $arg1)
		x/4wx $hptr
		set variable $hptr = $_ + 1
	end
end



###########################################################
# process_heaptrace
#
# Command for processing 7 writing heap trace to file
#
# Parameters:
# $arg0 - The heap start address
# $arg1 - The heap end address
###########################################################
define process_heaptrace
	set logging off

	echo process_heaptrace running\n
	echo Logging will be disabled after this command is run.\n

	echo Examining address range $arg0 to $arg1 . . . 
	set logging overwrite on
	set logging file .gdblog_heaptrace.log
	set logging redirect on
	set logging on
	set variable $heapsize = $arg1 - $arg0
	examine_addr $arg0 $heapsize

	set logging off
	set logging redirect off
	set logging overwrite off

	echo Done.\n

	echo Running pretty print python script . . . 
	shell ./gdbformat .gdblog_heaptrace.log -o gdboutput.txt
	echo Done.\n
end



###########################################################
# traceheap
# 
# Command to drive process_heaptrace
#
# Assumes there is a heap_start and heap_end variable
###########################################################
define traceheap
	process_heaptrace heap_start heap_end
end