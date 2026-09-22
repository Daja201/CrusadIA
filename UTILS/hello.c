#include <os.h>

int main(int argc, char **argv) {
    kklog("hello from cc!");

    klogf("screen is %dx%d\n", Wwidth(), Hheight());
    klogf("ticks since boot: %d\n", system_ticks);

    if (argc > 1) {
        klog("args: ");
        for (int i = 1; i < argc; i++) {
            klog(argv[i]);
            klog(" ");
        }
        klog("\n");
    } else {
        klog("no args passed\n");
    }

    vesa_draw_rec(20, 20, 200, 100, 0x00FF6600);
    vesa_swap();

    return 0;
}