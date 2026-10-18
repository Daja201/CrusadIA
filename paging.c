#include "string.h"
#include "pmm.h"
#include "vesa.h"
#include "paging.h"
#include "klog.h"

uint32_t* page_directory = (uint32_t *)0x10000;

#define PAT_MSR 0x277
#define FB_FLAGS_WC 0x0B
#define FB_FLAGS_FALLBACK 0x03

static int pat_wc_ready = 0;

static int cpu_has_pat(void) {
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx >> 16) & 1;
}

static void pat_setup_wc(void) {
    if (!cpu_has_pat()) {
        klog_color("paging: CPU has no PAT support, framebuffer will use default caching\n", 0xFFFF00);
        pat_wc_ready = 0;
        return;
    }

    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(PAT_MSR));
    klogf_color("paging: IA32_PAT before=0x%x%x\n", 0x00FFFF, hi, lo);

    lo = (lo & 0xFFFF00FFu) | (0x01u << 8);
    asm volatile("wrmsr" :: "c"(PAT_MSR), "a"(lo), "d"(hi));

    uint32_t lo2, hi2;
    asm volatile("rdmsr" : "=a"(lo2), "=d"(hi2) : "c"(PAT_MSR));
    klogf_color("paging: IA32_PAT after=0x%x%x (PA1=WC)\n", 0x00FF00, hi2, lo2);

    pat_wc_ready = 1;
}

static uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t benchmark_fill(uint32_t phys_addr, uint32_t size) {
    volatile uint8_t* p = (volatile uint8_t*)(uintptr_t)phys_addr;
    uint64_t start = rdtsc_now();
    memset((void*)p, 0xAA, size);
    uint64_t end = rdtsc_now();
    return end - start;
}

static uint32_t pick_fb_flags(uint32_t fb_phys_addr, uint32_t fb_size) {
    if (!pat_wc_ready) {
        klog_color("paging: no PAT, framebuffer uses fallback caching\n", 0xFFFF00);
        return FB_FLAGS_FALLBACK;
    }

    uint32_t test_size = (fb_size < (1u << 20)) ? fb_size : (1u << 20);
    if (test_size < 4096) {
        return FB_FLAGS_FALLBACK;
    }

    const int trials = 5;
    uint64_t wc_best = 0, wb_best = 0;

    for (int t = 0; t < trials; t++) {
        for (uint32_t i = 0; i < test_size; i += 4096) {
            map_page(fb_phys_addr + i, fb_phys_addr + i, FB_FLAGS_WC);
        }
        uint64_t ticks = benchmark_fill(fb_phys_addr, test_size);
        if (ticks > 0 && (wc_best == 0 || ticks < wc_best)) wc_best = ticks;
    }

    for (int t = 0; t < trials; t++) {
        for (uint32_t i = 0; i < test_size; i += 4096) {
            map_page(fb_phys_addr + i, fb_phys_addr + i, FB_FLAGS_FALLBACK);
        }
        uint64_t ticks = benchmark_fill(fb_phys_addr, test_size);
        if (ticks > 0 && (wb_best == 0 || ticks < wb_best)) wb_best = ticks;
    }

    klogf_color("paging: fb caching benchmark best wc_ticks=%d fallback_ticks=%d\n", 0x00FFFF,
        (uint32_t)wc_best, (uint32_t)wb_best);

    if (wc_best > 0 && wb_best > 0 && wc_best < (wb_best / 2)) {
        klog_color("paging: write-combining framebuffer chosen\n", 0x00FF00);
        return FB_FLAGS_WC;
    }
    klog_color("paging: fallback caching chosen for framebuffer\n", 0x00FF00);
    return FB_FLAGS_FALLBACK;
}

static void protect_table_frame(uint32_t phys) {
    uint32_t pd_index = phys >> 22;
    uint32_t pt_index = (phys >> 12) & 0x03FF;
    if ((page_directory[pd_index] & 0x01) == 0) {
        void* t = pmm_alloc_block();
        if (!t) return;
        memset(t, 0, 4096);
        page_directory[pd_index] = ((uint32_t)t) | 0x07;
    }
    uint32_t* table = (uint32_t*)(page_directory[pd_index] & ~0xFFFu);
    table[pt_index] = (phys & ~0xFFFu) | 0x03;
}

void map_page_dir(uint32_t* dir, uint32_t phys, uint32_t virt, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    if ((dir[pd_index] & 0x01) == 0) {
        void* new_table = pmm_alloc_block();
        if (!new_table) {
            klog_status("PAGING: OUT OF MEMORY FOR PAGE TABLE", 0xFF0000);
            return;
        }
        memset(new_table, 0, 4096);
        protect_table_frame((uint32_t)new_table);
        dir[pd_index] = ((uint32_t)new_table) | (flags & 0x07) | 0x01;
    }
    uint32_t* page_table = (uint32_t*)(dir[pd_index] & ~0xFFF);
    page_table[pt_index] = (phys & ~0xFFF) | flags;
}

void map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
    map_page_dir(page_directory, phys, virt, flags);
}

void unmap_page_dir(uint32_t* dir, uint32_t virt) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    if ((dir[pd_index] & 0x01) == 0) {
        return;
    }
    uint32_t* old_table = (uint32_t*)(dir[pd_index] & ~0xFFF);
    void* new_table = pmm_alloc_block();
    if (!new_table) {
        klog_status("PAGING: OUT OF MEMORY FOR PAGE TABLE", 0xFF0000);
        return;
    }
    protect_table_frame((uint32_t)new_table);
    uint32_t* nt = (uint32_t*)new_table;
    for (int i = 0; i < 1024; i++) {
        nt[i] = old_table[i];
    }
    nt[pt_index] = 0;
    dir[pd_index] = ((uint32_t)new_table) | (dir[pd_index] & 0xFFF);
}

uint32_t* clone_kernel_page_directory(void) {
    void* new_dir = pmm_alloc_block();
    if (!new_dir) {
        klog_status("PAGING: OUT OF MEMORY FOR PAGE DIRECTORY", 0xFF0000);
        return 0;
    }
    protect_table_frame((uint32_t)new_dir);
    uint32_t* nd = (uint32_t*)new_dir;
    for (int i = 0; i < 1024; i++) {
        nd[i] = page_directory[i];
    }
    return nd;
}

void switch_page_directory(uint32_t* dir) {
    asm volatile("mov %0, %%cr3" :: "r"(dir));
}

void init_paging(uint32_t fb_phys_addr, uint32_t fb_width, uint32_t fb_height, uint32_t fb_bpp) {
    page_directory = (uint32_t*)0x10000;
    memset(page_directory, 0, 4096);
    for (uint32_t i = 0; i < 1048576; i++) {
        uint32_t addr = i * 4096;
        map_page(addr, addr, 0x03);
    }
    pat_setup_wc();
    uint32_t fb_size = fb_width * fb_height * (fb_bpp / 8);
    uint32_t fb_flags = pick_fb_flags(fb_phys_addr, fb_size);
    klogf_color("paging: mapping framebuffer phys=0x%x size=0x%x flags=0x%x\n", 0x00FFFF,
        fb_phys_addr, fb_size, fb_flags);
    for (uint32_t i = 0; i < fb_size; i += 4096) {
        map_page(fb_phys_addr + i, fb_phys_addr + i, fb_flags);
    }

    map_page(0, 0, 0x02);
    map_page(0x10000, 0x10000, 0x03);

    asm volatile("mov %0, %%cr3" :: "r"(page_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}