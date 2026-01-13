/**
 * @file lexer.c
 * @brief Lexical analyzer (tokenizer) implementation
 */

// #include "../../include/tokens.h"
#include "../include/cup.h"
#include <assert.h>
#include <string.h>

/* ========================================================================== */
/*                         CHARACTER CLASSIFICATION                           */
/* ========================================================================== */

/**
 * @brief Check if character is valid in identifier
 * @param c Character to check
 * @return true if valid identifier character
 */
static inline int isID(const unsigned char c) {
  return ((c > '/') && (c < ':' || c > '@') && (c < '[' || c > '^') &&
          (c < '{' || c > 0x7F));
}

/**
 * @brief Find index of character in string
 * @param str String to search
 * @param c Character to find
 * @return Index of character
 */
static size_t strindex(const char *str, const char c) {
  const char *ptr = str;
  while (*ptr != c)
    ptr++;
  return ptr - str;
}

/* ========================================================================== */
/*                         LOCATION TRACKING                                  */
/* ========================================================================== */

static int newlineChar(const char *stream, Loc *next) {
  const char *s = stream;
  if (*stream == '\r') {
    if (stream[1] == '\n')
      stream++;
    goto newline;
  }
  if (*stream == '\n' || *stream == '\r') {
  newline:
    next->line++;
    next->col = 1;
    stream++;
  }
  return stream - s;
}

static void nextChar(CUPState *state, Loc *next) {
  int i = newlineChar(state->input_stream, next);
  if (i == 0) {
    next->col++;
    i = 1;
  }
  state->input_stream += i;
}

/* ========================================================================== */
/*                         NUMBER PARSING                                     */
/* ========================================================================== */

const char *binaryChar(CUPState *state, const char *p) {
  size_t value = 0;
  for (; (*p == '0' || *p == '1') || *p == '_'; ++p)
    if (*p != '_')
      value = (value << 1) | (*p & 1);
  state->value = value;
  state->loc.col += (p - state->input_stream);
  return p;
}

const char *octalChar(CUPState *state, const char *p) {
  size_t value = 0;
  for (; (*p >= '0' && *p <= '7') || *p == '_'; ++p)
    if (*p != '_')
      value = (value << 3) | (*p & 7);
  state->value = value;
  state->loc.col += (p - state->input_stream);
  return p;
}

const char *hexChar(CUPState *state, const char *p) {
  size_t value = 0;
  for (;; ++p) {
    if (*p == '_')
      continue;
    if (*p >= 'a' && *p <= 'f')
      value = (value << 4) | (*p - 87);
    else if (*p >= 'A' && *p <= 'F')
      value = (value << 4) | (*p - 55);
    else if (*p >= '0' && *p <= '9')
      value = (value << 4) | (*p - '0');
    else
      break;
  }
  state->value = value;
  state->node.loc.col += (p - state->input_stream);
  return p;
}

/**
 * @brief Parse number with prefix (0b, 0o, 0x)
 * @param state Compiler state
 * @return true if prefix number parsed successfully
 */
inline int zeroChar(CUPState *state, const char *p) {
  const char f = *p | 32;

  if (f == 'b') { /* Binary: 0b... */
    state->input_stream = binaryChar(state, ++p);
    return 1;
  }
  if (f == 'o') { /* Octal: 0o... */
    state->input_stream = octalChar(state, ++p);
    return 1;
  }
  if (f == 'x') { /* Hexadecimal: 0x... */
    state->input_stream = hexChar(state, ++p);
    return 1;
  }

  return 0;
}

/* ========================================================================== */
/*                         TOKEN EXTRACTION                                   */
/* ========================================================================== */

/**
 * @brief Get next token from input stream
 * @param state Compiler state
 */
void getToken(CUPState *state, Loc *next) {
  /* Operator tables for quick lookup */
  static const CTType types1[] = {T_ADD, T_SUB,  T_MUL,   T_DIV, T_MOD, T_AND,
                                  T_OR,  T_LESS, T_GREAT, T_XOR, T_NOT, T_EQ};
  static const CTType doubles[] = {T_ADDEQ,   T_SUBEQ, T_MULEQ, T_DIVEQ,
                                   T_MODEQ,   T_ANDEQ, T_OREQ,  T_LESSEQ,
                                   T_GREATEQ, T_XOREQ, T_NOTEQ, T_EQEQ};
  static const CTType types2[] = {
      T_ORB,   T_CRB,    T_OCB,    T_CCB, T_OSB, T_CSB,  T_DOT,   T_COMMA,
      T_COLON, T_SCOLON, T_IMPORT, T_ASK, T_AT,  T_THIS, T_CATNL, T_EXTERNAL};

  state->value = 0;
  assert(next != 0);

  /* Skip whitespace (except newlines) */
  const char *st = state->input_stream;
  while (*state->input_stream == ' ' || *state->input_stream == '\t')
    state->input_stream++;
  next->col += (state->input_stream - st);
  state->loc = *next;
  char input_char = *state->input_stream;

  /* End of file */
  if (input_char == '\0') {
    state->type = T_EOF;
    return;
  }

  /* Newline */
  {
    int i = newlineChar(state->input_stream, next);
    if (i) {
      state->input_stream += i;
      state->type = T_NL;
      return;
    }
  }

  /* String literals */
  if (input_char == '"') {
    next->col++;
    state->loc.col++;
    const char *temp = ++state->input_stream;
    while (*state->input_stream != '\0' && *state->input_stream != '"') {
      nextChar(state, next);
    }
    if (*state->input_stream == '"')
      state->input_stream++;
    size_t len = (state->input_stream - temp) - 1;
    /* Check if string already exists in data pool */
    if (state->data_size > len) {
      const size_t size = state->data_size - len;
      for (size_t i = state->data_size - len; i >= 0; i--) {
        if (memcmp(state->data + i, temp, len) == 0) {
          state->value = i;
          state->type = T_STRING;
          return;
        }
      }
    }

    /* Add new string to pool */
    state->data =
        (uint8_t *)state->reallocator(state->data, state->data_size + len + 1);
    memcpy(state->data + state->data_size, temp, len);
    state->data[state->data_size + len] = '\0';
    state->data_size += len + 1;
    state->type = T_STRING;
    return;
  }

  /* Numbers */
  if (input_char == '0' && zeroChar(state, state->input_stream + 1)) {
    state->loc = start_loc;
    state->type = T_NUMBER;
    return;
    /* Fall through to decimal parsing */
  }

  if (input_char >= '0' && input_char <= '9') {
    while ((*state->input_stream >= '0' && *state->input_stream <= '9') ||
           *state->input_stream == '_') {
      if (*state->input_stream != '_')
        state->value = state->value * 10 + (*state->input_stream - '0');
      nextChar(*state->input_stream++, &state->loc);
    }
    state->loc = start_loc;
    state->type = T_NUMBER;
    return;
  }

  /* Operators that can be doubled (+=, ==, etc.) */
  if (strchr("+-*/%&|<>^!=", input_char)) {
    input_char = strindex("+-*/%&|<>^!=", input_char);
    nextChar(*state->input_stream++, &state->loc);
    if (*state->input_stream != '=') {
      state->loc = start_loc;
      state->type = types1[(uint8_t)input_char];
      return;
    }
    nextChar(*state->input_stream++, &state->loc);
    state->loc = start_loc;
    state->type = doubles[(uint8_t)input_char];
    return;
  }

  /* Dot and range operator */
  if (input_char == '.') {
    nextChar(*state->input_stream++, &state->loc);
    if (*state->input_stream == '.') {
      if (state->input_stream[1] == '.') {
        nextChar(*state->input_stream++, &state->loc);
        nextChar(*state->input_stream++, &state->loc);
        state->loc = start_loc;
        state->type = T_VARG;
        return;
      }
    }
    state->loc = start_loc;
    state->type = T_DOT;
    return;
  }

  /* Single-character delimiters */
  if (strchr("(){}[].,:;#?@$\\~", input_char)) {
    input_char = strindex("(){}[].,:;#?@$\\~", input_char);
    nextChar(*state->input_stream++, &state->loc);
    state->loc = start_loc;
    state->type = types2[(uint8_t)input_char];
    return;
  }

  /* Keywords and identifiers */
  const char *temp = state->input_stream;
  do {
    nextChar(*state->input_stream++, &state->loc);
  } while (isID(*state->input_stream));
  size_t len = state->input_stream - temp;
  state->loc = start_loc;

  /* Check for keywords */
  if (len == 2 && memcmp(temp, "if", 2) == 0) {
    state->type = T_IF;
    return;
  }
  if (len == 3 && memcmp(temp, "for", 3) == 0) {
    state->type = T_FOR;
    return;
  }
  if (len == 4 && memcmp(temp, "else", 4) == 0) {
    state->type = T_ELSE;
    return;
  }
  if (len == 5 && memcmp(temp, "break", 5) == 0) {
    state->type = T_BREAK;
    return;
  }
  if (len == 5 && memcmp(temp, "while", 5) == 0) {
    state->type = T_WHILE;
    return;
  }
  if (len == 6 && memcmp(temp, "return", 6) == 0) {
    state->type = T_RETURN;
    return;
  }
  if (len == 8 && memcmp(temp, "continue", 8) == 0) {
    state->type = T_CONTINUE;
    return;
  }

  /* It's an identifier - intern it */
  idToken(state, temp, len);
  state->type = T_IDENTIFIER;
}

/**
 *  * @brief Skip whitespace and get next token
 *   * @param state Compiler state
 *    */
void skipSpaces(CUPState *state) {
  while (*state->input_stream == ' ' || *state->input_stream == '\t' ||
         *state->input_stream == '\n' || *state->input_stream == '\r')
    nextChar(*state->input_stream++, &state->loc);
  getToken(state);
}

/* ========================================================================== */
/*                         STRING INTERNING                                   */
/* ========================================================================== */

/**
 *  * @brief Get string from interned string table
 *   * @param state Compiler state
 *    * @param index String index
 *     * @return Pointer to string or empty string if not found
 *      */
const char *getString(CUPState *state, size_t index) {
  if (state->names == NULL || index == SIZE_MAX)
    return "";

  /* Find the start sentinel [0, 0] */
  uint8_t *ptr = state->names - 1;
  while (ptr > state->names - 1000) { /* safety limit */
    if (ptr[0] == '\0' && ptr[-1] == '\0') {
      ptr++; /* Move to position after [0, 0] */
      break;
    }
    ptr--;
  }

  /* Walk forward and count strings */
  size_t current_index = 0;
  while (ptr < state->names) {
    if (*ptr == '\0') {
      ptr++;
      continue;
    }
    if (current_index == index)
      return (const char *)ptr;
    while (*ptr++)
      ;
    current_index++;
  }

  return NULL;
}

/**
 *  * @brief Intern identifier string
 *   * @param state Compiler state
 *    * @param str String to intern
 *     * @param len String length
 *      * @return Pointer to interned string
 *       */
uint8_t *idToken(CUPState *state, const char *str, size_t len) {
  uint8_t *current = state->names - 1;
  state->value = 0;
  uint8_t *keyword = NULL;

  for (size_t l = 0;; current--) {
    if (current[-1] != '\0') {
      /* Found [non-0, X] continue to found [0, non-0](strings_start) */
      l++;
      continue;
    }
    if (*current == '\0') {
      /* Found [0, 0] sentinel so keyword not found */
      current--;
      if (keyword == NULL) {
        const size_t size = state->names - current;
        state->names = (uint8_t *)state->reallocator(current, size + len + 1);
        state->names += size;
        keyword = state->names;
        memcpy(state->names, str, len);
        state->names[len] = '\0';
        state->names += len + 1;
      }
      return keyword;
    }
    state->value++;
    if (keyword == NULL) {
      /* Found [0, non-0] strings_start */
      if (l == len && memcmp(str, current, l) == 0) {
        /* Match found and we need go to start */
        keyword = current;
        state->value = 0;
      } else {
        /* No match, continue to next keyword in backward order */
        l = 0;
      }
    }
  }
}
