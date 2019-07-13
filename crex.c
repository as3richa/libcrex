#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

typedef crex_status_t status_t;
typedef crex_regex_t regex_t;
#define WARN_UNUSED_RESULT CREX_WARN_UNUSED_RESULT

#define REPETITION_INFINITY (~(size_t)0)

// FIXME: put these somewhere

void my_memcpy(void *destination, const void *source, size_t size) {
  assert(destination != NULL || size == 0);

  if (size != 0) {
    memcpy(destination, source, size);
  }
}

typedef enum {
  AT_BOF,
  AT_BOL,
  AT_EOF,
  AT_EOL,
  AT_WORD_BOUNDARY,
  AT_NOT_WORD_BOUNDARY
} anchor_type_t;

/** Lexer **/

typedef enum {
  TT_CHARACTER,
  TT_ANCHOR,
  TT_PIPE,
  TT_GREEDY_REPETITION,
  TT_LAZY_REPETITION,
  TT_OPEN_PAREN,
  TT_CLOSE_PAREN
} token_type_t;

typedef struct {
  token_type_t type;

  union {
    unsigned char character;

    anchor_type_t anchor_type;

    struct {
      size_t lower_bound, upper_bound;
    } repetition;
  } data;
} token_t;

WARN_UNUSED_RESULT static status_t lex(token_t *result, const char **str, const char *eof) {
  assert((*str) < eof);

  const char byte = *((*str)++);

  switch (byte) {
  case '^':
    result->type = TT_ANCHOR;
    result->data.anchor_type = AT_BOL;
    break;

  case '$':
    result->type = TT_ANCHOR;
    result->data.anchor_type = AT_EOL;
    break;

  case '|':
    result->type = TT_PIPE;
    break;

  case '*':
  case '+':
  case '?':
    switch (byte) {
    case '*':
      result->data.repetition.lower_bound = 0;
      result->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '+':
      result->data.repetition.lower_bound = 1;
      result->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '?':
      result->data.repetition.lower_bound = 0;
      result->data.repetition.upper_bound = 1;
      break;
    }

    if ((*str) < eof && *(*str) == '?') {
      (*str)++;
      result->type = TT_LAZY_REPETITION;
    } else {
      result->type = TT_GREEDY_REPETITION;
    }

    break;

  case '(':
    result->type = TT_OPEN_PAREN;
    break;

  case ')':
    result->type = TT_CLOSE_PAREN;
    break;

  case '\\':
    result->type = TT_CHARACTER;

    if ((*str) == eof) {
      return CREX_E_BAD_ESCAPE;
    }

    switch (*((*str)++)) {
    case 'a':
      result->data.character = '\a';
      break;

    case 'f':
      result->data.character = '\f';
      break;

    case 'n':
      result->data.character = '\n';
      break;

    case 'r':
      result->data.character = '\r';
      break;

    case 't':
      result->data.character = '\t';
      break;

    case 'v':
      result->data.character = '\v';
      break;

    case 'A':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_BOF;
      break;

    case 'z':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_EOF;
      break;

    case 'b':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_WORD_BOUNDARY;
      break;

    case 'B':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_NOT_WORD_BOUNDARY;
      break;

    case 'x': {
      int value = 0;

      for (int i = 2; i--;) {
        if ((*str) == eof) {
          return CREX_E_BAD_ESCAPE;
        }

        const char hex_byte = *((*str)++);

        int digit;

        if ('0' <= hex_byte && hex_byte <= '9') {
          digit = hex_byte - '0';
        } else if ('a' <= hex_byte && hex_byte <= 'f') {
          digit = hex_byte - 'a' + 0xa;
        } else if ('A' <= hex_byte && hex_byte <= 'F') {
          digit = hex_byte - 'A' + 0xa;
        } else {
          return CREX_E_BAD_ESCAPE;
        }

        value = 16 * value + digit;
      }

      result->data.character = value;

      break;
    }

    case '.':
    case '|':
    case '*':
    case '+':
    case '?':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '^':
    case '$':
    case '\\':
      result->data.character = byte;
      break;

    case 'd':
    case 'D':
    case 's':
    case 'S':
    case 'w':
    case 'W':
      assert(0 && "FIXME");
      break;

    default:
      return CREX_E_BAD_ESCAPE;
    }

    break;

  default:
    result->type = TT_CHARACTER;
    result->data.character = byte;
  }

  return CREX_OK;
}

/** Parser **/

typedef enum {
  PT_CONCATENATION,
  PT_ALTERNATION,
  PT_GREEDY_REPETITION,
  PT_LAZY_REPETITION,
  PT_GROUP,
  PT_EMPTY,
  PT_CHARACTER,
  PT_ANCHOR,
} parsetree_type_t;

/* FIXME: explain this */
static const size_t operator_precedence[] = {1, 0, 2, 2};

typedef struct parsetree {
  parsetree_type_t type;

  union {
    unsigned char character;

    anchor_type_t anchor_type;

    struct parsetree *children[2];

    struct {
      size_t lower_bound, upper_bound;
      struct parsetree *child;
    } repetition;

    struct {
      size_t index;
      struct parsetree *child;
    } group;
  } data;
} parsetree_t;

typedef struct {
  parsetree_type_t type;

  union {
    struct {
      size_t lower_bound, upper_bound;
    } repetition;

    size_t group_index;
  } data;
} operator_t;

typedef struct {
  size_t size, capacity;
  parsetree_t **data;
} tree_stack_t;

typedef struct {
  size_t size, capacity;
  operator_t *data;
} operator_stack_t;

static void free_parsetree(parsetree_t *tree);

WARN_UNUSED_RESULT static int push_tree(tree_stack_t *trees, parsetree_t *tree);
WARN_UNUSED_RESULT static int push_empty(tree_stack_t *trees);
static void free_tree_stack(tree_stack_t *trees);

WARN_UNUSED_RESULT static int
push_operator(tree_stack_t *trees, operator_stack_t *operators, const operator_t *operator);
WARN_UNUSED_RESULT static int pop_operator(tree_stack_t *trees, operator_stack_t *operators);

WARN_UNUSED_RESULT status_t parse(parsetree_t **result, const char *str, size_t length) {
  const char *eof = str + length;

  tree_stack_t trees = {0, 0, NULL};
  operator_stack_t operators = {0, 0, NULL};

#define CHECK_ERRORS(condition, code)                                                              \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      free_tree_stack(&trees);                                                                     \
      free(operators.data);                                                                        \
      return code;                                                                                 \
    }                                                                                              \
  } while (0)

  {
    operator_t outer_group;
    outer_group.type = PT_GROUP;
    outer_group.data.group_index = 0;

    CHECK_ERRORS(push_operator(&trees, &operators, &outer_group), CREX_E_NOMEM);
    CHECK_ERRORS(push_empty(&trees), CREX_E_NOMEM);
  }

  size_t n_groups = 1;

  while (str != eof) {
    token_t token;
    const status_t lex_status = lex(&token, &str, eof);

    CHECK_ERRORS(lex_status == CREX_OK, lex_status);

    switch (token.type) {
    case TT_CHARACTER:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);

      parsetree_t *tree = malloc(sizeof(parsetree_t));
      CHECK_ERRORS(tree != NULL, CREX_E_NOMEM);

      if (token.type == TT_CHARACTER) {
        tree->type = PT_CHARACTER;
        tree->data.character = token.data.character;
      } else {
        tree->type = PT_ANCHOR;
        tree->data.anchor_type = token.data.anchor_type;
      }

      CHECK_ERRORS(push_tree(&trees, tree), CREX_E_NOMEM);

      break;
    }

    case TT_PIPE: {
      operator_t operator;
      operator.type = PT_ALTERNATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);
      CHECK_ERRORS(push_empty(&trees), CREX_E_NOMEM);
      break;
    }

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      operator_t operator;
      operator.type = (token.type == TT_GREEDY_REPETITION) ? PT_GREEDY_REPETITION : PT_LAZY_REPETITION;
      operator.data.repetition.lower_bound = token.data.repetition.lower_bound;
      operator.data.repetition.upper_bound = token.data.repetition.upper_bound;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);
      break;
    }

    case TT_OPEN_PAREN: {
      operator_t operator;

      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);

      operator.type = PT_GROUP;
      operator.data.group_index = n_groups ++;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);

      CHECK_ERRORS(push_empty(&trees), CREX_E_NOMEM);
      break;
    }

    case TT_CLOSE_PAREN:
      while (operators.size > 0) {
        if (operators.data[operators.size - 1].type == PT_GROUP) {
          break;
        }

        CHECK_ERRORS(pop_operator(&trees, &operators), CREX_E_NOMEM);
      }

      CHECK_ERRORS(operators.size > 0, CREX_E_UNMATCHED_CLOSE_PAREN);

      assert(operators.data[operators.size - 1].type == PT_GROUP);

      CHECK_ERRORS(pop_operator(&trees, &operators), CREX_E_NOMEM);

      break;

    default:
      assert(0);
    }
  }

  while (operators.size > 0) {
    if(operators.data[operators.size - 1].type == PT_GROUP) {
      CHECK_ERRORS(operators.size == 1, CREX_E_UNMATCHED_OPEN_PAREN);
    }

    CHECK_ERRORS(pop_operator(&trees, &operators), CREX_E_NOMEM);
  }

  assert(trees.size == 1);

  (*result) = trees.data[0];

  free(trees.data);
  free(operators.data);

  return CREX_OK;
}

static void free_parsetree(parsetree_t *tree) {
  switch (tree->type) {
  case PT_EMPTY:
  case PT_CHARACTER:
  case PT_ANCHOR:
    break;

  case PT_CONCATENATION:
  case PT_ALTERNATION:
    free_parsetree(tree->data.children[0]);
    free_parsetree(tree->data.children[1]);
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION:
    free_parsetree(tree->data.repetition.child);
    break;

  case PT_GROUP:
    free_parsetree(tree->data.group.child);
    break;

  default:
    assert(0);
  }

  free(tree);
}

static int push_tree(tree_stack_t *trees, parsetree_t *tree) {
  assert(trees->size <= trees->capacity);

  if (trees->size == trees->capacity) {
    trees->capacity = 2 * trees->capacity + 16;
    parsetree_t **data = realloc(trees->data, sizeof(parsetree_t *) * trees->capacity);

    if (data == NULL) {
      return 0;
    }

    trees->data = data;
  }

  trees->data[trees->size++] = tree;

  return 1;
}

static int push_empty(tree_stack_t *trees) {
  parsetree_t *tree = malloc(sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  tree->type = PT_EMPTY;

  return push_tree(trees, tree);
}

static void free_tree_stack(tree_stack_t *trees) {
  while (trees->size > 0) {
    free_parsetree(trees->data[--trees->size]);
  }

  free(trees->data);
}

static int
push_operator(tree_stack_t *trees, operator_stack_t *operators, const operator_t *operator) {
  assert(operators->size <= operators->capacity);

  const parsetree_type_t type = operator->type;

  if (type != PT_GROUP) {
    const size_t precedence = operator_precedence[type];

    assert(type == PT_CONCATENATION || type == PT_ALTERNATION || type == PT_GREEDY_REPETITION ||
           type == PT_LAZY_REPETITION);

    while (operators->size > 0) {
      const parsetree_type_t other_type = operators->data[operators->size - 1].type;

      assert(other_type == PT_GROUP || other_type == PT_CONCATENATION ||
             other_type == PT_ALTERNATION || other_type == PT_GREEDY_REPETITION ||
             other_type == PT_LAZY_REPETITION);

      const int should_pop =
          other_type != PT_GROUP && operator_precedence[other_type] >= precedence;

      if (!should_pop) {
        break;
      }

      if (!pop_operator(trees, operators)) {
        return 0;
      }
    }
  }

  if (operators->size == operators->capacity) {
    operators->capacity = 2 * operators->capacity + 16;

    operator_t *data = realloc(operators->data, sizeof(operator_t) * operators->capacity);

    if (data == NULL) {
      return 0;
    }

    operators->data = data;
  }

  operators->data[operators->size++] = *operator;

  return 1;
}

static int pop_operator(tree_stack_t *trees, operator_stack_t *operators) {
  assert(operators->size > 0);

  parsetree_t *tree = malloc(sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  const operator_t *operator= &operators->data[--operators->size];

  tree->type = operator->type;

  switch (tree->type) {
  case PT_CONCATENATION:
  case PT_ALTERNATION:
    assert(trees->size >= 2);
    tree->data.children[0] = trees->data[trees->size - 2];
    tree->data.children[1] = trees->data[trees->size - 1];
    trees->size--;
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION:
    assert(trees->size >= 1);
    tree->data.repetition.lower_bound = operator->data.repetition.lower_bound;
    tree->data.repetition.upper_bound = operator->data.repetition.upper_bound;
    tree->data.repetition.child = trees->data[trees->size - 1];
    break;

  case PT_GROUP:
    assert(trees->size >= 1);
    tree->data.group.index = operator->data.group_index;
    tree->data.group.child = trees->data[trees->size - 1];
    break;

  default:
    assert(0);
  }

  trees->data[trees->size - 1] = tree;

  return 1;
}

/** Compiler **/

enum {
  VM_CHARACTER,
  VM_ANCHOR_BOF,
  VM_ANCHOR_BOL,
  VM_ANCHOR_EOF,
  VM_ANCHOR_EOL,
  VM_ANCHOR_WORD_BOUNDARY,
  VM_ANCHOR_NOT_WORD_BOUNDARY,
  VM_JUMP,
  VM_SPLIT_PASSIVE,
  VM_SPLIT_EAGER,
  VM_WRITE_POINTER
};

static void serialize_long(unsigned char *destination, long value, size_t size) {
  assert(-2147483647 <= value && value <= 2147483647);

  switch (size) {
  case 1: {
    const int8_t i8_value = value;
    my_memcpy(destination, &i8_value, 1);
    break;
  }

  case 2: {
    const int16_t i16_value = value;
    my_memcpy(destination, &i16_value, 2);
    break;
  }

  case 4: {
    const int32_t i32_value = value;
    my_memcpy(destination, &i32_value, 4);
    break;
  }

  default:
    assert(0);
  }
}

static long deserialize_long(unsigned char *source, size_t size) {
  switch (size) {
  case 1: {
    int8_t i8_value;
    my_memcpy(&i8_value, source, 1);
    return i8_value;
  }

  case 2: {
    int16_t i16_value;
    my_memcpy(&i16_value, source, 2);
    return i16_value;
  }

  case 4: {
    int32_t i32_value;
    my_memcpy(&i32_value, source, 4);
    return i32_value;
  }

  default:
    assert(0);
    return 0;
  }
}

status_t compile(regex_t *result, parsetree_t *tree) {
  switch (tree->type) {
  case PT_EMPTY:
    result->size = 0;
    result->bytecode = NULL;
    break;

  case PT_CHARACTER:
    result->size = 2;
    result->bytecode = malloc(2);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_CHARACTER;
    result->bytecode[1] = tree->data.character;

    break;

  case PT_ANCHOR:
    result->size = 1;
    result->bytecode = malloc(1);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_ANCHOR_BOF + tree->data.anchor_type;

    break;

  case PT_CONCATENATION:
  case PT_ALTERNATION: {
    regex_t left, right;

    status_t status = compile(&left, tree->data.children[0]);

    if (status != CREX_OK) {
      return status;
    }

    status = compile(&right, tree->data.children[1]);

    if (status != CREX_OK) {
      free(left.bytecode);
      return status;
    }

    if (tree->type == PT_CONCATENATION) {
      result->size = left.size + right.size;
    } else {
      result->size = (1 + 4) + left.size + (1 + 4) + right.size;
    }

    result->bytecode = malloc(result->size);

    if (result->bytecode == NULL) {
      free(left.bytecode);
      free(right.bytecode);
      return CREX_E_NOMEM;
    }

    if (tree->type == PT_CONCATENATION) {
      my_memcpy(result->bytecode, left.bytecode, left.size);
      my_memcpy(result->bytecode + left.size, right.bytecode, right.size);
    } else {
      const size_t split_location = 0;
      const size_t left_location = 1 + 4;
      const size_t jump_location = left_location + left.size;
      const size_t right_location = jump_location + 1 + 4;

      const size_t split_origin = split_location + 1 + 4;
      const size_t jump_origin = jump_location + 1 + 4;

      const long split_delta = (long)right_location - (long)split_origin;
      const long jump_delta = (long)right_location + (long)right.size - (long)jump_origin;

      unsigned char *bytecode = result->bytecode;

      bytecode[split_location] = VM_SPLIT_PASSIVE;
      serialize_long(bytecode + split_location + 1, split_delta, 4);

      my_memcpy(bytecode + left_location, left.bytecode, left.size);

      bytecode[jump_location] = VM_JUMP;
      serialize_long(bytecode + jump_location + 1, jump_delta, 4);

      my_memcpy(bytecode + right_location, right.bytecode, right.size);
    }

    free(left.bytecode);
    free(right.bytecode);

    break;
  }

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    regex_t child;

    status_t status = compile(&child, tree->data.repetition.child);

    if (status != CREX_OK) {
      return status;
    }

    const size_t lower_bound = tree->data.repetition.lower_bound;
    const size_t upper_bound = tree->data.repetition.upper_bound;

    assert(lower_bound <= upper_bound && "FIXME");

    result->size = lower_bound * child.size;

    if (upper_bound == REPETITION_INFINITY) {
      result->size += 1 + 4 + child.size + 1 + 4;
    } else {
      result->size += (upper_bound - lower_bound) * (1 + 4 + child.size);
    }

    result->bytecode = malloc(result->size);

    if (result->bytecode == NULL) {
      free(child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;

    for (size_t i = 0; i < lower_bound; i++) {
      my_memcpy(bytecode + i * child.size, child.bytecode, child.size);
    }

    if (upper_bound == REPETITION_INFINITY) {
      /*
       * ...
       * VM_SPLIT_{PASSIVE,EAGER} end
       * child:
       * <child bytecode>
       * VM_SPLIT_{EAGER,PASSIVE} child
       * end:
       */

      unsigned char forward_split_opcode, backward_split_opcode;

      if (tree->type == PT_GREEDY_REPETITION) {
        forward_split_opcode = VM_SPLIT_PASSIVE;
        backward_split_opcode = VM_SPLIT_EAGER;
      } else {
        forward_split_opcode = VM_SPLIT_EAGER;
        backward_split_opcode = VM_SPLIT_PASSIVE;
      }

      const size_t offset = lower_bound * child.size;

      const size_t forward_split_origin = offset + 1 + 4;
      const long forward_split_delta = (long)result->size - (long)forward_split_origin;

      const size_t backward_split_location = offset + 1 + 4 + child.size;
      const size_t backward_split_origin = backward_split_location + 1 + 4;
      const long backward_split_delta = (long)forward_split_origin - (long)backward_split_origin;

      bytecode[offset] = forward_split_opcode;
      serialize_long(bytecode + offset + 1, forward_split_delta, 4);

      my_memcpy(bytecode + offset + 1 + 4, child.bytecode, child.size);

      bytecode[backward_split_location] = backward_split_opcode;
      serialize_long(bytecode + backward_split_location + 1, backward_split_delta, 4);
    } else {
      /*
       * ...
       * VM_SPLIT_{PASSIVE,EAGER} end
       * <child bytecode>
       * VM_SPLIT_{PASSIVE,EAGER} end
       * <child bytecode>
       * ...
       * VM_SPLIT_{PASSIVE,EAGER} end
       * <child bytecode>
       * end:
       */

      const unsigned char split_opcode =
          (tree->type == PT_GREEDY_REPETITION) ? VM_SPLIT_PASSIVE : VM_SPLIT_EAGER;

      for (size_t i = 0; i < upper_bound - lower_bound; i++) {
        const size_t offset = lower_bound * child.size + i * (1 + 4 + child.size);

        const size_t split_origin = offset + 1 + 4;
        const long split_delta = (long)result->size - (long)split_origin;

        bytecode[offset] = split_opcode;
        serialize_long(bytecode + offset + 1, split_delta, 4);

        my_memcpy(bytecode + offset + 1 + 4, child.bytecode, child.size);
      }
    }

    free(child.bytecode);

    break;
  }

  case PT_GROUP: {
    regex_t child;
    status_t status = compile(&child, tree->data.group.child);

    if (status != CREX_OK) {
      return status;
    }

    result->size = 1 + 4 + child.size + 1 + 4;

    result->bytecode = malloc(result->size);

    if(result->bytecode == NULL) {
      free(child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char* bytecode = result->bytecode;

    bytecode[0] = VM_WRITE_POINTER;
    serialize_long(bytecode + 1, (long)tree->data.group.index, 4); // FIXME: signedness

    my_memcpy(bytecode + 1 + 4, child.bytecode, child.size);

    const size_t offset = 1 + 4 + child.size;
    bytecode[offset] = VM_WRITE_POINTER;
    serialize_long(bytecode + offset + 1, (long)tree->data.group.index, 4); // FIXME: signedness

    free(child.bytecode);

    break;
  }

  default:
    assert(0);
  }

  return CREX_OK;
}

/** Executor **/

typedef struct {
  size_t instruction_pointer;
} thread_state_t;

typedef struct thread_list {
  thread_state_t state;
  struct thread_list *next;
} thread_list_t;

status_t
execute(crex_match_result_t *match_result, const regex_t *regex, const char *str, size_t length) {
  const size_t bitmap_size = (regex->size + CHAR_BIT - 1) / CHAR_BIT;

  unsigned char *visited = malloc(bitmap_size);

  if (visited == NULL) {
    return CREX_E_NOMEM;
  }

  // FIXME: justify (tighten?) this bound
  const size_t thread_list_capacity = 2 * regex->size;
  thread_list_t *thread_list_buffer = malloc(sizeof(thread_list_t) * thread_list_capacity);

  if (thread_list_buffer == NULL) {
    free(visited);
    return CREX_E_NOMEM;
  }

  for (size_t i = 0; i < thread_list_capacity; i++) {
    thread_list_buffer[i].next =
        (i == thread_list_capacity - 1) ? NULL : &thread_list_buffer[i + 1];
  }

  thread_list_t *thread_freelist = &thread_list_buffer[0];
  thread_list_t *thread_list_head = NULL;

#define PUSH(pointer, new_state)                                                                   \
  do {                                                                                             \
    assert(thread_freelist != NULL);                                                               \
    thread_list_t *node = thread_freelist;                                                         \
    thread_freelist = thread_freelist->next;                                                       \
    node->state = *(new_state);                                                                    \
    node->next = *(pointer);                                                                       \
    *(pointer) = node;                                                                             \
  } while (0)

#define POP(pointer, node)                                                                         \
  do {                                                                                             \
    assert((node) == *(pointer));                                                                  \
    *(pointer) = (node)->next;                                                                     \
    (node)->next = thread_freelist;                                                                \
    thread_freelist = (node);                                                                      \
  } while (0)

  const thread_state_t initial_state = {0};

  const char *eof = str + length;
  int prev_character = -1;

  match_result->matched = 0;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*(str++));

    memset(visited, 0, bitmap_size);

    PUSH(&thread_list_head, &initial_state);

    thread_list_t *iter = thread_list_head;
    thread_list_t **pointer = &thread_list_head;

    for (;;) {
      thread_state_t *state = &iter->state;
      size_t instruction_pointer = state->instruction_pointer;

      int keep = 1;

      for (;;) {
        if (instruction_pointer == regex->size) {
          match_result->matched = 1;
          break;
        }

        const size_t byte_index = instruction_pointer / CHAR_BIT;
        const size_t bit_index = instruction_pointer % CHAR_BIT;

        if (visited[byte_index] & (1u << bit_index)) {
          keep = 0;
          break;
        }

        visited[byte_index] |= 1u << bit_index;

        const unsigned char code = regex->bytecode[instruction_pointer++];

        if (code == VM_CHARACTER) {
          assert(instruction_pointer <= regex->size - 1);

          const unsigned char expected_character = regex->bytecode[instruction_pointer++];

          if (character != expected_character) {
            keep = 0;
          }

          break;
        }

        switch (code) {
        case VM_ANCHOR_BOF:
          if (prev_character != -1) {
            keep = 0;
          }
          break;

        case VM_ANCHOR_BOL:
          if (prev_character != -1 && prev_character != '\n') {
            keep = 0;
          }
          break;

        case VM_ANCHOR_EOF:
          if (character != -1) {
            keep = 0;
          }
          break;

        case VM_ANCHOR_EOL:
          if (character != -1 && character != '\n') {
            keep = 0;
          }
          break;

        case VM_ANCHOR_WORD_BOUNDARY:
          assert(0 && "FIXME");
          break;

        case VM_ANCHOR_NOT_WORD_BOUNDARY:
          assert(0 && "FIXME");
          break;

        case VM_JUMP: {
          assert(instruction_pointer <= regex->size - 4);

          const long delta = deserialize_long(regex->bytecode + instruction_pointer, 4);
          instruction_pointer += 4;

          instruction_pointer += delta;

          break;
        }

        case VM_SPLIT_PASSIVE:
        case VM_SPLIT_EAGER: {
          assert(instruction_pointer <= regex->size - 4);

          const long delta = deserialize_long(regex->bytecode + instruction_pointer, 4);
          instruction_pointer += 4;

          thread_state_t split_state;
          // FIXME: copy state

          if (code == VM_SPLIT_PASSIVE) {
            split_state.instruction_pointer = instruction_pointer + delta;
          } else {
            split_state.instruction_pointer = instruction_pointer;
            instruction_pointer += delta;
          }

          if (!((visited[split_state.instruction_pointer / CHAR_BIT] >>
                 (split_state.instruction_pointer % CHAR_BIT)) &
                1u)) {
            PUSH(&iter->next, &split_state);
          }

          break;
        }

        case VM_WRITE_POINTER: {
          // FIXME
          break;
        }

        default:
          assert(0);
        }

        if (!keep) {
          break;
        }
      }

      if (keep) {
        state->instruction_pointer = instruction_pointer;
        pointer = &iter->next;
        iter = iter->next;
      } else {
        POP(pointer, iter);
        iter = *pointer;
      }

      if (iter == NULL || match_result->matched) {
        break;
      }
    }

    if (character == -1 || match_result->matched) {
      break;
    }
  }

  free(visited);
  free(thread_list_buffer);

  return CREX_OK;
}

/** Public API **/

status_t crex_compile(crex_regex_t *regex, const char *pattern, size_t length) {
  parsetree_t *tree;
  status_t status = parse(&tree, pattern, length);

  if (status != CREX_OK) {
    return status;
  }

  status = compile(regex, tree);

  free_parsetree(tree);

  return status;
}

status_t crex_compile_str(crex_regex_t *regex, const char *pattern) {
  return crex_compile(regex, pattern, strlen(pattern));
}

void crex_free_regex(crex_regex_t *regex) { free(regex->bytecode); }

status_t crex_match(crex_match_result_t *result,
                    const crex_regex_t *regex,
                    const char *buffer,
                    size_t length) {
  return execute(result, regex, buffer, length);
}

status_t crex_match_str(crex_match_result_t *result, const crex_regex_t *regex, const char *str) {
  return crex_match(result, regex, str, strlen(str));
}

#ifdef CREX_DEBUG

#include <ctype.h>
#include <stdio.h>

const char *anchor_type_to_str(anchor_type_t type) {
  switch (type) {
  case AT_BOF:
    return "\\A";

  case AT_BOL:
    return "^";

  case AT_EOF:
    return "\\z";

  case AT_EOL:
    return "$";

  case AT_WORD_BOUNDARY:
    return "\\w";

  case AT_NOT_WORD_BOUNDARY:
    return "\\W";

  default:
    assert(0);
    return NULL;
  }
}

const char *status_to_str(status_t status) {
  switch (status) {
  case CREX_OK:
    return "CREX_OK";

  case CREX_E_NOMEM:
    return "CREX_E_NOMEM";

  case CREX_E_BAD_ESCAPE:
    return "CREX_E_BAD_ESCAPE";

  case CREX_E_UNMATCHED_OPEN_PAREN:
    return "CREX_E_UNMATCHED_OPEN_PAREN";

  case CREX_E_UNMATCHED_CLOSE_PAREN:
    return "CREX_E_UNMATCHED_CLOSE_PAREN";

  default:
    assert(0);
    return NULL;
  }
}

const char *crex_vm_code_to_str(unsigned char code) {
  switch (code) {
  case VM_CHARACTER:
    return "VM_CHARACTER";

  case VM_ANCHOR_BOF:
    return "VM_ANCHOR_BOF";

  case VM_ANCHOR_BOL:
    return "VM_ANCHOR_BOL";

  case VM_ANCHOR_EOF:
    return "VM_ANCHOR_EOF";

  case VM_ANCHOR_EOL:
    return "VM_ANCHOR_EOL";

  case VM_ANCHOR_WORD_BOUNDARY:
    return "VM_ANCHOR_WORD_BOUNDARY";

  case VM_ANCHOR_NOT_WORD_BOUNDARY:
    return "VM_ANCHOR_NOT_WORD_BOUNDARY";

  case VM_JUMP:
    return "VM_JUMP";

  case VM_SPLIT_PASSIVE:
    return "VM_SPLIT_PASSIVE";

  case VM_SPLIT_EAGER:
    return "VM_SPLIT_EAGER";

  case VM_WRITE_POINTER:
    return "VM_WRITE_POINTER";

  default:
    assert(0);
  }
}

void crex_debug_lex(const char *str, FILE* file) {
  const char *eof = str + strlen(str);

  token_t token;

  while (lex(&token, &str, eof)) {
    switch (token.type) {
    case TT_CHARACTER:
      if (isprint(token.data.character)) {
        fprintf(file, "TT_CHARACTER %c\n", token.data.character);
      } else {
        fprintf(file, "TT_CHARACTER 0x%02x\n", 0xff & (int)token.data.character);
      }
      break;

    case TT_ANCHOR:
      fprintf(file, "TT_ANCHOR %s\n", anchor_type_to_str(token.data.anchor_type));
      break;

    case TT_PIPE:
      fputs("TT_PIPE\n", file);
      break;

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      const char *str =
          (token.type == TT_GREEDY_REPETITION) ? "TT_GREEDY_REPETITION" : "TT_LAZY_REPETITION";

      fprintf(file, "%s %zu ... ", str, token.data.repetition.lower_bound);

      if (token.data.repetition.upper_bound == REPETITION_INFINITY) {
        fputs("inf\n", file);
      } else {
        fprintf(file, "%zu\n", token.data.repetition.upper_bound);
      }

      break;
    }

    case TT_OPEN_PAREN:
      fputs("TT_OPEN_PAREN\n", file);
      break;

    case TT_CLOSE_PAREN:
      fputs("TT_CLOSE_PAREN\n", file);
      break;

    default:
      assert(0);
    }
  }
}

static void crex_print_parsetree(const parsetree_t *tree, size_t depth, FILE* file) {
  for (size_t i = 0; i < depth; i++) {
    fputc(' ', file);
  }

  switch (tree->type) {
  case PT_EMPTY:
    fputs("(PT_EMPTY)", file);
    break;

  case PT_CHARACTER:
    if (isprint(tree->data.character)) {
      fprintf(file, "(PT_CHARACTER %c)", tree->data.character);
    } else {
      fprintf(file, "(PT_CHARACTER 0x%02x)", 0xff & (int)tree->data.character);
    }
    break;

  case PT_ANCHOR:
    fprintf(file, "(PT_ANCHOR %s)", anchor_type_to_str(tree->data.anchor_type));
    break;

  case PT_CONCATENATION:
    fputs("(PT_CONCATENATION\n", file);
    crex_print_parsetree(tree->data.children[0], depth + 1, file);
    fputc('\n', file);
    crex_print_parsetree(tree->data.children[1], depth + 1, file);
    fputc(')', file);
    break;

  case PT_ALTERNATION:
    fputs("(PT_ALTERNATION\n", file);
    crex_print_parsetree(tree->data.children[0], depth + 1, file);
    fputc('\n', file);
    crex_print_parsetree(tree->data.children[1], depth + 1, file);
    fputc(')', file);
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    const char *str =
        (tree->type == PT_GREEDY_REPETITION) ? "PT_GREEDY_REPETITION" : "PT_LAZY_REPETITION";

    fprintf(file, "(%s %zu ", str, tree->data.repetition.lower_bound);

    if (tree->data.repetition.upper_bound == REPETITION_INFINITY) {
      fputs("inf\n", file);
    } else {
      fprintf(file, "%zu\n", tree->data.repetition.upper_bound);
    }

    crex_print_parsetree(tree->data.repetition.child, depth + 1, file);
    fputc(')', file);

    break;
  }

  case PT_GROUP:
    fprintf(file, "(PT_GROUP %zu\n", tree->data.group.index);
    crex_print_parsetree(tree->data.group.child, depth + 1, file);
    fputc(')', file);
    break;

  default:
    assert(0);
  }
}

void crex_debug_parse(const char *str, FILE* file) {
  parsetree_t *tree;
  const status_t status = parse(&tree, str, strlen(str));

  if (status == CREX_OK) {
    crex_print_parsetree(tree, 0, file);
    fputc('\n', file);
  } else {
    fprintf(file, "Parse failed with status %s\n", status_to_str(status));
  }

  free_parsetree(tree);
}

void crex_debug_compile(const char *str, FILE* file) {
  parsetree_t *tree;
  status_t status = parse(&tree, str, strlen(str));

  if (status != CREX_OK) {
    fprintf(file, "Parse failed with status %s\n", status_to_str(status));
    return;
  }

  regex_t regex;
  status = compile(&regex, tree);

  free_parsetree(tree);

  if (status != CREX_OK) {
    fprintf(file, "Compilation failed with status %s\n", status_to_str(status));
    return;
  }

  /* for(size_t i = 0; i < regex.size; i++) {
   fprintf(stderr, "0x%02x\n", regex.bytecode[i]);
  } */

  for (size_t i = 0; i < regex.size; i++) {
    const unsigned char code = regex.bytecode[i];

    fprintf(file, "%05zd %s ", i, crex_vm_code_to_str(code));

    if (code == VM_JUMP || code == VM_SPLIT_PASSIVE || code == VM_SPLIT_EAGER) {
      const size_t origin = i + 1 + 4;
      const long delta = deserialize_long(regex.bytecode + i + 1, 4);
      const size_t destination = origin + delta;
      fprintf(file, "%ld (=> %zu)\n", delta, destination);
      i += 4;
    } else if (code == VM_CHARACTER) {
      fprintf(file, "%c\n", regex.bytecode[i + 1]);
      i++;
    } else if (code == VM_WRITE_POINTER) {
      fprintf(file, "%ld\n", deserialize_long(regex.bytecode + i + 1, 4)); // FIXME: signedness
      i += 4;
    } else {
      fputc('\n', file);
    }
  }

  fprintf(file, "%05zd\n ", regex.size);

  crex_free_regex(&regex);
}

#endif
