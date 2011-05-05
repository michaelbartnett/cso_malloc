# Define command for variable memory examination
# Parameters:
# $arg0 - The start address
# $arg1 - The number of bytes to examine
define examine_addr
set variable $hptr = $arg0
while $hptr <= ($arg0 + $arg1)
x/4wx $hptr
set variable $hptr = $_ + 1
end
end



###########################################################
#Define command for processing 7 writing heap trace to file
# Parameters:
# $arg0 - The heap start address
# $arg1 - The heap end address
###########################################################
define process_heaptrace
set logging off
shell rm .gdblog_heaptrace.txt
set logging file .gdblog_heaptrace.txt
set logging on
set variable $heapsize = $arg1 - $arg0
examine_addr $arg0 $heapsize
set logging off
shell ./gdbformat .gdblog_heaptrace.txt -o gdboutput.txt
end



define traceheap
process_heaptrace heap_start heap_end
end


##Load file, set breakpoints
file mtest

break mm_init
#commands
#finish
#echo We are at a breakpoint!!!
#continue
#end

break mm_malloc
#commands
#echo We are at a breakpoint!!!
#continue
#end

break debuggable_memset
#commands
#echo We are at a breakpoint!!!
#continue
#end

#shell rm gdboutput*

#run