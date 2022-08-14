include "stdlib/io.csh";
include "stdlib/std.csh";

proc fib(int n) {
	if(n <= 1)
		return n;
	return thisproc(n - 1) + thisproc(n - 2);
}

println(itos(fib(35)));