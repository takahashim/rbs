#include "ruby.h"
#include "ruby/re.h"
#include "ruby/encoding.h"

enum TokenType {
  NullType,         /* (Nothing) */

  pLPAREN,          /* ( */
  pRPAREN,          /* ) */
  pCOLON,           /* : */
  pCOLON2,          /* :: */
  pLBRACKET,        /* [ */
  pRBRACKET,        /* ] */
  pLBRACE,          /* { */
  pRBRACE,          /* } */
  pHAT,             /* ^ */
  pARROW,           /* -> */
  pFATARROW,        /* => */
  pCOMMA,           /* , */
  pBAR,             /* | */
  pAMP,             /* & */
  pSTAR,            /* * */
  pSTAR2,           /* ** */
  pDOT,             /* . */
  pDOT3,            /* ... */
  pMINUS,           /* - */
  pPLUS,            /* + */
  pSLASH,           /* / */
  pEQ2,             /* == */
  pEQ3,             /* === */
  pEQT,             /* =~ */
  pBANG,            /* ! */
  pQUESTION,        /* ? */

  kSINGLETON,       /* singleton */
  kSELF,            /* self */
  kINSTANCE,        /* instance */

  tLIDENT,          /* Identifiers starting with lower case */
  tUIDENT,          /* Identifiers starting with upper case */
  tULIDENT,         /* Identifiers starting with `_` */
  tGIDENT,          /* Identifiers starting with `$` */
  tAIDENT,          /* Identifiers starting with `@` */
  tA2IDENT,         /* Identifiers starting with `@@` */
  tBANGIDENT,       /* Identifiers ending with `!` */
  tEQIDENT,         /* Identifiers ending with `=` */
};

typedef struct {
  int byte_pos;
  int char_pos;
  int line;
  int column;
} position;

typedef struct {
  enum TokenType type;
  position start;
  position end;
} token;

typedef struct {
  VALUE string;
  position current;
} lexstate;

typedef struct {
  lexstate *lexstate;
  token next_token;
  token current_token;
  VALUE buffer;
} parserstate;

extern token NullToken;
extern position NullPosition;

token rbsparser_next_token(lexstate *state);

extern VALUE rbsparser_Keywords;

char *peek_token(lexstate *state, token tok);
int token_chars(token tok);
int token_bytes(token tok);

VALUE parse_type(parserstate *state);

void pp(VALUE object);

extern VALUE RBS_AST;
extern VALUE RBS_Types;
extern VALUE RBS_Types_Bases;
extern VALUE RBS_Types_Bases_Self;
extern VALUE RBS_Types_ClassInstance;
extern VALUE RBS_Types_Alias;
extern VALUE RBS_Types_Interface;
extern VALUE RBS_Types_Union;
extern VALUE RBS_Types_Intersection;
extern VALUE RBS_Types_ClassSingleton;
extern VALUE RBS_Types_Tuple;
extern VALUE RBS_Types_Optional;
extern VALUE RBS_Namespace;
extern VALUE RBS_TypeName;
extern VALUE RBS_Location;

VALUE rbs_self_type(VALUE location);
VALUE rbs_namespace(VALUE path, VALUE absolute);
VALUE rbs_type_name(VALUE namespace, VALUE name);
VALUE rbs_class_instance(VALUE typename, VALUE type_args);
VALUE rbs_class_singleton(VALUE typename);
VALUE rbs_alias(VALUE typename);
VALUE rbs_interface(VALUE typename, VALUE type_args);
VALUE rbs_union(VALUE types);
VALUE rbs_intersection(VALUE types);
VALUE rbs_tuple(VALUE types);
VALUE rbs_optional(VALUE type);
VALUE rbs_location(VALUE buffer, int start_pos, int end_pos);
VALUE rbs_location_pp(VALUE buffer, const position *start_pos, const position *end_pos);
VALUE rbs_location_tok(VALUE buffer, const token *tok);

