int demu_i2c(unsigned long long *mem, unsigned long long *desc,
	     unsigned long long *used, unsigned long long *avail, unsigned long long *fd,
	     int call, int kick);
void call_guest(void);
