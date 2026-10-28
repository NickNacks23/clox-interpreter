#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

static ObjString* makeString(bool ownsChars, const char* chars,
                             int length) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->ownsChars = ownsChars;
  string->length = length;
  string->chars = chars;
  return string;
}

ObjString* copyString(const char* chars, int length) {
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return makeString(true, heapChars, length);
}

ObjString* constantString(const char* chars, int length) {
  return makeString(false, chars, length);
}

ObjString* takeString(char* chars, int length) {
  return makeString(true, chars, length);
}



void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
printf("%.*s", AS_STRING(value)->length, AS_CSTRING(value));
    break;
  }
}