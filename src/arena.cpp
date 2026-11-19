#define ARENA_BLOCK_SIZE (64 * 1024)

struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    unsigned char data[ARENA_BLOCK_SIZE];
};

struct Arena {
    ArenaBlock *head;
};

static Arena g_ast_arena = { nullptr };    // AST and source, never release this
static Arena g_temp_arena = { nullptr };   // runtime temps, released back to a marked point
static Arena g_value_arena = { nullptr };  // copies owned, recycled through g_free
static ArenaBlock *g_spare = nullptr;      // released temp blocks, reused before malloc
static void *g_free[64];                   // free value chunks, indexed by log2 of chunk size

static ArenaBlock* arena_new_block() {
    ArenaBlock *b = g_spare;

    if (b) {
        g_spare = b->next;
    } else {
        b = (ArenaBlock*)malloc(sizeof(ArenaBlock));
    }

    b->next = nullptr;
    b->used = 0;
    return b;
}

static void* arena_alloc(size_t size, Arena *arena = &g_temp_arena) {
    size = (size + 7) & ~((size_t)7);

    if (size > ARENA_BLOCK_SIZE) {
        try_depth = 0;
        top_exit_code = CODE_PANIC;
        error_handler = &top_handler;
        print_err("alloc_guard", size, (size_t)ARENA_BLOCK_SIZE);
        exit(CODE_PANIC);
    }

    if (!arena->head) {
        arena->head = arena_new_block();
    }

    ArenaBlock *b = arena->head;
    if (b->used + size > ARENA_BLOCK_SIZE) {
        ArenaBlock *nb = arena_new_block();
        nb->next = arena->head;
        arena->head = nb;
        b = nb;
    }

    void *ptr = b->data + b->used;
    b->used += size;
    return ptr;
}

static ArenaMark temp_mark() {
    ArenaBlock *b = g_temp_arena.head;
    return { b, b ? b->used : 0 };
}

static void temp_release(ArenaMark m) {
    while (g_temp_arena.head != m.head) {
        ArenaBlock *b = g_temp_arena.head;
        g_temp_arena.head = b->next;
        b->next = g_spare;
        g_spare = b;
    }
    if (m.head) m.head->used = m.used;
}

static int size_class(size_t size) {
    int c = 4;
    while (((size_t)1 << c) < size) c++;
    return c;
}

static void* value_alloc(size_t size) {
    int c = size_class(size);
    void *p = g_free[c];
    if (p) { g_free[c] = *(void**)p; return p; }
    return arena_alloc((size_t)1 << c, &g_value_arena);
}

static void value_release(void *p, size_t size) {
    int c = size_class(size);
    *(void**)p = g_free[c];
    g_free[c] = p;
}

void ast_arena_free_all() {
    ArenaBlock *b = g_ast_arena.head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    g_ast_arena.head = nullptr;
}