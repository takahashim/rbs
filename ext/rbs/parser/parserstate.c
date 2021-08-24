#include "rbs_parser.h"

#define RESET_TABLE_P(table) (table->size == 0)

id_table *alloc_empty_table() {
  id_table *table = malloc(sizeof(id_table));
  table->size = 10;
  table->count = 0;
  table->ids = calloc(10, sizeof(ID));

  return table;
}

id_table *alloc_reset_table() {
  id_table *table = malloc(sizeof(id_table));
  table->size = 0;

  return table;
}

id_table *parser_push_typevar_table(parserstate *state, bool reset) {
  if (reset) {
    id_table *table = alloc_reset_table();
    table->next = state->vars;
    state->vars = table;
  }

  id_table *table = alloc_empty_table();
  table->next = state->vars;
  state->vars = table;

  return table;
}

void parser_pop_typevar_table(parserstate *state) {
  id_table *table;

  if (state->vars) {
    table = state->vars;
    state->vars = table->next;
    free(table->ids);
    free(table);
  } else {
    rb_raise(rb_eRuntimeError, "Cannot pop empty table");
  }

  if (state->vars && RESET_TABLE_P(state->vars)) {
    table = state->vars;
    state->vars = table->next;
    free(table);
  }
}

void parser_insert_typevar(parserstate *state, ID id) {
  id_table *table = state->vars;

  if (RESET_TABLE_P(table)) {
    rb_raise(rb_eRuntimeError, "Cannot insert to reset table");
  }

  if (table->size == table->count) {
    // expand
    ID *ptr = table->ids;
    table->size += 10;
    table->ids = calloc(table->size, sizeof(ID));
    memcpy(table->ids, ptr, sizeof(ID) * table->count);
    free(ptr);
  }

  table->ids[table->count++] = id;
}

bool parser_typevar_member(parserstate *state, ID id) {
  id_table *table = state->vars;

  while (table && !RESET_TABLE_P(table)) {
    for (size_t i = 0; i < table->count; i++) {
      if (table->ids[i] == id) {
        return true;
      }
    }

    table = table->next;
  }

  return false;
}

void print_parser(parserstate *state) {
  pp(state->buffer);
  printf("  current_token = %s (%d...%d)\n", token_type_str(state->current_token.type), state->current_token.range.start.char_pos, state->current_token.range.end.char_pos);
  printf("     next_token = %s (%d...%d)\n", token_type_str(state->next_token.type), state->next_token.range.start.char_pos, state->next_token.range.end.char_pos);
  printf("    next_token2 = %s (%d...%d)\n", token_type_str(state->next_token2.type), state->next_token2.range.start.char_pos, state->next_token2.range.end.char_pos);
  printf("    next_token3 = %s (%d...%d)\n", token_type_str(state->next_token3.type), state->next_token3.range.start.char_pos, state->next_token3.range.end.char_pos);
}

void parser_advance(parserstate *state) {
  state->current_token = state->next_token;
  state->next_token = state->next_token2;
  state->next_token2 = state->next_token3;

  while (true) {
    if (state->next_token3.type == pEOF) {
      break;
    }

    state->next_token3 = rbsparser_next_token(state->lexstate);

    if (state->next_token3.type == tCOMMENT) {
      // skip
    } else if (state->next_token3.type == tLINECOMMENT) {
      insert_comment_line(state, state->next_token3);
    } else {
      break;
    }
  }
}

/**
 * Advance token if _next_ token is `type`.
 * Ensures one token advance and `state->current_token.type == type`, or current token not changed.
 *
 * @returns true if token advances, false otherwise.
 **/
bool parser_advance_if(parserstate *state, enum TokenType type) {
  if (state->next_token.type == type) {
    parser_advance(state);
    return true;
  } else {
    return false;
  }
}

void parser_advance_assert(parserstate *state, enum TokenType type) {
  parser_advance(state);
  if (state->current_token.type != type) {
    raise_syntax_error(
      state,
      state->current_token,
      "expected a token `%s`",
      token_type_str(type)
    );
  }
}

void print_token(token tok) {
  printf(
    "%s char=%d...%d\n",
    token_type_str(tok.type),
    tok.range.start.char_pos,
    tok.range.end.char_pos
  );
}

void insert_comment_line(parserstate *state, token tok) {
  if (state->last_comment) {
    if (state->last_comment->end.line != tok.range.start.line - 1) {
      free(state->last_comment->tokens);
      free(state->last_comment);
      state->last_comment = NULL;
    }
  }

  if (!state->last_comment) {
    state->last_comment = calloc(1, sizeof(comment));
    state->last_comment->tokens = calloc(10, sizeof(token));
    state->last_comment->line_size = 10;
    state->last_comment->start = tok.range.start;
  }

  if (state->last_comment->line_count == state->last_comment->line_size) {
    token *p = state->last_comment->tokens;
    state->last_comment->tokens = calloc(state->last_comment->line_size + 10, sizeof(token));
    memcpy(state->last_comment->tokens, p, state->last_comment->line_size * sizeof(token));
    state->last_comment->line_size += 10;
    free(p);
  }

  state->last_comment->tokens[state->last_comment->line_count++] = tok;
  state->last_comment->end = tok.range.end;
}

VALUE get_comment(parserstate *state, int subject_line) {
  comment *comment = state->last_comment;

  if (!comment) return Qnil;
  if (comment->end.line != subject_line - 1) return Qnil;

  VALUE content = rb_funcall(state->buffer, rb_intern("content"), 0);
  rb_encoding *enc = rb_enc_get(content);
  VALUE string = rb_enc_str_new_cstr("", enc);

  int hash_bytes = rb_enc_codelen('#', enc);
  int space_bytes = rb_enc_codelen(' ', enc);

  for (size_t i = 0; i < comment->line_count; i++) {
    token tok = comment->tokens[i];

    char *comment_start = RSTRING_PTR(content) + tok.range.start.byte_pos + hash_bytes;
    int comment_bytes = RANGE_BYTES(tok.range) - hash_bytes;
    unsigned char c = rb_enc_mbc_to_codepoint(comment_start, RSTRING_END(content), enc);

    if (c == ' ') {
      comment_start += space_bytes;
      comment_bytes -= space_bytes;
    }

    rb_str_cat(string, comment_start, comment_bytes);
    rb_str_cat_cstr(string, "\n");
  }

  return rbs_ast_comment(
    string,
    rbs_location_pp(state->buffer, &comment->start, &comment->end)
  );
}

parserstate *alloc_parser(VALUE buffer, int line, int column, VALUE variables) {
  VALUE string = rb_funcall(buffer, rb_intern("content"), 0);

  lexstate *lexer = calloc(1, sizeof(lexstate));
  lexer->string = string;
  lexer->current.line = line;
  lexer->current.column = column;
  lexer->start = lexer->current;
  lexer->first_token_of_line = lexer->current.column == 0;

  parserstate *parser = calloc(1, sizeof(parserstate));
  parser->lexstate = lexer;
  parser->buffer = buffer;
  parser->current_token = NullToken;
  parser->next_token = NullToken;
  parser->next_token2 = NullToken;
  parser->next_token3 = NullToken;

  parser_advance(parser);
  parser_advance(parser);
  parser_advance(parser);

  if (!NIL_P(variables)) {
    parser_push_typevar_table(parser, true);

    for (long i = 0; i < rb_array_len(variables); i++) {
      VALUE index = INT2FIX(i);
      VALUE symbol = rb_ary_aref(1, &index, variables);
      parser_insert_typevar(parser, SYM2ID(symbol));
    }
  }

  return parser;
}

void free_parser(parserstate *parser) {
  free(parser->lexstate);
  free(parser);
}
