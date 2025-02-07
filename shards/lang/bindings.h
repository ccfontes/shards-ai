#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>
#include <shards/core/runtime.hpp>

struct EvalEnv;

struct Sequence;

struct SHLError {
  char *message;
  uint32_t line;
  uint32_t column;
};

struct SHLAst {
  Sequence *ast;
  SHLError *error;
};

struct SHLWire {
  SHWireRef *wire;
  SHLError *error;
};

extern "C" {

void shards_init(SHCore *core);

SHLAst shards_read(SHStringWithLen code);

EvalEnv *shards_create_env(SHStringWithLen namespace_);

void shards_free_env(EvalEnv *env);

EvalEnv *shards_create_sub_env(EvalEnv *env, SHStringWithLen namespace_);

const SHLError *shards_eval_env(EvalEnv *env, Sequence *ast);

SHLWire shards_transform_env(EvalEnv *env, SHStringWithLen name);

SHLWire shards_transform_envs(EvalEnv **env, uintptr_t len, SHStringWithLen name);

SHLWire shards_eval(Sequence *sequence, SHStringWithLen name);

void shards_free_sequence(Sequence *sequence);

void shards_free_wire(SHWireRef *wire);

void shards_free_error(SHLError *error);

int32_t shards_process_args(int32_t argc, const char *const *argv);

} // extern "C"
