/**
 * @file lexer.c
 * @brief Lexical analyzer (tokenizer) implementation
 */

#include "../include/cup.h"
#include <assert.h>

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
 * @return Index of character or -1 on end
 */
static int strindex(const char *str, const char c) {
  for (const char *ptr = str; *ptr; ptr++)
    if (*ptr == c) return ptr - str;
  return -1;
}

/* ========================================================================== */
/*                         STRING INTERNING                                   */
/* ========================================================================== */

const uint8_t *getData(CUPState *state, size_t i) {
  return state->data + i;
}

const uint8_t* getdata(CUPState* state, const char* str, size_t len) {
  if (state->data_size < len) return 0;
  for (uint8_t* i = state->data + state->data_size - len; i >= state->data; i--) {
    if (memcmp(i, str, len) == 0)
      return i;
  }
  return 0;
}

/**
 * @brief Get string from interned string table
 * @param state Compiler state
 * @param index String index
 * @return Pointer to string or empty string if not found
 */
const char *getString(CUPState *state, size_t index) {
  if (state == NULL || state->names == NULL || index == SIZE_MAX)
    return NULL;

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
    while (*ptr++);
    current_index++;
  }

  return NULL;
}

/**
 * @brief Intern identifier string
 * @param state Compiler state
 * @param str String to intern
 * @param len String length
 * @return Pointer to interned string
 */
uint8_t *idToken(CUPState *state, const char *str, size_t len) {
  if (len == 2 && memcmp(str, "if", 2) == 0) {
    state->nodes.token = T_IF;
    return NULL;
  }
  if (len == 3 && memcmp(str, "for", 3) == 0) {
    state->nodes.token = T_FOR;
    return NULL;
  }
  if (len == 4 && memcmp(str, "else", 4) == 0) {
    state->nodes.token = T_ELSE;
    return NULL;
  }
  if (len == 5 && memcmp(str, "break", 5) == 0) {
    state->nodes.token = T_BREAK;
    return NULL;
  }
  if (len == 5 && memcmp(str, "while", 5) == 0) {
    state->nodes.token = T_WHILE;
    return NULL;
  }
  if (len == 6 && memcmp(str, "return", 6) == 0) {
    state->nodes.token = T_RETURN;
    return NULL;
  }
  if (len == 8 && memcmp(str, "continue", 8) == 0) {
    state->nodes.token = T_CONTINUE;
    return NULL;
  }
  uint8_t *current = state->names - 1;
  state->nodes.value = 0;
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
        state->names = (uint8_t *)cup_realloc(current, size + len + 1);
        state->names += size;
        keyword = state->names;
        memcpy(state->names, str, len);
        state->names[len] = '\0';
        state->names += len + 1;
      }
      state->nodes.token = T_IDENTIFIER;
      return keyword;
    }
    state->nodes.value++;
    if (keyword == NULL) {
      /* Found [0, non-0] strings_start */
      if (l == len && memcmp(str, current, l) == 0) {
        /* Match found and we need go to start */
        keyword = current;
        state->nodes.value = 0;
      } else {
        /* No match, continue to next keyword in backward order */
        l = 0;
      }
    }
  }
}

/* ========================================================================== */
/*                         LOCATION TRACKING                                  */
/* ========================================================================== */

static int newlineChar(const char *stream) {
  const char *s = stream;
  if (*stream == '\r')
    stream++;
  if (*stream == '\n')
    stream++;
  return stream - s;
}

static void nextChar(const char **stream, Loc *next) {
  int i = newlineChar(*stream);
  if (i == 0) {
    next->col++;
    i = 1;
  } else {
    next->line++;
    next->col = 1;
  }
  *stream += i;
}

/* ========================================================================== */
/*                         NUMBER PARSING                                     */
/* ========================================================================== */

const char *binaryChar(CUPState *state, const char *p) {
  size_t value = 0;
  for (; (*p == '0' || *p == '1') || *p == '_'; ++p)
    if (*p != '_')
      value = (value << 1) | (*p & 1);
  state->nodes.value = value;
  return p;
}

const char *octalChar(CUPState *state, const char *p) {
  size_t value = 0;
  for (; (*p >= '0' && *p <= '7') || *p == '_'; ++p)
    if (*p != '_')
      value = (value << 3) | (*p & 7);
  state->nodes.value = value;
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
  state->nodes.value = value;
  return p;
}

/**
 * @brief Parse number with prefix (0b, 0o, 0x)
 * @param state Compiler state
 * @return true if prefix number parsed successfully
 */
int zeroChar(CUPState *state, const char *p) {
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

size_t numberChar(CUPState* state) {
  size_t value = 0;
  for (;(*state->input_stream >= '0' && *state->input_stream <= '9') ||
           *state->input_stream == '_';state->input_stream++) {
    if (*state->input_stream == '_') continue;
    value = value * 10 + (*state->input_stream - '0');
  }
  return value;
}

/* ========================================================================== */
/*                         TOKEN EXTRACTION                                   */
/* ========================================================================== */


  static const CTType types1[] = {T_ADD, T_SUB,  T_MUL,   T_DIV, T_MOD, T_AND,
                                  T_OR,  T_LESS, T_GREAT, T_XOR, T_NOT, T_EQ};
  static const CTType doubles[] = {T_ADDEQ,   T_SUBEQ, T_MULEQ, T_DIVEQ,
                                   T_MODEQ,   T_ANDEQ, T_OREQ,  T_LESSEQ,
                                   T_GREATEQ, T_XOREQ, T_NOTEQ, T_EQEQ};
  static const CTType types2[] = {
      T_CALL,   T_CRB,    T_OCB,    T_CCB, T_OSB, T_CSB,  T_DOT,   T_COMMA,
      T_COLON, T_SCOLON, T_IMPORT, T_ASK, T_AT,  T_THIS, T_CATNL, T_EXTERNAL};

/**
 * @brief Get next token from input stream
 * @param state Compiler state
 */
void getToken(CUPState *state) {
  /* Operator tables for quick lookup */

  state->nodes.value = 0;

  if (state->priv_stream == NULL)
    state->priv_stream = state->input_stream;

  while (state->priv_stream != state->input_stream)
    nextChar(&state->priv_stream, &state->nodes.node.loc);

  union {
    int i;
    const char* temp;
  } u;
  
  for (u.temp = state->input_stream;
    *state->input_stream == ' ' || *state->input_stream == '\t';
    state->input_stream++); 
  state->nodes.node.loc.col += (state->input_stream - u.temp);
  state->priv_stream = state->input_stream;
  char input_char = *state->input_stream;

  /* End of file */
  if (input_char == '\0') {
    state->nodes.token = T_EOF;
    return;
  }

  /* String literals */
  if (input_char == '"') {
    state->nodes.token = T_STRING;
    state->nodes.node.loc.col++;
    u.temp = ++state->input_stream;
    while (*state->input_stream != '\0' && *state->input_stream != '"') {
      state->input_stream++;
    }
    size_t len = state->input_stream - u.temp;
    if (*state->input_stream == '"')
      state->input_stream++;
    const uint8_t* i = getdata(state, u.temp, len);
    if (i) {
      state->nodes.value = i - state->data;
      return;
    }
    state->data = (uint8_t*)cup_realloc(state->data, state->data_size + len + 1);
    memcpy(state->data + state->data_size, u.temp, len);
    state->data[state->data_size + len] = '\0';
    state->nodes.value = state->data_size;
    state->data_size += len + 1;
    return;
  }
  /* Numbers */
  if (input_char == '0' && zeroChar(state, state->input_stream + 1)) {
    state->nodes.token = T_NUMBER;
    return;
  }
  if (input_char >= '0' && input_char <= '9') {
    state->nodes.token = T_NUMBER;
    state->nodes.value = numberChar(state);
    return;
  }
  /* Dot and range operator */
  if (input_char == '.') {
    if (*(++state->input_stream) == '.') {
      if (state->input_stream[1] == '.') {
        state->input_stream += 2;
        state->nodes.token = T_VARG;
        return;
      }
    }
    state->nodes.token = T_DOT;
    return;
  }
  /* Newline */
  u.i = newlineChar(state->input_stream);
  if (u.i) {
    state->input_stream += u.i;
    state->nodes.token = T_NL;
    return;
  }
  /* Operators that can be doubled (+=, ==, etc.) */
  u.i = strindex("+-*/%&|<>^!=", input_char);
  if (u.i != -1) {
    if (*(++state->input_stream) != '=') {
      state->nodes.token = types1[u.i];
      return;
    }
    state->nodes.token = doubles[u.i];
    return;
  }
  /* Single-character delimiters */
  u.i = strindex("(){}[].,:;#?@$\\~", input_char);
  if (u.i != -1) {
    state->input_stream++;
    state->nodes.token = types2[u.i];
    return;
  }
  /* Keywords and identifiers */
  u.temp = state->input_stream;
  for (state->input_stream++; isID(*state->input_stream); state->input_stream++);
  size_t len = state->input_stream - u.temp;

  /* It's an identifier - intern it */
  idToken(state, u.temp, len);
}

/**
 * @brief Skip whitespace and get next token
 * @param state Compiler state
 */
void skipSpaces(CUPState *state) {
  while (*state->input_stream == ' ' || *state->input_stream == '\t' ||
         *state->input_stream == '\n' || *state->input_stream == '\r')
    nextChar(&state->input_stream, &state->nodes.node.loc);
  getToken(state);
}