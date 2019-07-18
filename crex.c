#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h> // FIXME

#include "crex.h"

typedef crex_status_t status_t;
typedef crex_regex_t regex_t;
typedef crex_context_t context_t;
#define WARN_UNUSED_RESULT CREX_WARN_UNUSED_RESULT

#define REPETITION_INFINITY (~(size_t)0)

static struct {
  const char *name;
  unsigned char bitmap[32];
} builtin_char_classes[] = {
    {"alnum", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff,
               0x07, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"alpha", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff,
               0x07, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"ascii", {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
               0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"blank", {0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"cntrl", {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"digit", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"graph", {0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
               0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"lower", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"print", {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
               0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"punct", {0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0x00, 0xfc, 0x01, 0x00, 0x00,
               0xf8, 0x01, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"space", {0x00, 0x3e, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"upper", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff,
               0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"word", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff,
              0x87, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"xdigit", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x7e, 0x00, 0x00,
                0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {NULL /* Non-digits */, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0xff, 0xff, 0xff,
                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    {NULL /* Non-whitespace */, {0xff, 0xc1, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    {NULL /* Non-word */, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0x01, 0x00, 0x00,
                           0x78, 0x01, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}};

#define BCC_DIGIT 5
#define BCC_WHITESPACE 10
#define BCC_WORD 12
#define BCC_NOT_DIGIT 14
#define BCC_NOT_WHITESPACE 15
#define BCC_NOT_WORD 16

// FIXME: put these somewhere

static void safe_memcpy(void *destination, const void *source, size_t size) {
  assert((destination != NULL && source != NULL) || size == 0);

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
  TT_BUILTIN_CHAR_CLASS,
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

    size_t char_class_index;

    anchor_type_t anchor_type;

    struct {
      size_t lower_bound, upper_bound;
    } repetition;
  } data;
} token_t;

WARN_UNUSED_RESULT static status_t lex(token_t *token, const char **str, const char *eof) {
  assert((*str) < eof);

  const char byte = *((*str)++);

  switch (byte) {
  case '^':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_BOL;
    break;

  case '$':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_EOL;
    break;

  case '|':
    token->type = TT_PIPE;
    break;

  case '*':
  case '+':
  case '?':
    switch (byte) {
    case '*':
      token->data.repetition.lower_bound = 0;
      token->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '+':
      token->data.repetition.lower_bound = 1;
      token->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '?':
      token->data.repetition.lower_bound = 0;
      token->data.repetition.upper_bound = 1;
      break;
    }

    if ((*str) < eof && *(*str) == '?') {
      (*str)++;
      token->type = TT_LAZY_REPETITION;
    } else {
      token->type = TT_GREEDY_REPETITION;
    }

    break;

  case '(':
    token->type = TT_OPEN_PAREN;
    break;

  case ')':
    token->type = TT_CLOSE_PAREN;
    break;

  case '\\':
    token->type = TT_CHARACTER;

    if ((*str) == eof) {
      return CREX_E_BAD_ESCAPE;
    }

    switch (*((*str)++)) {
    case 'a':
      token->data.character = '\a';
      break;

    case 'f':
      token->data.character = '\f';
      break;

    case 'n':
      token->data.character = '\n';
      break;

    case 'r':
      token->data.character = '\r';
      break;

    case 't':
      token->data.character = '\t';
      break;

    case 'v':
      token->data.character = '\v';
      break;

    case 'A':
      token->type = TT_ANCHOR;
      token->data.anchor_type = AT_BOF;
      break;

    case 'z':
      token->type = TT_ANCHOR;
      token->data.anchor_type = AT_EOF;
      break;

    case 'b':
      token->type = TT_ANCHOR;
      token->data.anchor_type = AT_WORD_BOUNDARY;
      break;

    case 'B':
      token->type = TT_ANCHOR;
      token->data.anchor_type = AT_NOT_WORD_BOUNDARY;
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

      token->data.character = value;

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
      token->data.character = byte;
      break;

    case 'd':
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = BCC_DIGIT;
      break;

    case 'D':
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = BCC_NOT_DIGIT;
      break;

    case 's':
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = BCC_WHITESPACE;
      break;

    case 'S':
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = BCC_NOT_WHITESPACE;
      break;

    case 'w':
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = BCC_WORD;
      break;

    case 'W':
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = BCC_NOT_WORD;
      break;

    default:
      return CREX_E_BAD_ESCAPE;
    }

    break;

  default:
    token->type = TT_CHARACTER;
    token->data.character = byte;
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
  PT_BUILTIN_CHAR_CLASS,
  PT_ANCHOR,
} parsetree_type_t;

/* FIXME: explain this */
static const size_t operator_precedence[] = {1, 0, 2, 2};

typedef struct parsetree {
  parsetree_type_t type;

  union {
    unsigned char character;

    size_t char_class_index;

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

WARN_UNUSED_RESULT status_t parse(parsetree_t **result,
                                  size_t *n_groups,
                                  const char *str,
                                  size_t length) {
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

  (*n_groups) = 1;

  while (str != eof) {
    token_t token;
    const status_t lex_status = lex(&token, &str, eof);

    CHECK_ERRORS(lex_status == CREX_OK, lex_status);

    switch (token.type) {
    case TT_CHARACTER:
    case TT_BUILTIN_CHAR_CLASS:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);

      parsetree_t *tree = malloc(sizeof(parsetree_t));
      CHECK_ERRORS(tree != NULL, CREX_E_NOMEM);

      switch (token.type) {
      case TT_CHARACTER:
        tree->type = PT_CHARACTER;
        tree->data.character = token.data.character;
        break;

      case TT_BUILTIN_CHAR_CLASS:
        tree->type = PT_BUILTIN_CHAR_CLASS;
        tree->data.char_class_index = token.data.char_class_index;
        break;

      case TT_ANCHOR:
        tree->type = PT_ANCHOR;
        tree->data.anchor_type = token.data.anchor_type;
        break;

      default:
        assert(0);
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
      operator.type =(token.type == TT_GREEDY_REPETITION) ? PT_GREEDY_REPETITION
                                                          : PT_LAZY_REPETITION;
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
      operator.data.group_index =(*n_groups)++;
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
    if (operators.data[operators.size - 1].type == PT_GROUP) {
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
  case PT_BUILTIN_CHAR_CLASS:
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
  VM_BUILTIN_CHAR_CLASS,
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

typedef struct {
  size_t size;
  unsigned char *bytecode;
} compilation_result_t;

static void serialize_long(unsigned char *destination, long value, size_t size) {
  assert(-2147483647 <= value && value <= 2147483647); // FIXME

  switch (size) {
  case 1: {
    const int8_t i8_value = value;
    safe_memcpy(destination, &i8_value, 1);
    break;
  }

  case 2: {
    const int16_t i16_value = value;
    safe_memcpy(destination, &i16_value, 2);
    break;
  }

  case 4: {
    const int32_t i32_value = value;
    safe_memcpy(destination, &i32_value, 4);
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
    safe_memcpy(&i8_value, source, 1);
    return i8_value;
  }

  case 2: {
    int16_t i16_value;
    safe_memcpy(&i16_value, source, 2);
    return i16_value;
  }

  case 4: {
    int32_t i32_value;
    safe_memcpy(&i32_value, source, 4);
    return i32_value;
  }

  default:
    assert(0);
    return 0;
  }
}

status_t compile(compilation_result_t *result, parsetree_t *tree) {
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

  case PT_BUILTIN_CHAR_CLASS:
    result->size = 5;
    result->bytecode = malloc(5);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_BUILTIN_CHAR_CLASS;
    serialize_long(result->bytecode + 1, (long)tree->data.char_class_index, 4);

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
    compilation_result_t left, right;

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
      safe_memcpy(result->bytecode, left.bytecode, left.size);
      safe_memcpy(result->bytecode + left.size, right.bytecode, right.size);
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

      safe_memcpy(bytecode + left_location, left.bytecode, left.size);

      bytecode[jump_location] = VM_JUMP;
      serialize_long(bytecode + jump_location + 1, jump_delta, 4);

      safe_memcpy(bytecode + right_location, right.bytecode, right.size);
    }

    free(left.bytecode);
    free(right.bytecode);

    break;
  }

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    compilation_result_t child;

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
      safe_memcpy(bytecode + i * child.size, child.bytecode, child.size);
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

      safe_memcpy(bytecode + offset + 1 + 4, child.bytecode, child.size);

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

        safe_memcpy(bytecode + offset + 1 + 4, child.bytecode, child.size);
      }
    }

    free(child.bytecode);

    break;
  }

  case PT_GROUP: {
    compilation_result_t child;
    status_t status = compile(&child, tree->data.group.child);

    if (status != CREX_OK) {
      return status;
    }

    result->size = 1 + 4 + child.size + 1 + 4;

    result->bytecode = malloc(result->size);

    if (result->bytecode == NULL) {
      free(child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;

    // FIXME: signedness

    bytecode[0] = VM_WRITE_POINTER;
    serialize_long(bytecode + 1, (long)(2 * tree->data.group.index), 4);

    safe_memcpy(bytecode + 1 + 4, child.bytecode, child.size);

    const size_t offset = 1 + 4 + child.size;
    bytecode[offset] = VM_WRITE_POINTER;
    serialize_long(bytecode + offset + 1, (long)(2 * tree->data.group.index + 1), 4);

    free(child.bytecode);

    break;
  }

  default:
    assert(0);
  }

  return CREX_OK;
}

/** Instruction pointer (i.e. size_t) list with custom allocator **/

typedef size_t state_list_handle_t;
typedef state_list_handle_t *state_list_iter_t;

#define STATE_LIST_EMPTY (~(state_list_handle_t)0)

/* typedef union {
  size_t size;
  char *pointer;
} slot_t; */ // FIXME

typedef struct {
  size_t capacity;
  slot_t *buffer;

  size_t n_pointers;
  size_t element_size;
  size_t max_capacity;

  size_t bump_allocator;
  state_list_handle_t freelist;

  state_list_handle_t head;
} state_list_t;

#define LIST_NEXT(list, handle) (*(size_t *)((list)->buffer + (handle)))
#define LIST_INSTR_POINTER(list, handle) (*(size_t *)((list)->buffer + (handle) + 1))
#define LIST_POINTER_BUFFER(list, handle) ((char **)((list)->buffer + (handle) + 2))

static void state_list_create(
    state_list_t *list, size_t capacity, slot_t *buffer, size_t n_pointers, size_t max_elements) {
  list->capacity = capacity;
  list->buffer = buffer;

  list->n_pointers = n_pointers;
  list->element_size = 2 + n_pointers;
  list->max_capacity = max_elements * list->element_size;

  list->bump_allocator = 0;
  list->freelist = STATE_LIST_EMPTY;

  list->head = STATE_LIST_EMPTY;
}

static state_list_handle_t state_list_alloc(state_list_t *list) {
  state_list_handle_t state;

#ifndef NDEBUG
  state = STATE_LIST_EMPTY;
#endif

  if (list->bump_allocator + list->element_size <= list->capacity) {
    state = list->bump_allocator;
    list->bump_allocator += list->element_size;
  } else if (list->freelist != STATE_LIST_EMPTY) {
    state = list->freelist;
    list->freelist = LIST_NEXT(list, state);
  } else {
    // FIXME: think about a better resize strategy?
    size_t capacity = 2 * list->capacity + list->element_size;

    if (capacity > list->max_capacity) {
      capacity = list->max_capacity;
    }

    slot_t *buffer = malloc(sizeof(slot_t) * capacity);

    if (buffer == NULL) {
      return STATE_LIST_EMPTY;
    }

    safe_memcpy(buffer, list->buffer, sizeof(slot_t) * list->capacity);
    free(list->buffer);

    list->buffer = buffer;
    list->capacity = capacity;

    assert(list->bump_allocator + list->element_size <= capacity);

    state = list->bump_allocator;
    list->bump_allocator += list->element_size;
  }

  assert(state + list->element_size <= list->capacity && state % list->element_size == 0);

  return state;
}

static int state_list_push_initial_state(state_list_t *list, state_list_handle_t predecessor) {
  assert(predecessor < list->capacity || predecessor == STATE_LIST_EMPTY);

  const state_list_handle_t state = state_list_alloc(list);

  if (state == STATE_LIST_EMPTY) {
    return 0;
  }

  LIST_INSTR_POINTER(list, state) = 0;

  char **pointer_buffer = LIST_POINTER_BUFFER(list, state);

  for (size_t i = 0; i < list->n_pointers; i++) {
    pointer_buffer[i] = NULL;
  }

  if (predecessor == STATE_LIST_EMPTY) {
    LIST_NEXT(list, state) = list->head;
    list->head = state;
  } else {
    LIST_NEXT(list, state) = LIST_NEXT(list, predecessor);
    LIST_NEXT(list, predecessor) = state;
  }

  return 1;
}

static int
state_list_push_copy(state_list_t *list, state_list_handle_t predecessor, size_t instr_pointer) {
  assert(predecessor < list->capacity);

  const state_list_handle_t state = state_list_alloc(list);

  if (state == STATE_LIST_EMPTY) {
    return 0;
  }

  LIST_INSTR_POINTER(list, state) = instr_pointer;

  char **dest_pointer_buffer = LIST_POINTER_BUFFER(list, state);
  char **source_pointer_buffer = LIST_POINTER_BUFFER(list, predecessor);
  safe_memcpy(dest_pointer_buffer, source_pointer_buffer, sizeof(char *) * list->n_pointers);

  LIST_NEXT(list, state) = LIST_NEXT(list, predecessor);
  LIST_NEXT(list, predecessor) = state;

  return 1;
}

static state_list_handle_t state_list_pop(state_list_t *list, state_list_handle_t predecessor) {
  assert(predecessor < list->capacity || predecessor == STATE_LIST_EMPTY);

  const state_list_handle_t state =
      (predecessor == STATE_LIST_EMPTY) ? list->head : LIST_NEXT(list, predecessor);
  assert(state < list->capacity);

  const state_list_handle_t successor = LIST_NEXT(list, state);

  if (predecessor == STATE_LIST_EMPTY) {
    list->head = successor;
  } else {
    LIST_NEXT(list, predecessor) = successor;
  }

  LIST_NEXT(list, state) = list->freelist;
  list->freelist = state;

  return successor;
}

#define MATCH_BOOLEAN
#include "executor.h"

#define MATCH_LOCATION
#include "executor.h"

#define MATCH_GROUPS
#include "executor.h"

/** Public API **/

status_t crex_compile(crex_regex_t *regex, const char *pattern, size_t length) {
  parsetree_t *tree;
  status_t status = parse(&tree, &regex->n_groups, pattern, length);

  if (status != CREX_OK) {
    return status;
  }

  // compilation_result_t is a prefix of regex
  compilation_result_t *result = (compilation_result_t *)regex;

  status = compile(result, tree);

  assert(regex->size == result->size && regex->bytecode == result->bytecode);

  free_parsetree(tree);

  return status;
}

status_t crex_compile_str(crex_regex_t *regex, const char *pattern) {
  return crex_compile(regex, pattern, strlen(pattern));
}

void crex_create_context(crex_context_t *context) {
  context->visited_size = 0;
  context->visited = NULL;

  context->list_buffer_size = 0;
  context->list_buffer = NULL;
}

void crex_free_regex(crex_regex_t *regex) { free(regex->bytecode); }

void crex_free_context(crex_context_t *context) {
  free(context->visited);
  free(context->list_buffer);
}

/* status_t crex_is_match(
    int *is_match, context_t *context, const regex_t *regex, const char *buffer, size_t length) {
  return execute(is_match, context, regex, buffer, length);
} */

status_t
crex_is_match_str(int *is_match, context_t *context, const regex_t *regex, const char *str) {
  return crex_is_match(is_match, context, regex, str, strlen(str));
}

status_t
crex_find_str(crex_slice_t *match, context_t *context, const regex_t *regex, const char *str) {
  return crex_find(match, context, regex, str, strlen(str));
}

status_t crex_match_groups_str(crex_slice_t *matches,
                               context_t *context,
                               const regex_t *regex,
                               const char *str) {
  return crex_match_groups(matches, context, regex, str, strlen(str));
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

  case VM_BUILTIN_CHAR_CLASS:
    return "VM_BUILTIN_CHAR_CLASS";

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

void crex_debug_lex(const char *str, FILE *file) {
  const char *eof = str + strlen(str);

  token_t token;

  while (str != eof) {
    const status_t status = lex(&token, &str, eof);

    if (status != CREX_OK) {
      fprintf(file, "Lex failed with status %s\n", status_to_str(status));
      return;
    }

    switch (token.type) {
    case TT_CHARACTER:
      fputs("TT_CHARACTER ", file);

      if (isprint(token.data.character)) {
        fputc(token.data.character, file);
      } else {
        fprintf(file, "0x%02x\n", 0xff & (int)token.data.character);
      }

      fputc('\n', file);

      break;

    case TT_BUILTIN_CHAR_CLASS: {
      const char *name = builtin_char_classes[token.data.char_class_index].name;

      fputs("TT_BUILTIN_CHAR_CLASS ", file);

      if (name != NULL) {
        fprintf(file, "[[:%s:]]\n", name);
      } else {
        fprintf(file, "%zu\n", token.data.char_class_index);
      }

      break;
    }

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

static void crex_print_parsetree(const parsetree_t *tree, size_t depth, FILE *file) {
  for (size_t i = 0; i < depth; i++) {
    fputc(' ', file);
  }

  switch (tree->type) {
  case PT_EMPTY:
    fputs("(PT_EMPTY)", file);
    break;

  case PT_CHARACTER:
    fputs("(PT_CHARACTER ", file);

    if (isprint(tree->data.character)) {
      fprintf(file, "%c)", tree->data.character);
    } else {
      fprintf(file, "0x%02x)", 0xff & (int)tree->data.character);
    }

    break;

  case PT_BUILTIN_CHAR_CLASS: {
    const char *name = builtin_char_classes[tree->data.char_class_index].name;

    fputs("(PT_BUILTIN_CHAR_CLASS ", file);

    if (name != NULL) {
      fprintf(file, "[[:%s:]])", name);
    } else {
      fprintf(file, "%zu)", tree->data.char_class_index);
    }

    break;
  }

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

void crex_debug_parse(const char *str, FILE *file) {
  parsetree_t *tree;
  size_t n_groups;
  const status_t status = parse(&tree, &n_groups, str, strlen(str));

  if (status == CREX_OK) {
    crex_print_parsetree(tree, 0, file);
    fputc('\n', file);
  } else {
    fprintf(file, "Parse failed with status %s\n", status_to_str(status));
  }

  free_parsetree(tree);
}

void crex_debug_compile(const char *str, FILE *file) {
  parsetree_t *tree;
  size_t n_groups;
  status_t status = parse(&tree, &n_groups, str, strlen(str));

  if (status != CREX_OK) {
    fprintf(file, "Parse failed with status %s\n", status_to_str(status));
    return;
  }

  compilation_result_t result;
  status = compile(&result, tree);

  free_parsetree(tree);

  if (status != CREX_OK) {
    fprintf(file, "Compilation failed with status %s\n", status_to_str(status));
    return;
  }

  /* for(size_t i = 0; i < regex.size; i++) {
   fprintf(stderr, "0x%02x\n", regex.bytecode[i]);
  } */

  for (size_t i = 0; i < result.size; i++) {
    const unsigned char code = result.bytecode[i];

    fprintf(file, "%05zd %s ", i, crex_vm_code_to_str(code));

    if (code == VM_JUMP || code == VM_SPLIT_PASSIVE || code == VM_SPLIT_EAGER) {
      const size_t origin = i + 1 + 4;
      const long delta = deserialize_long(result.bytecode + i + 1, 4);
      const size_t destination = origin + delta;
      fprintf(file, "%ld (=> %zu)\n", delta, destination);
      i += 4;
    } else if (code == VM_CHARACTER) {
      fprintf(file, "%c\n", result.bytecode[i + 1]);
      i++;
    } else if (code == VM_WRITE_POINTER || code == VM_BUILTIN_CHAR_CLASS) {
      fprintf(file, "%ld\n", deserialize_long(result.bytecode + i + 1, 4)); // FIXME: signedness
      i += 4;
    } else {
      fputc('\n', file);
    }
  }

  fprintf(file, "%05zd\n ", result.size);

  free(result.bytecode);
}

#endif
