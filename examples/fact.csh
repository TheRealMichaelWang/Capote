$include "stdlib/io.csh";
$include "stdlib/std.csh";

proc fact(int n) {
	if(n == 0)
		return 1;
	return n * thisproc(n - 1);
}

fact(100);