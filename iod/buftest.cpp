#include <filtering.h>
#include <buffering.h>

using namespace std;

int main (int argc, char *argvp[]) {
		LongBuffer values(16);
		CircularBuffer *samples = createBuffer(16);
		long x;
		double tot = 0;
		long count = 0;
		long pos = 0;
		while (cin.good()) {
				pos += 20;
				cin >> x; ++count; values.append(x+pos); addSample(samples, count*20, x+pos);
				cout << count*20 << "\t" << values.average(16) << "\t" 
					<< bufferAverage(samples) << "\t" << rate(samples)*1000<< "\n";
		}
		for (int i=0; i<20; i++) { values.append(0); addSample(samples, i, 0); }
		assert(values.average(values.length()) == 0);
		assert(bufferAverage(samples) == 0);
		return 0;
};
