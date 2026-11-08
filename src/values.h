static bool is_numeric(Value v);
static double as_double(Value v);
static bool is_truthy(Value v);
static bool is_ident(AstNode *node, const char *name);
static bool is_ext_annotation(AstNode *type_annotation);
static bool ident_is_type_name(const char *name, int len, ValueType *out_vt);

static bool values_equal(Value a, Value b);
static bool coerce(Value v, Value *out);
static long truncate_int(long v, const char *tname, int tlen);
static bool value_matches_type(AstNode *type_annotation, Value v, Value *out);
static bool type_is_void(AstNode *rt);
static bool return_type_matches(AstNode *return_type, Value result, Value *out, bool loose = true);
static Value eval_math(TokenType op, Value l, Value r);
static Value eval_concat(Value l, Value r);
static void array_needs_capacity(ObjArray *a, int needed);
static void* grow_array(void *arr, int count, int *capacity, size_t elem_size, int min_cap);
static int array_find_index(ObjArray *a, long target);
void value_print(Value v, FILE *out = stdout);

static Value make_null();
static Value make_bool(bool b);
static Value make_int(long i);
static Value make_float(double f);
static Value make_str(const char *start, int len);
static Value make_rune(const char *start, int len);
static Value make_str_from_value(Value v);
static Value make_ext_asset(AstNode *path_expr);

static const struct { const char *name; int len; ValueType vt; } type_names[] = {
    {"string", 6, ValueType::Str},
    {"rune", 4, ValueType::Rune},
    {"bool", 4, ValueType::Bool},
    {"i8", 2, ValueType::Int},
    {"i16", 3, ValueType::Int},
    {"i32", 3, ValueType::Int},
    {"i64", 3, ValueType::Int},
    {"u8", 2, ValueType::Int},
    {"u16", 3, ValueType::Int},
    {"u32", 3, ValueType::Int},
    {"u64", 3, ValueType::Int},
    {"f32", 3, ValueType::Float},
    {"f64", 3, ValueType::Float},
    {"array", 5, ValueType::Array},
    {"struct", 6, ValueType::Struct},
};