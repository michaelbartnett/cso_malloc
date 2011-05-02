def ALIGNW_ODD(size):
	return 3 if (size) < 3 else (size) + (((size) % 2) ^ 0x01)

def align_size(size):
	WSIZE = 4
	return (ALIGNW_ODD(((size) + WSIZE - 1)/WSIZE)) * WSIZE

def calc_min_bits(size):
	bits = 0;
	while (size >> bits) > 1:
		bits += 1

	return bits


def alignment_test():
	for i in range(1, 32):
		print(str(i) + " = " + str(align_size(i)))


if __name__ == '__main__':
	print 'Running alignment test . . .'
	alignment_test()
	print "Aligning 4072"
	print str(align_size(4072))
	print "Calc min bits:"
	print str(calc_min_bits(int(raw_input())))

