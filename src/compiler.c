#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_CONDITIONAL,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);


typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;


typedef struct {
  Token name;
  int depth;
  bool isConst;
} Local;

typedef struct {
Local locals[1024];
  int localCount;
  int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;


static int innermostLoopStart = -1;
static int innermostLoopScopeDepth = 0;


Chunk* compilingChunk;

static Table stringConstants;

static bool globalIsConst[256] = {false};

static Chunk* currentChunk() {
  return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type != TOKEN_ERROR) {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;
    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}


static void emitLocal(uint8_t opcode, int slot) {
  emitByte(opcode);
  emitByte((uint8_t)((slot >> 8) & 0xff));
  emitByte((uint8_t)(slot & 0xff));
}



static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void patchJump(int offset) {
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) {
    error("Loop body too large.");
  }

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}



static void emitReturn() {
  emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);

  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}


static void initCompiler(Compiler* compiler) {
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  current = compiler;
}


static void beginScope() {
  current->scopeDepth++;
}


static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
             current->scopeDepth) {
    emitByte(OP_POP);
    current->localCount--;
  }
}




static void endCompiler() {
  emitReturn();

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif
}

// Forward declarations
static void expression();
static void statement();
static void declaration();
static void variable(bool canAssign);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// Expression parsing functions

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                  parser.previous.length - 2)));
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
    case TOKEN_BANG:
      emitByte(OP_NOT);
      break;
    case TOKEN_MINUS:
      emitByte(OP_NEGATE);
      break;
    default:
      return;
  }
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);

  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:
      emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
      break;
    case TOKEN_GREATER:
      emitByte(OP_GREATER);
      break;
    case TOKEN_GREATER_EQUAL:
      emitBytes(OP_LESS, OP_NOT);
      break;
    case TOKEN_LESS:
      emitByte(OP_LESS);
      break;
    case TOKEN_LESS_EQUAL:
      emitBytes(OP_GREATER, OP_NOT);
      break;
    case TOKEN_PLUS:
      emitByte(OP_ADD);
      break;
    case TOKEN_MINUS:
      emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR:
      emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH:
      emitByte(OP_DIVIDE);
      break;
    default:
      return;
  }
}

static void ternary(bool canAssign) {
  // Parse the then branch.
  parsePrecedence(PREC_CONDITIONAL);

  consume(TOKEN_COLON,
          "Expect ':' after then branch of conditional operator.");

  // Parse the else branch.
  parsePrecedence(PREC_ASSIGNMENT);
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE:
      emitByte(OP_FALSE);
      break;
    case TOKEN_NIL:
      emitByte(OP_NIL);
      break;
    case TOKEN_TRUE:
      emitByte(OP_TRUE);
      break;
    default:
      return;
  }
}


static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}


// Parsing rules

static ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CONTINUE] = {NULL, NULL, PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_QUESTION]      = {NULL,     ternary, PREC_CONDITIONAL},
  [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
[TOKEN_AND] = {NULL, and_, PREC_AND},

[TOKEN_CASE]    = {NULL, NULL, PREC_NONE},
[TOKEN_DEFAULT] = {NULL, NULL, PREC_NONE},
[TOKEN_SWITCH]  = {NULL, NULL, PREC_NONE},

  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CONST] = {NULL, NULL, PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
[TOKEN_OR] = {NULL, or_, PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

// Variable handling

static uint8_t identifierConstant(Token* name) {
  ObjString* identifier = copyString(name->start, name->length);
  Value indexValue;

  if (tableGet(&vm.globalNames, OBJ_VAL(identifier), &indexValue)) {
    return (uint8_t)AS_NUMBER(indexValue);
  }

  if (vm.globalValues.count >= 256) {
    error("Too many global variables.");
    return 0;
  }

  uint8_t newIndex = (uint8_t)vm.globalValues.count;
  writeValueArray(&vm.globalValues, UNDEFINED_VAL);

  tableSet(&vm.globalNames, OBJ_VAL(identifier),
           NUMBER_VAL((double)newIndex));

  return newIndex;
}


static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static void addLocal(Token name, bool isConst) {  if (current->localCount == 1024) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  
local->name = name;
local->depth = -1;
local->isConst = isConst;
}

static void declareVariable(bool isConst) {
    if (current->scopeDepth == 0) return;

  Token* name = &parser.previous;

  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];

    if (local->depth != -1 &&
        local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

addLocal(*name, isConst);}




static uint8_t parseVariable(const char* errorMessage, bool isConst) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable(isConst);

  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}



static void markInitialized() {
  current->locals[current->localCount - 1].depth =
      current->scopeDepth;
}



static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}


static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}





static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  bool isConst = false;

  int arg = resolveLocal(current, &name);
  bool isLocal = arg != -1;

  if (isLocal) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
    isConst = current->locals[arg].isConst;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
    isConst = globalIsConst[arg];
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();

    if (isConst) {
      errorAt(&name, "Can't assign to a constant variable.");
    }

    if (isLocal) {
      emitLocal(setOp, arg);
    } else {
      emitBytes(setOp, (uint8_t)arg);
    }
  } else {
    if (isLocal) {
      emitLocal(getOp, arg);
    } else {
      emitBytes(getOp, (uint8_t)arg);
    }
  }
}




static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

// Expression precedence

static void parsePrecedence(Precedence precedence) {
  advance();

  ParseFn prefixRule = getRule(parser.previous.type)->prefix;

  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();

    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);
}


static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}


// Statements and declarations

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}



static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);

  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) statement();

  patchJump(elseJump);
}



static void varDeclaration(bool isConst) {
  uint8_t global = parseVariable("Expect variable name.", isConst);

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    if (isConst) {
      error("Constants must be initialized.");
    }
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON,
          "Expect ';' after variable declaration.");

  if (current->scopeDepth == 0) {
    if (globalIsConst[global]) {
      error("Can't redeclare a constant variable.");
    }

    if (isConst) {
      globalIsConst[global] = true;
    }
  }

  defineVariable(global);
}



static void whileStatement() {
  int loopStart = currentChunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  
emitByte(OP_POP);

int surroundingLoopStart = innermostLoopStart;
int surroundingLoopScopeDepth = innermostLoopScopeDepth;

innermostLoopStart = loopStart;
innermostLoopScopeDepth = current->scopeDepth;

statement();

innermostLoopStart = surroundingLoopStart;
innermostLoopScopeDepth = surroundingLoopScopeDepth;

emitLoop(loopStart);


  patchJump(exitJump);
  emitByte(OP_POP);
}


static void forStatement() {
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (match(TOKEN_SEMICOLON)) {
    // No initializer.
  } else if (match(TOKEN_VAR)) {
    varDeclaration(false);
  } else if (match(TOKEN_CONST)) {
    varDeclaration(true);
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;
  int exitJump = -1;

  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON,
            "Expect ';' after loop condition.");

    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;

    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN,
            "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  
int surroundingLoopStart = innermostLoopStart;
int surroundingLoopScopeDepth = innermostLoopScopeDepth;

innermostLoopStart = loopStart;
innermostLoopScopeDepth = current->scopeDepth;

statement();

innermostLoopStart = surroundingLoopStart;
innermostLoopScopeDepth = surroundingLoopScopeDepth;

emitLoop(loopStart);


  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }

  endScope();
}




#define MAX_CASES 256

static void switchStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after value.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before switch cases.");

  // 0 = before cases, 1 = cases, 2 = after default.
  int state = 0;
  int caseEnds[MAX_CASES];
  int caseCount = 0;
  int previousCaseSkip = -1;

  while (!match(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    if (match(TOKEN_CASE) || match(TOKEN_DEFAULT)) {
      TokenType caseType = parser.previous.type;

      if (state == 2) {
        error("Can't have another case after default.");
      }

      if (state == 1) {
        int endJump = emitJump(OP_JUMP);

        if (caseCount >= MAX_CASES) {
          error("Too many switch cases.");
        } else {
          caseEnds[caseCount++] = endJump;
        }

        patchJump(previousCaseSkip);
        emitByte(OP_POP);
      }

      if (caseType == TOKEN_CASE) {
        state = 1;

        emitByte(OP_DUP);
        expression();

        consume(TOKEN_COLON, "Expect ':' after case value.");

        emitByte(OP_EQUAL);
        previousCaseSkip = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
      } else {
        state = 2;
        consume(TOKEN_COLON, "Expect ':' after default.");
        previousCaseSkip = -1;
      }
    } else {
      if (state == 0) {
        error("Can't have statements before any case.");
      }

      statement();
    }
  }

  if (state == 1) {
    patchJump(previousCaseSkip);
    emitByte(OP_POP);
  }

  for (int i = 0; i < caseCount; i++) {
    patchJump(caseEnds[i]);
  }

  emitByte(OP_POP);
}



static void continueStatement() {
  if (innermostLoopStart == -1) {
    error("Can't use 'continue' outside of a loop.");
  }

  consume(TOKEN_SEMICOLON,
          "Expect ';' after 'continue'.");

  if (innermostLoopStart == -1) return;

  // Remove local variables belonging to scopes
  // inside the current loop.
  for (int i = current->localCount - 1;
       i >= 0 &&
       current->locals[i].depth > innermostLoopScopeDepth;
       i--) {
    emitByte(OP_POP);
  }

  emitLoop(innermostLoopStart);
}



static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();

} else if (match(TOKEN_CONTINUE)) {
  continueStatement();

    } else if (match(TOKEN_SWITCH)) {
  switchStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}





static void declaration() {
  if (match(TOKEN_VAR)) {
    varDeclaration(false);
  } else if (match(TOKEN_CONST)) {
    varDeclaration(true);
  } else {
    statement();
  }
}


// Compile source code


bool compile(const char* source, Chunk* chunk) {
  initScanner(source);

  Compiler compiler;
  initCompiler(&compiler);

  compilingChunk = chunk;
  initTable(&stringConstants);

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  endCompiler();
freeTable(&stringConstants);
return !parser.hadError;
}
