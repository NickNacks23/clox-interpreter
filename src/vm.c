#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm;

static bool clockNative(int argCount, Value* args);
static bool errorNative(int argCount, Value* args);
static bool squareNative(int argCount, Value* args);
static bool absNative(int argCount, Value* args);

static void closeUpvalues(Value* last);

static void resetStack() {
  // A runtime error may abandon frames with captured variables still open.
  if (vm.openUpvalues != NULL) closeUpvalues(vm.stack);
  vm.openUpvalues = NULL;
  vm.stackCount = 0;
  vm.frameCount = 0;
}

// Return the bytecode function regardless of whether the current call uses
// a bare ObjFunction or an ObjClosure.
static inline ObjFunction* frameFunction(const CallFrame* frame) {
  if (frame->callable->type == OBJ_FUNCTION) {
    return (ObjFunction*)frame->callable;
  }
  return ((ObjClosure*)frame->callable)->function;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frameFunction(frame);
    size_t instruction = frame->ip - function->chunk.code - 1;
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

static void defineNative(const char* name, NativeFn function, int arity) {
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
  vm.openUpvalues = NULL;
  resetStack();

  defineNative("clock", clockNative, 0);
  defineNative("testError", errorNative, 0);
  defineNative("square", squareNative, 1);
  defineNative("abs", absNative, 1);
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
    int newCapacity = GROW_CAPACITY(oldCapacity);
    Value* newStack = ALLOCATE(Value, newCapacity);
    if (vm.stackCount > 0) {
      memcpy(newStack, vm.stack, sizeof(Value) * vm.stackCount);
    }
    // Any open upvalue may reference a stack slot about to move.
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL;
         upvalue = upvalue->next) {
      ptrdiff_t index = upvalue->location - vm.stack;
      upvalue->location = &newStack[index];
    }
    FREE_ARRAY(Value, vm.stack, oldCapacity);
    vm.stack = newStack;
    vm.stackCapacity = newCapacity;
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

static bool call(Obj* callee, ObjFunction* function, int argCount) {
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
  frame->callable = callee;
  frame->ip = function->chunk.code;
  frame->slotStart = vm.stackCount - argCount - 1;
  return true;
}

static bool callClosure(ObjClosure* closure, int argCount) {
  return call((Obj*)closure, closure->function, argCount);
}

static bool callFunction(ObjFunction* function, int argCount) {
  return call((Obj*)function, function, argCount);
}


static bool clockNative(int argCount, Value* args) {
  args[-1] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
  return true;
}

static bool errorNative(int argCount, Value* args) {
  args[-1] = OBJ_VAL(copyString("Test error!", 11));
  return false;
}

static bool squareNative(int argCount, Value* args) {
  if (!IS_NUMBER(args[0])) {
    args[-1] = OBJ_VAL(copyString("Expected a number.", 18));
    return false;
  }

  double number = AS_NUMBER(args[0]);
  args[-1] = NUMBER_VAL(number * number);
  return true;
}

static bool absNative(int argCount, Value* args) {
  if (!IS_NUMBER(args[0])) {
    args[-1] = OBJ_VAL(copyString("Expected a number.", 18));
    return false;
  }

  double number = AS_NUMBER(args[0]);
  if (number < 0) number = -number;
  args[-1] = NUMBER_VAL(number);
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_CLOSURE(callee)) {
    return callClosure(AS_CLOSURE(callee), argCount);
  }
  if (IS_FUNCTION(callee)) {
    return callFunction(AS_FUNCTION(callee), argCount);
  }

  if (IS_NATIVE(callee)) {
    ObjNative* nativeObj = (ObjNative*)AS_OBJ(callee);

    if (argCount != nativeObj->arity) {
      runtimeError("Expected %d arguments but got %d.",
                   nativeObj->arity, argCount);
      return false;
    }

    Value* args = &vm.stack[vm.stackCount - argCount];
    if (nativeObj->function(argCount, args)) {
      vm.stackCount -= argCount;
      return true;
    } else {
      runtimeError("%s",
          AS_CSTRING(vm.stack[vm.stackCount - argCount - 1]));
      return false;
    }
  }

  runtimeError("Can only call functions.");
  return false;
}

// The linked list is ordered from the highest stack slot downward.
// Reuse the same upvalue for sibling closures capturing the same variable.
static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* previous = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    previous = upvalue;
    upvalue = upvalue->next;
  }
  if (upvalue != NULL && upvalue->location == local) return upvalue;

  ObjUpvalue* created = newUpvalue(local);
  created->next = upvalue;
  if (previous == NULL) vm.openUpvalues = created;
  else previous->next = created;
  return created;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
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

  return frameFunction(frame)->chunk.constants.values[index];
}

static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  uint8_t* ip = frame->ip;

#define READ_BYTE() (*ip++)
#define READ_SHORT() \
  (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_CONSTANT() \
  (frameFunction(frame)->chunk.constants.values[READ_BYTE()])
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
    disassembleInstruction(&frameFunction(frame)->chunk,
        (int)(ip - frameFunction(frame)->chunk.code));
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

      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        // Only functions with upvalues are wrapped in ObjClosure.
        ObjClosure* closure = (ObjClosure*)frame->callable;
        push(*closure->upvalues[slot]->location);
        break;
      }

      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        ObjClosure* closure = (ObjClosure*)frame->callable;
        *closure->upvalues[slot]->location = peek(0);
        break;
      }

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

      case OP_DEFINE_GLOBAL:
        vm.globalValues.values[READ_BYTE()] = pop();
        break;

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
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          frame->ip = ip;
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

      case OP_PRINT:
        printValue(pop());
        printf("\n");
        break;

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
        frame->ip = ip;

        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }

      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint16_t index = READ_SHORT();
          if (isLocal) {
            closure->upvalues[i] =
                captureUpvalue(&vm.stack[frame->slotStart + index]);
          } else {
            // Capturing an enclosing upvalue implies the enclosing callable
            // is itself an ObjClosure, never a bare ObjFunction.
            ObjClosure* enclosing = (ObjClosure*)frame->callable;
            closure->upvalues[i] = enclosing->upvalues[index];
          }
        }
        break;
      }

      case OP_CLOSE_UPVALUE:
        closeUpvalues(&vm.stack[vm.stackCount - 1]);
        pop();
        break;

      case OP_RETURN: {
        Value result = pop();
        int slotStart = frame->slotStart;
        // Capture any still-open parameters or function-body locals.
        closeUpvalues(&vm.stack[slotStart]);
        vm.frameCount--;

        if (vm.frameCount == 0) {
          resetStack();
          return INTERPRET_OK;
        }

        vm.stackCount = slotStart;
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }

      default:
        return INTERPRET_RUNTIME_ERROR;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_CONSTANT_LONG
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  resetStack();
  // The top-level script has no upvalues, so it can run bare as well.
  push(OBJ_VAL(function));
  if (!callFunction(function, 0)) return INTERPRET_RUNTIME_ERROR;
  return run();
}
