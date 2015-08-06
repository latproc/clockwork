#include <iostream>
#include <errno.h>

using namespace std;

int main(int argc, const char *argv[]) {
	for (int i=1; i<argc; ++i) {
		int x;
		sscanf(argv[i], "%o", &x);
		if (errno == -1) perror("scanf"); else printf("%d\n", x);
	}
}