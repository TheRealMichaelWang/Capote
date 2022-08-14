global int zero = 0;

proc fib(int n) {
	if(n <= 1)
		return n / zero;
	return thisproc(n - 1) + thisproc(n - 2);
}

fib(35);