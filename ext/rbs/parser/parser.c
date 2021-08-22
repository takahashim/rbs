#include "parser.h"

VALUE rbsparser_Keywords;

static VALUE RBSParser;

static ID id_RBS;
static VALUE RBS;

static VALUE sym_class;
static VALUE sym_alias;
static VALUE sym_interface;

static const char *names[] = {
  "NullType",
  "pEOF",

  "pLPAREN",
  "pRPAREN",
  "pCOLON",
  "pCOLON2",
  "pLBRACKET",
  "pRBRACKET",
  "pLBRACE",
  "pRBRACE",
  "pHAT",
  "pARROW",
  "pFATARROW",
  "pCOMMA",
  "pBAR",
  "pAMP",
  "pSTAR",
  "pSTAR2",
  "pDOT",
  "pDOT3",
  "pMINUS",
  "pPLUS",
  "pSLASH",
  "pEQ2",
  "pEQ3",
  "pEQT",
  "pBANG",
  "pQUESTION",

  "kBOOL",            /* bool */
  "kBOT",             /* bot */
  "kCLASS",           /* class */
  "kINSTANCE",        /* instance */
  "kINTERFACE",       /* interface */
  "kNIL",             /* nil */
  "kSELF",            /* self */
  "kSINGLETON",       /* singleton */
  "kTOP",             /* top */
  "kVOID",            /* void */

  "tLIDENT",          /* Identifiers starting with lower case */
  "tUIDENT",          /* Identifiers starting with upper case */
  "tULIDENT",         /* Identifiers starting with `_` */
  "tGIDENT",          /* Identifiers starting with `$` */
  "tAIDENT",          /* Identifiers starting with `@` */
  "tA2IDENT",         /* Identifiers starting with `@@` */
  "tBANGIDENT",
  "tEQIDENT",
};

typedef struct {
  VALUE required_positionals;
  VALUE optional_positionals;
  VALUE rest_positionals;
  VALUE trailing_positionals;
  VALUE required_keywords;
  VALUE optional_keywords;
  VALUE rest_keywords;
} method_params;

#define INTERN_TOKEN(parserstate, tok) rb_intern2(peek_token(parserstate->lexstate, tok), token_bytes(tok))


static VALUE parse_optional(parserstate *state);
static VALUE parse_simple(parserstate *state);

static void print_token(token tok) {
  printf("%s char=%d...%d\n", names[tok.type], tok.start.char_pos, tok.end.char_pos);
}

static void print_parser(parserstate *state) {
  pp(state->buffer);
  printf("  current_token = %s (%d...%d)\n", names[state->current_token.type], state->current_token.start.char_pos, state->current_token.end.char_pos);
  printf("     next_token = %s (%d...%d)\n", names[state->next_token.type], state->next_token.start.char_pos, state->next_token.end.char_pos);
  printf("    next_token2 = %s (%d...%d)\n", names[state->next_token2.type], state->next_token2.start.char_pos, state->next_token2.end.char_pos);
}

static void parser_advance(parserstate *state) {
  state->current_token = state->next_token;
  state->next_token = state->next_token2;
  if (state->next_token2.type != pEOF) {
    state->next_token2 = rbsparser_next_token(state->lexstate);
  }
}

static void parser_advance_assert(parserstate *state, enum TokenType type) {
  parser_advance(state);
  if (state->current_token.type != type) {
    print_token(state->current_token);
    rb_raise(
      rb_eRuntimeError,
      "Unexpected token: expected=%s, actual=%s",
      names[type],
      names[state->current_token.type]
    );
  }
}

static void __attribute__((noreturn)) raise_syntax_error() {
  rb_raise(rb_eRuntimeError, "Syntax error");
}

VALUE parse_type_name(parserstate *state) {
  VALUE absolute = Qfalse;
  VALUE path = rb_ary_new();
  VALUE namespace;

  if (state->current_token.type == pCOLON2) {
    absolute = Qtrue;
    parser_advance(state);
  }

  if (state->current_token.type == tUIDENT) {
    // may be prefix
    while (state->next_token.type == pCOLON2) {
      rb_ary_push(path, ID2SYM(INTERN_TOKEN(state, state->current_token)));

      parser_advance(state);
      parser_advance(state);
    }
  }
  namespace = rbs_namespace(path, absolute);

  switch (state->current_token.type)
  {
  case tLIDENT:
  case tULIDENT:
  case tUIDENT:
    return rbs_type_name(namespace, ID2SYM(INTERN_TOKEN(state, state->current_token)));
    break;
  default:
    raise_syntax_error();
  }
}

static VALUE parse_type_list(parserstate *state, enum TokenType eol, VALUE types) {
  while (true) {
    if (state->next_token.type == eol) {
      break;
    }

    rb_ary_push(types, parse_type(state));

    if (state->next_token.type == pCOMMA) {
      parser_advance(state);
    }
  }

  return types;
}

static bool is_keyword_token(enum TokenType type) {
  switch (type)
  {
  case tLIDENT:
  case tUIDENT:
  case tULIDENT:
  case kSINGLETON:
  case kSELF:
  case kINSTANCE:
  case kVOID:
    return true;
  default:
    return false;
  }
}

/**
 * ```
 * ... type name , ...
 *    >         >       before type => after name
 * ```
 *
 * ```
 *     ... type ) ...
 *        >    >            before type => after type
 * ```
 * */
static VALUE parse_function_param(parserstate *state) {
  position start = state->next_token.start;
  VALUE type = parse_type(state);

  if (state->next_token.type == pCOMMA || state->next_token.type == pRPAREN) {
    return rbs_function_param(
      type,
      Qnil,
      rb_funcall(type, rb_intern("location"), 0)
    );
  } else {
    parser_advance(state);
    VALUE name = ID2SYM(INTERN_TOKEN(state, state->current_token));
    VALUE location = rbs_location_pp(state->buffer, &start, &state->current_token.end);
    return rbs_function_param(type, name, location);
  }
}

/**
 * ```
 * ... keyword `:` type name , ...
 *    ^                              start (before keyword token)
 *                            ^      end   (after comma)
 * ```
 * */
static void parse_required_keyword(parserstate *state, method_params *params) {
  parser_advance(state);
  VALUE keyword = ID2SYM(INTERN_TOKEN(state, state->current_token));
  parser_advance_assert(state, pCOLON);
  VALUE param = parse_function_param(state);
  rb_hash_aset(params->required_keywords, keyword, param);

  if (state->next_token.type == pCOMMA) {
    parser_advance(state);
  }

  return;
}

/**
 * ... `?` keyword `:` type name , ...
 *        ^                              start (before keyword token)
 *                                ^      end   (after comma)
 * */
static void parse_optional_keyword(parserstate *state, method_params *params) {
  parser_advance(state);
  VALUE keyword = ID2SYM(INTERN_TOKEN(state, state->current_token));
  parser_advance_assert(state, pCOLON);
  VALUE param = parse_function_param(state);
  rb_hash_aset(params->optional_keywords, keyword, param);

  if (state->next_token.type == pCOMMA) {
    parser_advance(state);
  }

  return;
}

/**
 *   ... keyword `:` param, ... `)`
 *      >                      >
 *   ... `?` keyword `:` param, ... `)`
 *      >                          >
 *   ... `**` type param `)`
 *      >               >
 *   ...  `)`
 *      >>
 * */
static void parse_keywords(parserstate *state, method_params *params) {
  while (true) {
    switch (state->next_token.type) {
    case pRPAREN:
      return;
    case pQUESTION:
      parser_advance(state);
      parse_optional_keyword(state, params);
      break;
    case pSTAR2:
      parser_advance(state);
      params->rest_keywords = parse_function_param(state);
      return;
    default:
      parse_required_keyword(state, params);
    }
  }
}

/**
 * ... type name, ...
 *    >          >
 *
 * */
static void parse_trailing_params(parserstate *state, method_params *params) {
  while (true) {
    if (state->next_token.type == pRPAREN) {
      return;
    }

    if (state->next_token.type == pQUESTION) {
      parser_advance(state);
      parse_optional_keyword(state, params);
      parse_keywords(state, params);
      return;
    }

    if (state->next_token2.type == pSTAR2) {
      parse_keywords(state, params);
      return;
    }

    if (is_keyword_token(state->next_token.type)) {
      if (state->next_token2.type == pCOLON) {
        parse_keywords(state, params);
        return;
      }
    }

    VALUE param = parse_function_param(state);
    rb_ary_push(params->trailing_positionals, param);

    if (state->next_token.type == pCOMMA) {
      parser_advance(state);
    }
  }
}

/**
 * ... `?` Foo bar `,` ...
 *    >               >
 *
 * */
static void parse_optional_params(parserstate *state, method_params *params) {
  while (true) {
    if (state->next_token.type == pRPAREN) {
      return;
    }

    if (state->next_token.type == pSTAR) {
      parser_advance(state);
      params->rest_positionals = parse_function_param(state);

      if (state->next_token.type == pRPAREN) {
        return;
      } else {
        parser_advance_assert(state, pCOMMA);
        parse_trailing_params(state, params);
        return;
      }
    }

    if (state->next_token.type == pQUESTION) {
      parser_advance(state);

      if (is_keyword_token(state->next_token.type)) {
        if (state->next_token2.type == pCOLON) {
          parse_optional_keyword(state, params);
          parse_keywords(state, params);
          return;
        }
      }

      VALUE param = parse_function_param(state);
      rb_ary_push(params->optional_positionals, param);

      if (state->next_token.type == pCOMMA) {
        parser_advance(state);
      }
    } else {
      parse_keywords(state, params);
      return;
    }
  }
}

/**
 *
 * ... type name, type , ...
 *    >          >      >
 *
 *
 *
 * */
static void parse_required_params(parserstate *state, method_params *params) {
  while (true) {
    if (state->next_token.type == pRPAREN) {
      return;
    }

    if (state->next_token.type == pSTAR) {
      parser_advance(state);
      params->rest_positionals = parse_function_param(state);

      if (state->next_token.type == pRPAREN) {
        return;
      } else {
        parser_advance_assert(state, pCOMMA);
        parse_trailing_params(state, params);
        return;
      }
    }

    if (state->next_token.type == pSTAR2) {
      parse_keywords(state, params);
      return;
    }

    if (state->next_token.type == pQUESTION) {
      parse_optional_params(state, params);
      return;
    }

    VALUE param = parse_function_param(state);
    rb_ary_push(params->required_positionals, param);

    if (state->next_token.type == pCOMMA) {
      parser_advance(state);
    }
  }
}

static void parse_params(parserstate *state, method_params *params) {
  parse_required_params(state, params);
}

/*
    simple_type
  | simple_type '?'
*/
static VALUE parse_optional(parserstate *state) {
  VALUE type = parse_simple(state);

  if (state->next_token.type == pQUESTION) {
    parser_advance(state);
    return rbs_optional(type);
  } else {
    return type;
  }
}

static VALUE parse_proc_type(parserstate *state) {
  method_params params;
  params.required_positionals = rb_ary_new();
  params.optional_positionals = rb_ary_new();
  params.rest_positionals = Qnil;
  params.trailing_positionals = rb_ary_new();
  params.required_keywords = rb_hash_new();
  params.optional_keywords = rb_hash_new();
  params.rest_keywords = Qnil;

  position start = state->current_token.start;
  parser_advance_assert(state, pLPAREN);

  parse_params(state, &params);

  parser_advance_assert(state, pRPAREN);
  parser_advance_assert(state, pARROW);
  VALUE type = parse_optional(state);
  position end = state->current_token.end;

  VALUE function = rbs_function(
    params.required_positionals,
    params.optional_positionals,
    params.rest_positionals,
    params.trailing_positionals,
    params.required_keywords,
    params.optional_keywords,
    params.rest_keywords,
    type
  );

  VALUE loc = rbs_location_pp(state->buffer, &start, &end);

  return rbs_proc(function, Qnil, loc);
}

static VALUE parse_simple(parserstate *state) {
  parser_advance(state);

  switch (state->current_token.type) {
  case pLPAREN: {
    VALUE type = parse_type(state);
    parser_advance_assert(state, pRPAREN);
    return type;
  }
  case kBOOL:
    return rbs_base_type(RBS_Types_Bases_Bool, rbs_location_current_token(state));
  case kBOT:
    return rbs_base_type(RBS_Types_Bases_Bottom, rbs_location_current_token(state));
  case kCLASS:
    return rbs_base_type(RBS_Types_Bases_Class, rbs_location_current_token(state));
  case kINSTANCE:
    return rbs_base_type(RBS_Types_Bases_Instance, rbs_location_current_token(state));
  case kNIL:
    return rbs_base_type(RBS_Types_Bases_Nil, rbs_location_current_token(state));
  case kSELF:
    return rbs_base_type(RBS_Types_Bases_Self, rbs_location_current_token(state));
  case kTOP:
    return rbs_base_type(RBS_Types_Bases_Top, rbs_location_current_token(state));
  case kVOID:
    return rbs_base_type(RBS_Types_Bases_Void, rbs_location_current_token(state));
  case tUIDENT:
  case pCOLON2: {
    VALUE typename = parse_type_name(state);
    VALUE types = rb_ary_new();

    VALUE kind = rb_ivar_get(typename, rb_intern_const("@kind"));

    if (state->next_token.type == pLBRACKET) {
      parser_advance(state);
      rb_ary_push(types, parse_type(state));
      if (state->next_token.type == pCOMMA) parser_advance(state);
      parse_type_list(state, pRBRACKET, types);
      parser_advance_assert(state, pRBRACKET);
    }

    if (kind == sym_class) {
      return rbs_class_instance(typename, types);
    } else if (kind == sym_interface) {
      return rbs_interface(typename, types);
    } else if (kind == sym_alias) {
      return rbs_alias(typename);
    } else {
      return Qnil;
    }
  }
  case tLIDENT: {
    VALUE typename = parse_type_name(state);
    return rbs_alias(typename);
  }
  case kSINGLETON: {
    parser_advance_assert(state, pLPAREN);
    parser_advance(state);
    VALUE typename = parse_type_name(state);
    parser_advance_assert(state, pRPAREN);

    VALUE kind = rb_ivar_get(typename, rb_intern_const("@kind"));
    if (kind != sym_class) {
      raise_syntax_error();
    }

    return rbs_class_singleton(typename);
  }
  case pLBRACKET: {
    VALUE types = parse_type_list(state, pRBRACKET, rb_ary_new());
    parser_advance_assert(state, pRBRACKET);
    return rbs_tuple(types);
  }
  case pHAT: {
    return parse_proc_type(state);
  }
  default:
    rb_raise(rb_eRuntimeError, "Parse error in parse_simple: %s", names[state->current_token.type]);
  }
}

/*
    optional
  | optional '&' ... '&' optional
*/
static VALUE parse_intersection(parserstate *state) {
  VALUE type = parse_optional(state);
  VALUE intersection_types = rb_ary_new();

  rb_ary_push(intersection_types, type);
  while (state->next_token.type == pAMP) {
    parser_advance(state);
    rb_ary_push(intersection_types, parse_optional(state));
  }

  if (rb_array_len(intersection_types) > 1) {
    type = rbs_intersection(intersection_types);
  }

  return type;
}

/*
    intersection
  | intersection '|' ... '|' intersection
*/
VALUE parse_type(parserstate *state) {
  VALUE type = parse_intersection(state);
  VALUE union_types = rb_ary_new();

  rb_ary_push(union_types, type);
  while (state->next_token.type == pBAR) {
    parser_advance(state);
    rb_ary_push(union_types, parse_intersection(state));
  }

  if (rb_array_len(union_types) > 1) {
    type = rbs_union(union_types);
  }

  return type;
}

static VALUE
rbsparser_parse_type(VALUE self, VALUE buffer, VALUE line, VALUE column)
{
  VALUE string = rb_funcall(buffer, rb_intern("content"), 0);
  lexstate lex = { string };
  lex.current.line = NUM2INT(line);
  lex.current.column = NUM2INT(column);

  parserstate parser = { &lex };
  parser.buffer = buffer;
  parser.current_token = NullToken;
  parser.next_token = NullToken;
  parser.next_token2 = NullToken;

  parser_advance(&parser);
  parser_advance(&parser);
  return parse_type(&parser);
}

void
Init_parser(void)
{
  id_RBS = rb_intern_const("RBS");

  RBS = rb_const_get(rb_cObject, id_RBS);
  RBS_AST = rb_const_get(RBS, rb_intern("AST"));
  RBS_Location = rb_const_get(RBS, rb_intern("Location"));
  RBS_Namespace = rb_const_get(RBS, rb_intern("Namespace"));
  RBS_TypeName = rb_const_get(RBS, rb_intern("TypeName"));
  RBS_Types = rb_const_get(RBS, rb_intern("Types"));
  RBS_Types_Alias = rb_const_get(RBS_Types, rb_intern("Alias"));
  RBS_Types_Bases = rb_const_get(RBS_Types, rb_intern("Bases"));
  RBS_Types_Bases_Any = rb_const_get(RBS_Types_Bases, rb_intern("Any"));
  RBS_Types_Bases_Bool = rb_const_get(RBS_Types_Bases, rb_intern("Bool"));
  RBS_Types_Bases_Bottom = rb_const_get(RBS_Types_Bases, rb_intern("Bottom"));
  RBS_Types_Bases_Class = rb_const_get(RBS_Types_Bases, rb_intern("Class"));
  RBS_Types_Bases_Instance = rb_const_get(RBS_Types_Bases, rb_intern("Instance"));
  RBS_Types_Bases_Nil = rb_const_get(RBS_Types_Bases, rb_intern("Nil"));
  RBS_Types_Bases_Self = rb_const_get(RBS_Types_Bases, rb_intern("Self"));
  RBS_Types_Bases_Top = rb_const_get(RBS_Types_Bases, rb_intern("Top"));
  RBS_Types_Bases_Void = rb_const_get(RBS_Types_Bases, rb_intern("Void"));
  RBS_Types_Block = rb_const_get(RBS_Types, rb_intern("Block"));
  RBS_Types_ClassInstance = rb_const_get(RBS_Types, rb_intern("ClassInstance"));
  RBS_Types_ClassSingleton = rb_const_get(RBS_Types, rb_intern("ClassSingleton"));
  RBS_Types_Function = rb_const_get(RBS_Types, rb_intern("Function"));
  RBS_Types_Function_Param = rb_const_get(RBS_Types_Function, rb_intern("Param"));
  RBS_Types_Interface = rb_const_get(RBS_Types, rb_intern("Interface"));
  RBS_Types_Intersection = rb_const_get(RBS_Types, rb_intern("Intersection"));
  RBS_Types_Optional = rb_const_get(RBS_Types, rb_intern("Optional"));
  RBS_Types_Proc = rb_const_get(RBS_Types, rb_intern("Proc"));
  RBS_Types_Tuple = rb_const_get(RBS_Types, rb_intern("Tuple"));
  RBS_Types_Union = rb_const_get(RBS_Types, rb_intern("Union"));
  RBSParser = rb_define_class_under(RBS, "Parser", rb_cObject);

  sym_class = ID2SYM(rb_intern_const("class"));
  sym_interface = ID2SYM(rb_intern_const("interface"));
  sym_alias = ID2SYM(rb_intern_const("alias"));

  rbsparser_Keywords = rb_hash_new();
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("bool"), INT2FIX(kBOOL));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("bot"), INT2FIX(kBOT));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("class"), INT2FIX(kCLASS));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("instance"), INT2FIX(kINSTANCE));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("interface"), INT2FIX(kINTERFACE));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("nil"), INT2FIX(kNIL));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("self"), INT2FIX(kSELF));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("singleton"), INT2FIX(kSINGLETON));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("top"), INT2FIX(kTOP));
  rb_hash_aset(rbsparser_Keywords, rb_str_new_literal("void"), INT2FIX(kVOID));

  rb_define_const(RBSParser, "KEYWORDS", rbsparser_Keywords);

  rb_define_singleton_method(RBSParser, "_parse_type", rbsparser_parse_type, 3);
}
