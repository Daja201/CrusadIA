#include "heap.h"
#include "pmm.h"
#include "string.h"
#include "task.h"
#include "serial.h"

static void dbgw(const char *s) { while (*s) write_serial(*s++); }
static void dbgwx(unsigned int v) {
    const char *h = "0123456789ABCDEF";
    write_serial('0'); write_serial('x');
    for (int i = 28; i >= 0; i -= 4) write_serial(h[(v >> i) & 0xF]);
}

#define HEAP_MAGIC 0x5A4C534A
#define ALIGN 8
#define ALIGN_UP(x) (((x) + (ALIGN - 1)) & ~(size_t)(ALIGN - 1))
#define SIZE_MAX_LOCAL ((size_t)-1)

struct heap_block_header {
    uint32_t magic;
    size_t size;
    uint8_t free;
    struct heap_block_header* next;
    struct heap_block_header* prev;
};
typedef struct heap_block_header block_header_t;

#define HEADER_SIZE ALIGN_UP(sizeof(block_header_t))

static heap_arena_t kernel_heap_arena;

static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory", "cc");
    return flags;
}
static inline void irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

void heap_init(void) {
    kernel_heap_arena.head = 0;
    kernel_heap_arena.tail = 0;
}

static block_header_t* grow_heap(heap_arena_t* arena, size_t min_size, int owner_slot) {
    if (min_size > SIZE_MAX_LOCAL - HEADER_SIZE) return 0;
    size_t needed = HEADER_SIZE + min_size;
    if (needed > SIZE_MAX_LOCAL - (PMM_BLOCK_SIZE - 1)) return 0;
    size_t pages_needed = (needed + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;

    dbgw("grow_heap calling pmm_alloc_block\n");
    void* first = pmm_alloc_block();
    dbgw("grow_heap pmm_alloc_block -> "); dbgwx((unsigned int)(unsigned long)first); dbgw("\n");
    if (!first) return 0;
    if (owner_slot >= 0) {
        uint32_t f = (uint32_t)first;
        task_track_frames(owner_slot, &f, 1);
    }

    uint32_t region_start = (uint32_t)first;
    uint32_t region_end = region_start + PMM_BLOCK_SIZE;
    size_t got_pages = 1;

    while (got_pages < pages_needed) {
        void* next_page = pmm_alloc_block();
        if (!next_page) break;
        if ((uint32_t)next_page == region_end) {
            if (owner_slot >= 0) {
                uint32_t f = (uint32_t)next_page;
                task_track_frames(owner_slot, &f, 1);
            }
            region_end += PMM_BLOCK_SIZE;
            got_pages++;
        } else {
            pmm_free_block(next_page);
            break;
        }
    }

    block_header_t* block = (block_header_t*)region_start;
    block->magic = HEAP_MAGIC;
    block->size = (region_end - region_start) - HEADER_SIZE;
    block->free = 1;
    block->next = 0;
    block->prev = arena->tail;

    if (arena->tail) arena->tail->next = block;
    else arena->head = block;
    arena->tail = block;

    return block;
}

static void split_block(heap_arena_t* arena, block_header_t* block, size_t size) {
    dbgw("split_block enter block="); dbgwx((unsigned int)(unsigned long)block);
    dbgw(" magic="); dbgwx((unsigned int)block->magic);
    dbgw(" block->size="); dbgwx((unsigned int)block->size);
    dbgw(" size="); dbgwx((unsigned int)size); dbgw("\n");

    if (size > block->size) { dbgw("split_block too-small return\n"); return; }
    if (block->size - size < HEADER_SIZE + ALIGN) { dbgw("split_block no-room return\n"); return; }

    block_header_t* new_block = (block_header_t*)((uint8_t*)block + HEADER_SIZE + size);
    dbgw("split_block new_block="); dbgwx((unsigned int)(unsigned long)new_block); dbgw("\n");
    new_block->magic = HEAP_MAGIC;
    dbgw("split_block wrote magic\n");
    new_block->size = block->size - size - HEADER_SIZE;
    dbgw("split_block wrote size\n");
    new_block->free = 1;
    dbgw("split_block wrote free\n");
    new_block->next = block->next;
    dbgw("split_block wrote next, block->next="); dbgwx((unsigned int)(unsigned long)block->next); dbgw("\n");
    new_block->prev = block;
    dbgw("split_block wrote prev\n");

    if (block->next) {
        dbgw("split_block writing block->next->prev\n");
        block->next->prev = new_block;
        dbgw("split_block wrote block->next->prev\n");
    } else {
        arena->tail = new_block;
        dbgw("split_block wrote arena->tail\n");
    }

    block->next = new_block;
    dbgw("split_block wrote block->next = new_block\n");
    block->size = size;
    dbgw("split_block done\n");
}

static void coalesce(heap_arena_t* arena, block_header_t* block) {
    if (block->next && block->next->free &&
        (uint8_t*)block + HEADER_SIZE + block->size == (uint8_t*)block->next) {
        block_header_t* dead = block->next;
        block->size += HEADER_SIZE + dead->size;
        block->next = dead->next;
        if (dead->next) dead->next->prev = block;
        else arena->tail = block;
    }

    if (block->prev && block->prev->free &&
        (uint8_t*)block->prev + HEADER_SIZE + block->prev->size == (uint8_t*)block) {
        block_header_t* prev = block->prev;
        prev->size += HEADER_SIZE + block->size;
        prev->next = block->next;
        if (block->next) block->next->prev = prev;
        else arena->tail = prev;
    }
}

void heap_arena_init(heap_arena_t* arena) {
    arena->head = 0;
    arena->tail = 0;
}

void* heap_arena_alloc(heap_arena_t* arena, size_t size, int owner_slot) {
    if (size == 0) return 0;
    if (size > SIZE_MAX_LOCAL - (ALIGN - 1)) return 0;
    size = ALIGN_UP(size);

    dbgw("heap_arena_alloc enter size="); dbgwx((unsigned int)size); dbgw("\n");
    uint32_t fl = irq_save();

    block_header_t* cur = arena->head;
    int steps = 0;
    while (cur) {
        if (++steps > 1000000) { dbgw("heap_arena_alloc LIST LOOP STUCK at "); dbgwx((unsigned int)(unsigned long)cur); dbgw("\n"); irq_restore(fl); return 0; }
        if (cur->free && cur->size >= size) {
            dbgw("heap_arena_alloc reusing block "); dbgwx((unsigned int)(unsigned long)cur); dbgw("\n");
            split_block(arena, cur, size);
            cur->free = 0;
            irq_restore(fl);
            return (void*)((uint8_t*)cur + HEADER_SIZE);
        }
        cur = cur->next;
    }

    dbgw("heap_arena_alloc calling grow_heap\n");
    block_header_t* fresh = grow_heap(arena, size, owner_slot);
    dbgw("heap_arena_alloc grow_heap -> "); dbgwx((unsigned int)(unsigned long)fresh); dbgw("\n");
    if (!fresh) { irq_restore(fl); return 0; }
    if (fresh->size < size) { irq_restore(fl); return 0; }

    split_block(arena, fresh, size);
    fresh->free = 0;
    irq_restore(fl);
    dbgw("heap_arena_alloc done -> "); dbgwx((unsigned int)(unsigned long)fresh); dbgw("\n");
    return (void*)((uint8_t*)fresh + HEADER_SIZE);
}

void heap_arena_free(heap_arena_t* arena, void* ptr) {
    if (!ptr) return;
    if (((uintptr_t)ptr % ALIGN) != 0) return;

    uint32_t fl = irq_save();
    block_header_t* block = (block_header_t*)((uint8_t*)ptr - HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) { irq_restore(fl); return; }
    if (block->free) { irq_restore(fl); return; }
    block->free = 1;
    coalesce(arena, block);
    irq_restore(fl);
}

void* heap_arena_calloc(heap_arena_t* arena, size_t nmemb, size_t size, int owner_slot) {
    if (nmemb == 0 || size == 0) return 0;
    if (nmemb > SIZE_MAX_LOCAL / size) return 0;
    size_t total = nmemb * size;
    void* ptr = heap_arena_alloc(arena, total, owner_slot);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* heap_arena_realloc(heap_arena_t* arena, void* ptr, size_t size, int owner_slot) {
    if (!ptr) return heap_arena_alloc(arena, size, owner_slot);
    if (size == 0) {
        heap_arena_free(arena, ptr);
        return 0;
    }
    if (((uintptr_t)ptr % ALIGN) != 0) return 0;

    uint32_t fl = irq_save();

    block_header_t* block = (block_header_t*)((uint8_t*)ptr - HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) { irq_restore(fl); return 0; }
    if (block->free) { irq_restore(fl); return 0; }
    if (size > SIZE_MAX_LOCAL - (ALIGN - 1)) { irq_restore(fl); return 0; }

    size_t aligned = ALIGN_UP(size);
    if (block->size >= aligned) {
        split_block(arena, block, aligned);
        irq_restore(fl);
        return ptr;
    }

    size_t old_size = block->size;
    irq_restore(fl);

    void* new_ptr = heap_arena_alloc(arena, size, owner_slot);
    if (!new_ptr) return 0;
    memcpy(new_ptr, ptr, old_size);
    heap_arena_free(arena, ptr);
    return new_ptr;
}

void* malloc(size_t size) { return heap_arena_alloc(&kernel_heap_arena, size, -1); }
void free(void* ptr) { heap_arena_free(&kernel_heap_arena, ptr); }
void* calloc(size_t nmemb, size_t size) { return heap_arena_calloc(&kernel_heap_arena, nmemb, size, -1); }
void* realloc(void* ptr, size_t size) { return heap_arena_realloc(&kernel_heap_arena, ptr, size, -1); }

void heap_stats(uint32_t* out_total, uint32_t* out_used, uint32_t* out_free) {
    uint32_t total = 0, used = 0, freeb = 0;
    block_header_t* cur = kernel_heap_arena.head;
    while (cur) {
        total += (uint32_t)(cur->size + HEADER_SIZE);
        if (cur->free) freeb += (uint32_t)cur->size;
        else used += (uint32_t)cur->size;
        cur = cur->next;
    }
    if (out_total) *out_total = total;
    if (out_used) *out_used = used;
    if (out_free) *out_free = freeb;
}