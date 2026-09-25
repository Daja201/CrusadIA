#include "cosio.h"
int main() {
    klog("before open\n");
    int fd = cosfs_open("t.txt", 0x42);
    klog("after open\n");
    cosfs_close(fd);
    klog("after close\n");
    return 0;
}