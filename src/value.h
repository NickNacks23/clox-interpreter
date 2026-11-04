
#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
  VAL_EMPTY,
VAL_UNDEFINED
} ValueType;

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
} Value;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_EMPTY(value) ((value).type == VAL_EMPTY)
#define AS_OBJ(value) ((value).as.obj)
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj*)(object)}})
#define EMPTY_VAL ((Value){VAL_EMPTY, {.number = 0}})
#define IS_UNDEFINED(value) ((value).type == VAL_UNDEFINED)
#define UNDEFINED_VAL ((Value){VAL_UNDEFINED, {.number = 0}})

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

bool valuesEqual(Value a, Value b);

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
uint32_t hashValue(Value value);

#endif
