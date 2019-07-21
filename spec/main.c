#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../crex.h"
#include "types.h"

#define MAX_QUOTED_LENGTH 50000
#define MAX_TESTS 1000000

extern const size_t n_patterns;
extern const pattern_defn_t pattern_defns[];

extern const size_t n_cases;
extern const testcase_t testcases[];

static const char *inflect(size_t number) {
  switch (number % 10) {
  case 1:
    return "st";

  case 2:
    return "nd";

  case 3:
    return "rd";

  default:
    return "th";
  }
}

static void quote(char *result, const char *str, size_t length) {
  *(result++) = '"';

  for (size_t i = 0; i < length; i++) {
    if (str[i] == '\n') {
      *(result++) = '\\';
      *(result++) = 'n';
    } else if (str[i] == '"') {
      *(result++) = '\\';
      *(result++) = '"';
    } else if (isprint(str[i]) || str[i] == ' ') {
      *(result++) = str[i];
    } else {
      *(result++) = '\\';
      *(result++) = 'x';
      *(result++) = "0123456789abcdef"[(unsigned)str[i] / 16];
      *(result++) = "0123456789abcdef"[(unsigned)str[i] % 16];
    }
  }

  (*result++) = '"';

  (*result++) = 0;
}

#define RED(str_literal) "\x1b[31m" str_literal "\x1b[0m"

#define GREEN(str_literal) "\x1b[32m" str_literal "\x1b[0m"

int main(int argc, char **argv) {
  unsigned char bitmap[(MAX_TESTS + 7) / 8];

  if(argc != 1) {
    memset(bitmap, 0, (n_cases + 7) / 8);

    for(size_t i = 1; i < (size_t)argc; i ++) {
      const size_t j = (size_t)atol(argv[i]);

      assert(j < n_cases);

      bitmap[j >> 3u] |= 1u << (j & 7u);
    }
  } else {
    memset(bitmap, 0xff, (n_cases + 7) / 8);
  }

  crex_regex_t **regexes = malloc(sizeof(crex_regex_t *) * n_patterns);
  assert(regexes != NULL);

  for (size_t i = 0; i < n_patterns; i++) {
    regexes[i] = NULL;
  }

  size_t n_failures = 0;
  size_t *failures = malloc(sizeof(size_t) * n_cases);
  assert(failures != NULL);

  crex_status_t status;

  crex_context_t *context = crex_create_context(&status);
  assert(context != NULL);

  size_t run = 0;

  for (size_t i = 0; i < n_cases; i++) {
    if(!(bitmap[i >> 3u] & (1u << (i & 7u)))) {
      continue;
    }

    run ++;

    const testcase_t *testcase = &testcases[i];

    if (regexes[testcase->pattern_index] == NULL) {
      const size_t index = testcase->pattern_index;
      regexes[index] = crex_compile(&status, pattern_defns[index].str, pattern_defns[index].size);
      assert(regexes[index] != NULL);
    }

    const crex_regex_t *regex = regexes[testcase->pattern_index];
    const size_t n_groups = crex_regex_n_groups(regex);

    const char *pattern = pattern_defns[testcase->pattern_index].str;

    const char *str = testcase->str; // FIXME: escape this on output
    const char size = testcase->size;

    char quoted_str[MAX_QUOTED_LENGTH];
    quote(quoted_str, str, size);

    int is_match;
    crex_slice_t find_result;
    crex_slice_t groups[MAX_GROUPS];

    assert(crex_is_match(&is_match, context, regex, str, size) == CREX_OK);
    assert(crex_find(&find_result, context, regex, str, size) == CREX_OK);
    assert(crex_match_groups(groups, context, regex, str, size) == CREX_OK);

    if (testcase->is_match && !is_match) {
      printf(RED("%04zu: expected /%s/ to match %s, but it did not\n"), i, pattern, quoted_str);
      failures[n_failures++] = i;
      continue;
    }

    if (!testcase->is_match && is_match) {
      printf(RED("%04zu: expected /%s/ to not match %s, but it did\n"), i, pattern, quoted_str);
      failures[n_failures++] = i;
      continue;
    }

    if (!is_match) {
      assert(find_result.begin == NULL && find_result.end == NULL);

      for (size_t j = 0; j < n_groups; j++) {
        assert(groups[j].begin == NULL && groups[j].end == NULL);
      }

      printf(GREEN("%04zu: /%s/ does not match %s; okay\n"), i, pattern, quoted_str);

      continue;
    }

    assert(find_result.begin != NULL && find_result.end != NULL);
    assert(find_result.begin == groups[0].begin && find_result.end == groups[0].end);

    int okay = 1;

    for (size_t j = 0; j < n_groups; j++) {
      const crex_slice_t *slice = &groups[j];
      const crex_slice_t expected = {
        str + testcase->groups[j].begin,
        str + testcase->groups[j].end
      };

      if (slice->begin == expected.begin && slice->end == expected.end) {
        continue;
      }

      char quoted_slice[MAX_QUOTED_LENGTH];
      quote(quoted_slice, slice->begin, slice->end - slice->begin);

      char quoted_expectation[MAX_QUOTED_LENGTH];
      quote(quoted_expectation, expected.begin, expected.end - expected.begin);

      const size_t match_position = slice->begin - str;
      const size_t expected_position = expected.begin - str;

      printf(RED("%04zu: expected the %zu%s group of /%s/ "), i, j, inflect(j), pattern);

      if (slice->begin == NULL) {
        printf(RED("to match %s at position %zu, but it did not match\n"),
               quoted_expectation,
               expected_position);
      } else if (expected.begin == NULL) {
        printf(
            RED("to not match, but it matched %s at position %zu\n"), quoted_slice, match_position);
      } else {
        printf(RED("to match %s at position %zu, but it matched %s at position %zu\n"),
               quoted_expectation,
               expected_position,
               quoted_slice,
               match_position);
      }

      failures[n_failures++] = i;
      okay = 0;
      break;
    }

    if(okay) {
      printf(GREEN("%04zu: /%s/ matches %s; okay\n"), i, pattern, quoted_str);
    }
  }

  if (n_failures != 0) {
    printf("%zu of %zu test(s) failed. To rerun failed tests only:\n  %s ", n_failures, run, argv[0]);

    for (size_t i = 0; i < n_failures; i++) {
      printf("%zu%c", failures[i], (i == n_failures - 1) ? '\n' : ' ');
    }
  } else {
    printf("All %zu test(s) passed.\n", run);
  }

  for (size_t i = 0; i < n_patterns; i++) {
    if (regexes[i] == NULL) {
      continue;
    }
    crex_destroy_regex(regexes[i]);
  }

  free(regexes);

  free(failures);

  return (n_failures != 0);
}