#ifndef RBS__PARSER_H
#define RBS__PARSER_H

#include "ruby.h"
#include "lexer.h"
#include "location.h"

typedef struct id_table {
  size_t size;
  size_t count;
  ID *ids;
  struct id_table *next;
} id_table;

typedef struct {
  position start;
  position end;
  size_t line_size;
  size_t line_count;
  token *tokens;
} comment;

typedef struct {
  lexstate *lexstate;
  token current_token;
  token next_token;
  token next_token2;
  token next_token3;
  VALUE buffer;
  id_table *vars;
  comment *last_comment;
} parserstate;

VALUE parse_type(parserstate *state);
VALUE parse_method_type(parserstate *state);
VALUE parse_signature(parserstate *state);

void rbs_unescape_string(VALUE string);

/*
Unquote and unescape given string.
*/
VALUE rbs_unquote_string(parserstate *state, range rg, int offset_bytes);

id_table *parser_push_typevar_table(parserstate *state, bool reset);
void parser_insert_typevar(parserstate *state, ID id);
bool parser_typevar_member(parserstate *state, ID id);
void parser_pop_typevar_table(parserstate *state);

void init_parser(parserstate *parser, lexstate *lexer, VALUE buffer, int line, int column, VALUE variables);
void print_parser(parserstate *state);
void parser_advance(parserstate *state);
void parser_advance_assert(parserstate *state, enum TokenType type);
bool parser_advance_if(parserstate *state, enum TokenType type);

void insert_comment_line(parserstate *state, token token);
VALUE get_comment(parserstate *state, int subject_line);

#define INTERN_TOKEN(parserstate, tok) \
  rb_intern3(\
    peek_token(parserstate->lexstate, tok),\
    token_bytes(tok),\
    rb_enc_get(parserstate->lexstate->string)\
  )

#endif
