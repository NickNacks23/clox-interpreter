#include <string.h>
#include "object.h"
#include "memory.h"

#include <stdarg.h>
#include <stdio.h>

#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "vm.h"

VM vm;
static Value clockNative(int argCount, Value* args);

static void resetStack() {
  vm.stackCount = 0;
  vm.frameCount = 0;
}


static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->function;

    size_t instruction =
        frame->ip - function->chunk.code - 1;

    int line = getLine(&function->chunk, (int)instruction);

    fprintf(stderr, "[line %d] in ", line);

    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}



static void defineNative(const char* name, NativeFn function,
                         int arity) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function, arity)));

  tableSet(&vm.globalNames, vm.stack[vm.stackCount - 2],
           NUMBER_VAL((double)vm.globalValues.count));

  writeValueArray(&vm.globalValues, pop());
  pop();
}


void initVM() { 
  vm.frameCount = 0;
  vm.objects = NULL;
  initTable(&vm.globalNames);
initValueArray(&vm.globalValues);
initTable(&vm.strings);
  vm.stackCapacity = 0;
  vm.stackCount = 0;
  vm.stack = NULL;
  resetStack();
  defineNative("clock", clockNative, 0);
}

void freeVM() {
  FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
  vm.stack = NULL;
  vm.stackCapacity = 0;
  vm.stackCount = 0;
  freeTable(&vm.globalNames);
freeValueArray(&vm.globalValues);
freeTable(&vm.strings);
freeObjects();
}

void push(Value value) {
  if (vm.stackCapacity < vm.stackCount + 1) {
    int oldCapacity = vm.stackCapacity;
    vm.stackCapacity = GROW_CAPACITY(oldCapacity);
    vm.stack = GROW_ARRAY(Value, vm.stack,
                          oldCapacity, vm.stackCapacity);
  }

  vm.stack[vm.stackCount] = value;
  vm.stackCount++;
}

Value pop() {
  vm.stackCount--;
  return vm.stack[vm.stackCount];
}

static Value peek(int distance) {
  return vm.stack[vm.stackCount - 1 - distance];
}

static bool call(ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d.",
                 function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slotStart = vm.stackCount - argCount - 1;

  return true;
}

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static bool callValue(Value callee, int argCount) {
  if (IS_FUNCTION(callee)) {
    return call(AS_FUNCTION(callee), argCount);
  }

  if (IS_NATIVE(callee)) {
    ObjNative* nativeObj = (ObjNative*)AS_OBJ(callee);

    if (argCount != nativeObj->arity) {
      runtimeError("Expected %d arguments but got %d.",
                   nativeObj->arity, argCount);
      return false;
    }

    Value result = nativeObj->function(
        argCount, &vm.stack[vm.stackCount - argCount]);

    vm.stackCount -= argCount + 1;
    push(result);
    return true;
  }

  runtimeError("Can only call functions.");
  return false;
}

static bool isFalsey(Value value) {
  return IS_NIL(value) ||
         (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);

  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  push(OBJ_VAL(result));
}


static Value readConstantLong(CallFrame* frame, uint8_t** ip) {
  uint32_t index = (uint32_t)(*(*ip)++);
  index |= (uint32_t)(*(*ip)++) << 8;
  index |= (uint32_t)(*(*ip)++) << 16;

  return frame->function->chunk.constants.values[index];
}



static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
uint8_t* ip = frame->ip;
#define READ_BYTE() (*ip++)

#define READ_SHORT() \
  (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))

#define READ_CONSTANT() \
  (frame->function->chunk.constants.values[READ_BYTE()])

#define READ_CONSTANT_LONG() readConstantLong(frame, &ip)

#define BINARY_OP(valueType, op) \
  do { \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
      frame->ip = ip; \
      runtimeError("Operands must be numbers."); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b = AS_NUMBER(pop()); \
    double a = AS_NUMBER(pop()); \
    push(valueType(a op b)); \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (int i = 0; i < vm.stackCount; i++) {
      printf("[ ");
      printValue(vm.stack[i]);
      printf(" ]");
    }
    printf("\n");
   disassembleInstruction(
    &frame->function->chunk,
    (int)(ip - frame->function->chunk.code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }

      case OP_CONSTANT_LONG: {
        Value constant = READ_CONSTANT_LONG();
        push(constant);
        break;
      }

      case OP_NIL:
        push(NIL_VAL);
        break;

      case OP_TRUE:
        push(BOOL_VAL(true));
        break;

      case OP_FALSE:
        push(BOOL_VAL(false));
        break;

case OP_DUP:
  push(peek(0));
  break;

        case OP_POP:
  pop();
  break;



case OP_GET_LOCAL: {
  uint16_t slot = READ_SHORT();
  push(vm.stack[frame->slotStart + slot]);
  break;
}

case OP_SET_LOCAL: {
  uint16_t slot = READ_SHORT();
  vm.stack[frame->slotStart + slot] = peek(0);
  break;
}



 case OP_GET_GLOBAL: {
  Value value = vm.globalValues.values[READ_BYTE()];

  if (IS_UNDEFINED(value)) {
    frame->ip = ip;
    runtimeError("Undefined variable.");
    return INTERPRET_RUNTIME_ERROR;
  }

  push(value);
  break;
}

case OP_DEFINE_GLOBAL: {
  vm.globalValues.values[READ_BYTE()] = pop();
  break;
}

case OP_SET_GLOBAL: {
  uint8_t index = READ_BYTE();

  if (IS_UNDEFINED(vm.globalValues.values[index])) {
    frame->ip = ip;
    runtimeError("Undefined variable.");
    return INTERPRET_RUNTIME_ERROR;
  }

  vm.globalValues.values[index] = peek(0);
  break;
}


      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }

      case OP_GREATER:
        BINARY_OP(BOOL_VAL, >);
        break;

      case OP_LESS:
        BINARY_OP(BOOL_VAL, <);
        break;

      case OP_ADD: {
  if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
    concatenate();
    frame->ip = ip;
  } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
    double b = AS_NUMBER(pop());
    double a = AS_NUMBER(pop());
    push(NUMBER_VAL(a + b));
  } else {
    runtimeError("Operands must be two numbers or two strings.");
    return INTERPRET_RUNTIME_ERROR;
  }
  break;
}

      case OP_SUBTRACT:
        BINARY_OP(NUMBER_VAL, -);
        break;

      case OP_MULTIPLY:
        BINARY_OP(NUMBER_VAL, *);
        break;

      case OP_DIVIDE:
        BINARY_OP(NUMBER_VAL, /);
        break;

      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;

      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          frame->ip = ip;
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;

        case OP_PRINT: {
  printValue(pop());
  printf("\n");
  break;
}




case OP_JUMP: {
  uint16_t offset = READ_SHORT();
  ip += offset;
  break;
}

case OP_JUMP_IF_FALSE: {
  uint16_t offset = READ_SHORT();
  if (isFalsey(peek(0))) ip += offset;
  break;
}

case OP_LOOP: {
  uint16_t offset = READ_SHORT();
  ip -= offset;
  break;
}



case OP_CALL: {
  int argCount = READ_BYTE();

  // Save the current instruction pointer.
  frame->ip = ip;

  if (!callValue(peek(argCount), argCount)) {
    return INTERPRET_RUNTIME_ERROR;
  }

  // Switch to the called function.
  frame = &vm.frames[vm.frameCount - 1];
  ip = frame->ip;
  break;
}



case OP_RETURN: {
  Value result = pop();
  int slotStart = frame->slotStart;

  vm.frameCount--;

  if (vm.frameCount == 0) {
    resetStack();
    return INTERPRET_OK;
  }

  vm.stackCount = slotStart;
  push(result);

  // Return to the previous function.
  frame = &vm.frames[vm.frameCount - 1];
  ip = frame->ip;
  break;
}
      default:
        return INTERPRET_RUNTIME_ERROR;
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef READ_CONSTANT_LONG
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);

  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  resetStack();

  push(OBJ_VAL(function));

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slotStart = 0;

  return run();
}
