#ifndef __buffer_monitor_h__
#define __buffer_monitor_h__

#include <string>
#include "monitor.h"

/* Modbus interface */
template<class T> class BufferMonitor {
public:
	std::string name;
	T *last_data;
	T *cmp_data;
	T *dbg_mask;
	size_t buflen;
	int max_read_len; // maximum number of reads into this buffer
	bool initial_read;
    const Options &options;
	
	BufferMonitor(const char *buffer_name, const Options &global_options) 
      : name(buffer_name), last_data(0), cmp_data(0), dbg_mask(0), buflen(0), 
        max_read_len(0), initial_read(true), options(global_options) {}
    ~BufferMonitor() {}

	void refresh() { initial_read = true;}

    void setMaskBits(int start, int num) {
        if (!dbg_mask) return;
        //std::cout << "masking: " << start << " to " << (start+num-1) << "\n";
        int i = start;
        while (i<start+num && i+start < buflen) {
            dbg_mask[i] = 0xff;
        }
    }

    void check(size_t size, T *upd_data, unsigned int base_address, std::set<ModbusMonitor*> &changes) {
        if (size != buflen) {
            buflen = size;
            if (last_data) delete[] last_data;
            if (dbg_mask) delete[] dbg_mask;
            if (cmp_data) delete[] cmp_data;
            if (size) {
                initial_read = true;
                last_data = new T[size];
                memset(last_data, 0, size);
                dbg_mask = new T[size];
                memset(dbg_mask, 0xff, size);
                cmp_data = new T[size];
                memset(cmp_data, 0, size);
            }
        }
        if (size) {
            if (options.verbose && initial_read) {
                //std::cout << name << " initial read. size: " << size << "\n";
                displayAscii(upd_data, buflen);
                std::cout << "\n";
            }
            T *p = upd_data, *q = cmp_data; //, *msk = dbg_mask;
            // note: masks are not currently used but we retain this functionality for future
            for (size_t ii=0; ii<size; ++ii) {
                ModbusMonitor *mm;
                if (*q != *p) { 
                    //if (options.verbose) std::cout << "change at " << (base_address + (q-cmp_data) ) << "\n";
                    mm = ModbusMonitor::lookupAddress(base_address + (q-cmp_data) ); 
                    if (mm) { changes.insert(mm); } //if (options.verbose) std::cout << "found change " << mm->name() << "\n"; }
                }
                *q++ = *p++; // & *msk++;
            }
            
            // check changes and build a set of changed monitors
            if (!initial_read && memcmp( cmp_data, last_data, size * sizeof(T)) != 0) {
                if (options.verbose) { 
                    std::cout << "\n"; display(upd_data, (buflen<120)?buflen:120); std::cout << "\n";
                    //std::cout << " "; display(dbg_mask, buflen); std::cout << "\n";
                }
            }
            memcpy(last_data, cmp_data, size * sizeof(T));
            initial_read = false;
        }
    }
};
#endif
