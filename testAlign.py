def ALIGNW_ODD(size):
	return 3 if (size) < 3 else (size) + (((size) % 2) ^ 0x01)

def align_size(size):
	WSIZE = 4
	return (ALIGNW_ODD(((size) + WSIZE - 1)/WSIZE)) * WSIZE

def calc_min_bits(size):
	bits = 0;
	while (size >> bits) > 0:
		bits += 1

	return bits


def alignment_test(bits):
	for i in range(1, 32):
		print(str(i) + " = " + str(align_size(i)))

def printbits(bits):
	print "Calc min bits(%u):" %(bits)
	print str(calc_min_bits(bits))

if __name__ == '__main__':
	# print 'Running alignment test . . .'
	# alignment_test()
	print "Aligning 56"
	print str(align_size(56))
	printbits(4048)
	printbits(4072)
	

