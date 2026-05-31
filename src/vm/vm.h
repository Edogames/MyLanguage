#ifndef MYLANG_VM_H
#define MYLANG_VM_H

int vm_run_source(const char* source, const char* filename);
int vm_run_bundle(const char* path);
int vm_write_bundle(const char* path, const char* source, const char* target);

#endif

