#define ARENA_BLOCK_SIZE (64 * 1024)

struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    unsigned char data[ARENA_BLOCK_SIZE];
};

struct Arena {
    ArenaBlock *head;
};

static Arena g_ast_arena = { nullptr };

static ArenaBlock* arena_new_block() {
    ArenaBlock *b = (ArenaBlock*)malloc(sizeof(ArenaBlock));
    b->next = nullptr;
    b->used = 0;
    return b;
}

static void* arena_alloc(size_t size) {
    size = (size + 7) & ~((size_t)7);

    if (size > ARENA_BLOCK_SIZE) {
        try_depth = 0;
        print_err("alloc_guard", size, (size_t)ARENA_BLOCK_SIZE);
        exit(CODE_PANIC);
    }

    if (!g_ast_arena.head) {
        g_ast_arena.head = arena_new_block();
    }

    ArenaBlock *b = g_ast_arena.head;
    if (b->used + size > ARENA_BLOCK_SIZE) {
        ArenaBlock *nb = arena_new_block();
        nb->next = g_ast_arena.head;
        g_ast_arena.head = nb;
        b = nb;
    }

    void *ptr = b->data + b->used;
    b->used += size;
    return ptr;
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