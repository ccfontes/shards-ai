/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2019 Fragcolor Pte. Ltd. */

#include "runtime.hpp"
#include <shards/common_types.hpp>
#include "core/foundation.hpp"
#include "foundation.hpp"
#include <shards/shards.h>
#include <shards/shards.hpp>
#include "shards/ops.hpp"
#include "shared.hpp"
#include <shards/utility.hpp>
#include <shards/inlined.hpp>
#include "inline.hpp"
#include "async.hpp"
#include <boost/asio/thread_pool.hpp>
#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>
#include <csignal>
#include <cstdarg>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string.h>
#include <unordered_set>
#include <log/log.hpp>
#include <shared_mutex>
#include <boost/atomic/atomic_ref.hpp>
#include <boost/container/small_vector.hpp>

namespace fs = boost::filesystem;

using namespace shards;

#ifdef __EMSCRIPTEN__
// clang-format off
EM_JS(void, sh_emscripten_init, (), {
  // inject some of our types
  if(typeof globalThis.shards === 'undefined') {
    globalThis.shards = {};
  }
  if(typeof globalThis.shards.bonds === 'undefined') {
    globalThis.shards.bonds = {};
  }
  if(typeof globalThis.ShardsBonder === 'undefined') {
    globalThis.ShardsBonder = class ShardsBonder {
      constructor(promise) {
        this.finished = false;
        this.hadErrors = false;
        this.promise = promise;
        this.result = null;
      }

      async run() {
        try {
          this.result = await this.promise;
        } catch (err) {
          console.error(err);
          this.hadErrors = true;
        }
        this.finished = true;
      }
    };
  }
});
// clang-format on
#endif

namespace shards {
#ifdef SH_COMPRESSED_STRINGS
SHOptionalString getCompiledCompressedString(uint32_t id) {
  static std::remove_pointer_t<decltype(Globals::CompressedStrings)> CompiledCompressedStrings;
  if (GetGlobals().CompressedStrings == nullptr)
    GetGlobals().CompressedStrings = &CompiledCompressedStrings;
  auto &val = CompiledCompressedStrings[id];
  val.crc = id; // make sure we return with crc to allow later lookups!
  return val;
}

#include <shards/core/shccstrings.hpp>

static std::unordered_map<uint32_t, std::string> strings_storage;

void decompressStrings() {
  std::scoped_lock lock(shards::GetGlobals().GlobalMutex);
  if (!shards::GetGlobals().CompressedStrings) {
    throw shards::SHException("String storage was null");
  }
  // run the script to populate compressed strings
  auto bytes = Var(__shards_compressed_strings);
  auto wire = ::shards::Wire("decompress strings").let(bytes).shard("Brotli.Decompress").shard("FromBytes");
  auto mesh = SHMesh::make();
  mesh->schedule(wire);
  mesh->tick();
  if (wire->finishedOutput.valueType != SHType::Seq) {
    throw shards::SHException("Failed to decompress strings!");
  }

  for (uint32_t i = 0; i < wire->finishedOutput.payload.seqValue.len; i++) {
    auto pair = wire->finishedOutput.payload.seqValue.elements[i];
    if (pair.valueType != SHType::Seq || pair.payload.seqValue.len != 2) {
      throw shards::SHException("Failed to decompress strings!");
    }
    auto crc = pair.payload.seqValue.elements[0];
    auto str = pair.payload.seqValue.elements[1];
    if (crc.valueType != SHType::Int || str.valueType != SHType::String) {
      throw shards::SHException("Failed to decompress strings!");
    }
    auto emplaced = strings_storage.emplace(uint32_t(crc.payload.intValue), str.payload.stringValue);
    auto &s = emplaced.first->second;
    auto &val = (*shards::GetGlobals().CompressedStrings)[uint32_t(crc.payload.intValue)];
    val.string = s.c_str();
    val.crc = uint32_t(crc.payload.intValue);
  }
}
#else
SHOptionalString setCompiledCompressedString(uint32_t id, const char *str) {
  static std::remove_pointer_t<decltype(Globals::CompressedStrings)> CompiledCompressedStrings;
  if (GetGlobals().CompressedStrings == nullptr)
    GetGlobals().CompressedStrings = &CompiledCompressedStrings;
  SHOptionalString ls{str, id};
  CompiledCompressedStrings[id] = ls;
  return ls;
}
#endif

#ifdef SH_USE_UBSAN
extern "C" void __sanitizer_set_report_path(const char *path);
#endif

void loadExternalShards(std::string from) {
  namespace fs = boost::filesystem;
  auto root = fs::path(from);
  auto pluginPath = root / "externals";
  if (!fs::exists(pluginPath))
    return;

  for (auto &p : fs::recursive_directory_iterator(pluginPath)) {
    if (p.status().type() == fs::file_type::regular_file) {
      auto ext = p.path().extension();
      if (ext == ".dll" || ext == ".so" || ext == ".dylib") {
        auto filename = p.path().filename();
        auto dllstr = p.path().string();
        SHLOG_INFO("Loading external dll: {} path: {}", filename, dllstr);
#if _WIN32
        auto handle = LoadLibraryExA(dllstr.c_str(), NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle) {
          SHLOG_ERROR("LoadLibrary failed, error: {}", GetLastError());
        }
#elif defined(__linux__) || defined(__APPLE__)
        auto handle = dlopen(dllstr.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
          SHLOG_ERROR("LoadLibrary failed, error: {}", dlerror());
        }
#endif
      }
    }
  }
}

#ifdef TRACY_ENABLE
// Defined in the gfx rust crate
//   used to initialize tracy on the rust side, since it required special intialization (C++ doesn't)
//   but since we link to the dll, we can use it from C++ too
extern "C" void gfxTracyInit();
static bool tracyInitialized{};

void tracyInit() {
  gfxTracyInit();
  tracyInitialized = true;
}

#ifdef TRACY_FIBERS
UntrackedVector<SHWire *> &getCoroWireStack() {
  // Here is the thing, this currently works.. but only because we don't move coroutines between threads
  // When we will do if we do this will break...
  static thread_local UntrackedVector<SHWire *> wireStack;
  return wireStack;
}
#endif
#endif

#ifdef SH_USE_TSAN
UntrackedVector<SHWire *> &getCoroWireStack2() {
  // Here is the thing, this currently works.. but only because we don't move coroutines between threads
  // When we will do if we do this will break...
  static thread_local UntrackedVector<SHWire *> wireStack;
  return wireStack;
}
#endif

extern void registerModuleShards(SHCore *core);
void registerShards() {
  SHLOG_DEBUG("Registering shards");

  // at this point we might have some auto magical static linked shard already
  // keep them stored here and re-register them
  // as we assume the observers were setup in this call caller so too late for
  // them
  std::vector<std::pair<std::string_view, SHShardConstructor>> earlyshards;
  for (auto &pair : GetGlobals().ShardsRegister) {
    earlyshards.push_back(pair);
  }
  GetGlobals().ShardsRegister.clear();

  SHCore *core = shardsInterface(SHARDS_CURRENT_ABI);
  registerModuleShards(core);

  // Enums are auto registered we need to propagate them to observers
  for (auto &einfo : GetGlobals().EnumTypesRegister) {
    int32_t vendorId = (int32_t)((einfo.first & 0xFFFFFFFF00000000) >> 32);
    int32_t enumId = (int32_t)(einfo.first & 0x00000000FFFFFFFF);
    for (auto &pobs : GetGlobals().Observers) {
      if (pobs.expired())
        continue;
      auto obs = pobs.lock();
      obs->registerEnumType(vendorId, enumId, einfo.second);
    }
  }

  // re run early shards registration!
  for (auto &pair : earlyshards) {
    registerShard(pair.first, pair.second);
  }

  // finally iterate shard directory and load external dlls
  loadExternalShards(GetGlobals().ExePath);
  if (GetGlobals().RootPath != GetGlobals().ExePath) {
    loadExternalShards(GetGlobals().RootPath);
  }
}

Shard *createShard(std::string_view name) {
  auto it = GetGlobals().ShardsRegister.find(name);
  if (it == GetGlobals().ShardsRegister.end()) {
    return nullptr;
  }

  auto shard = it->second();

  shards::setInlineShardId(shard, name);
  shard->nameLength = uint32_t(name.length());

  return shard;
}

inline void setupRegisterLogging() {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
  logging::setupDefaultLoggerConditional();
#endif
}

void registerShard(std::string_view name, SHShardConstructor constructor, std::string_view fullTypeName) {
  setupRegisterLogging();
  // SHLOG_TRACE("registerBlock({})", name);

  auto findIt = GetGlobals().ShardsRegister.find(name);
  if (findIt == GetGlobals().ShardsRegister.end()) {
    GetGlobals().ShardsRegister.emplace(name, constructor);
  } else {
    GetGlobals().ShardsRegister[name] = constructor;
    SHLOG_WARNING("Overriding shard: {}", name);
  }

  GetGlobals().ShardNamesToFullTypeNames[name] = fullTypeName;

  for (auto &pobs : GetGlobals().Observers) {
    if (pobs.expired())
      continue;
    auto obs = pobs.lock();
    obs->registerShard(name.data(), constructor);
  }
}

void registerObjectType(int32_t vendorId, int32_t typeId, SHObjectInfo info) {
  setupRegisterLogging();
  // SHLOG_TRACE("registerObjectType({})", info.name);

  int64_t id = (int64_t)vendorId << 32 | typeId;
  auto typeName = std::string_view(info.name);

  auto findIt = GetGlobals().ObjectTypesRegister.find(id);
  if (findIt == GetGlobals().ObjectTypesRegister.end()) {
    GetGlobals().ObjectTypesRegister.insert(std::make_pair(id, info));
  } else {
    GetGlobals().ObjectTypesRegister[id] = info;
    SHLOG_WARNING("Overriding object type: {}", typeName);
  }

  auto findIt2 = GetGlobals().ObjectTypesRegisterByName.find(typeName);
  if (findIt2 == GetGlobals().ObjectTypesRegisterByName.end()) {
    GetGlobals().ObjectTypesRegisterByName.emplace(info.name, id);
  } else {
    GetGlobals().ObjectTypesRegisterByName[info.name] = id;
    SHLOG_WARNING("Overriding enum type by name: {}", typeName);
  }

  for (auto &pobs : GetGlobals().Observers) {
    if (pobs.expired())
      continue;
    auto obs = pobs.lock();
    obs->registerObjectType(vendorId, typeId, info);
  }
}

void registerEnumType(int32_t vendorId, int32_t typeId, SHEnumInfo info) {
  setupRegisterLogging();
  // SHLOG_TRACE("registerEnumType({})", info.name);

  int64_t id = (int64_t)vendorId << 32 | typeId;
  auto enumName = std::string_view(info.name);

  auto findIt = GetGlobals().EnumTypesRegister.find(id);
  if (findIt == GetGlobals().EnumTypesRegister.end()) {
    GetGlobals().EnumTypesRegister.insert(std::make_pair(id, info));
  } else {
    GetGlobals().EnumTypesRegister[id] = info;
    SHLOG_WARNING("Overriding enum type: {}", enumName);
  }

  auto findIt2 = GetGlobals().EnumTypesRegisterByName.find(enumName);
  if (findIt2 == GetGlobals().EnumTypesRegisterByName.end()) {
    GetGlobals().EnumTypesRegisterByName.emplace(info.name, id);
  } else {
    GetGlobals().EnumTypesRegisterByName[info.name] = id;
    SHLOG_WARNING("Overriding enum type by name: {}", enumName);
  }

  for (auto &pobs : GetGlobals().Observers) {
    if (pobs.expired())
      continue;
    auto obs = pobs.lock();
    obs->registerEnumType(vendorId, typeId, info);
  }
}

const SHObjectInfo *findObjectInfo(int32_t vendorId, int32_t typeId) {
  int64_t id = (int64_t)vendorId << 32 | typeId;
  auto it = shards::GetGlobals().ObjectTypesRegister.find(id);
  if (it != shards::GetGlobals().ObjectTypesRegister.end()) {
    return &it->second;
  }
  return nullptr;
}

int64_t findObjectTypeId(std::string_view name) {
  auto it = shards::GetGlobals().ObjectTypesRegisterByName.find(name);
  if (it != shards::GetGlobals().ObjectTypesRegisterByName.end()) {
    return it->second;
  }
  return 0;
}

const SHEnumInfo *findEnumInfo(int32_t vendorId, int32_t typeId) {
  int64_t id = (int64_t)vendorId << 32 | typeId;
  auto it = shards::GetGlobals().EnumTypesRegister.find(id);
  if (it != shards::GetGlobals().EnumTypesRegister.end()) {
    return &it->second;
  }
  return nullptr;
}

int64_t findEnumId(std::string_view name) {
  auto it = shards::GetGlobals().EnumTypesRegisterByName.find(name);
  if (it != shards::GetGlobals().EnumTypesRegisterByName.end()) {
    return it->second;
  }
  return 0;
}

void registerRunLoopCallback(std::string_view eventName, SHCallback callback) {
  shards::GetGlobals().RunLoopHooks[eventName] = callback;
}

void unregisterRunLoopCallback(std::string_view eventName) {
  auto findIt = shards::GetGlobals().RunLoopHooks.find(eventName);
  if (findIt != shards::GetGlobals().RunLoopHooks.end()) {
    shards::GetGlobals().RunLoopHooks.erase(findIt);
  }
}

void registerExitCallback(std::string_view eventName, SHCallback callback) {
  shards::GetGlobals().ExitHooks[eventName] = callback;
}

void unregisterExitCallback(std::string_view eventName) {
  auto findIt = shards::GetGlobals().ExitHooks.find(eventName);
  if (findIt != shards::GetGlobals().ExitHooks.end()) {
    shards::GetGlobals().ExitHooks.erase(findIt);
  }
}

void registerWire(SHWire *wire) {
  std::shared_ptr<SHWire> sc(wire);
  shards::GetGlobals().GlobalWires[wire->name] = sc;
}

void unregisterWire(SHWire *wire) {
  auto findIt = shards::GetGlobals().GlobalWires.find(wire->name);
  if (findIt != shards::GetGlobals().GlobalWires.end()) {
    shards::GetGlobals().GlobalWires.erase(findIt);
  }
}

void callExitCallbacks() {
  // Iterate backwards
  for (auto it = shards::GetGlobals().ExitHooks.begin(); it != shards::GetGlobals().ExitHooks.end(); ++it) {
    it->second();
  }
}

entt::id_type findId(SHContext *ctx) noexcept {
  entt::id_type id = entt::null;

  // try find a wire id
  // from top to bottom of wire stack
  {
    auto rit = ctx->wireStack.rbegin();
    for (; rit != ctx->wireStack.rend(); ++rit) {
      // prioritize local variables
      auto wire = *rit;
      if (wire->id != entt::null) {
        id = wire->id;
        break;
      }
    }
  }

  return id;
}

SHVar *referenceWireVariable(SHWire *wire, std::string_view name) {
  // let's avoid creating a string every time
  static thread_local std::string nameStr;
  nameStr.clear();
  nameStr.append(name.data(), name.size());

  SHVar &v = wire->variables[nameStr];
  v.refcount++;
  v.flags |= SHVAR_FLAGS_REF_COUNTED;
  return &v;
}

SHVar *referenceWireVariable(SHWireRef wire, std::string_view name) {
  auto swire = SHWire::sharedFromRef(wire);
  return referenceWireVariable(swire.get(), name);
}

SHVar *referenceGlobalVariable(SHContext *ctx, std::string_view name) {
  // let's avoid creating a string every time
  static thread_local std::string nameStr;
  nameStr.clear();
  nameStr.append(name.data(), name.size());

  auto mesh = ctx->main->mesh.lock();
  assert(mesh);

  SHVar &v = mesh->variables[nameStr];
  v.refcount++;
  if (v.refcount == 1) {
    SHLOG_TRACE("Creating a global variable, wire: {} name: {}", ctx->wireStack.back()->name, name);
  }
  v.flags |= SHVAR_FLAGS_REF_COUNTED;
  return &v;
}

SHVar *referenceVariable(SHContext *ctx, std::string_view name) {
  // let's avoid creating a string every time
  static thread_local std::string nameStr;
  nameStr.clear();
  nameStr.append(name.data(), name.size());

  // try find a wire variable
  // from top to bottom of wire stack
  {
    auto rit = ctx->wireStack.rbegin();
    for (; rit != ctx->wireStack.rend(); ++rit) {
      // prioritize local variables
      auto wire = *rit;
      auto it = wire->variables.find(nameStr);
      if (it != wire->variables.end()) {
        // found, lets get out here
        SHVar &cv = it->second;
        cv.refcount++;
        cv.flags |= SHVAR_FLAGS_REF_COUNTED;
        return &cv;
      }
      // try external variables
      auto pit = wire->externalVariables.find(nameStr);
      if (pit != wire->externalVariables.end()) {
        // found, lets get out here
        SHVar &cv = *pit->second;
        assert((cv.flags & SHVAR_FLAGS_EXTERNAL) != 0);
        return &cv;
      }
      // if this wire is pure we break here and do not look further
      if (wire->pure) {
        goto create;
      }
    }
  }

  // try using mesh
  {
    auto mesh = ctx->main->mesh.lock();
    assert(mesh);

    // Was not in wires.. find in mesh
    {
      auto it = mesh->variables.find(nameStr);
      if (it != mesh->variables.end()) {
        // found, lets get out here
        SHVar &cv = it->second;
        cv.refcount++;
        cv.flags |= SHVAR_FLAGS_REF_COUNTED;
        return &cv;
      }
    }

    // Was not in mesh directly.. try find in meshs refs
    {
      auto it = mesh->refs.find(nameStr);
      if (it != mesh->refs.end()) {
        SHLOG_TRACE("Referencing a parent node variable, wire: {} name: {}", ctx->wireStack.back()->name, name);
        // found, lets get out here
        SHVar *cv = it->second;
        cv->refcount++;
        cv->flags |= SHVAR_FLAGS_REF_COUNTED;
        return cv;
      }
    }
  }

create:
  // worst case create in current top wire!
  SHLOG_TRACE("Creating a variable, wire: {} name: {}", ctx->wireStack.back()->name, name);
  SHVar &cv = ctx->wireStack.back()->variables[nameStr];
  assert(cv.refcount == 0);
  cv.refcount++;
  // can safely set this here, as we are creating a new variable
  cv.flags = SHVAR_FLAGS_REF_COUNTED;
  return &cv;
}

void releaseVariable(SHVar *variable) {
  if (!variable)
    return;

  if ((variable->flags & SHVAR_FLAGS_EXTERNAL) != 0) {
    return;
  }

  assert((variable->flags & SHVAR_FLAGS_REF_COUNTED) == SHVAR_FLAGS_REF_COUNTED);
  assert(variable->refcount > 0);

  variable->refcount--;
  if (variable->refcount == 0) {
    SHLOG_TRACE("Destroying a variable (0 ref count), type: {}", type2Name(variable->valueType));
    destroyVar(*variable);
  }
}

SHWireState suspend(SHContext *context, double seconds) {
  if (unlikely(!context->shouldContinue() || context->onCleanup)) {
    throw ActivationError("Trying to suspend a terminated context!");
  } else if (unlikely(!context->continuation)) {
    throw ActivationError("Trying to suspend a context without coroutine!");
  }

  if (seconds <= 0) {
    context->next = SHDuration(0);
  } else {
    context->next = SHClock::now().time_since_epoch() + SHDuration(seconds);
  }

#ifndef __EMSCRIPTEN__
  context->continuation = context->continuation.resume();
#else
  context->continuation->yield();
#endif

  // RESUMING

  return context->getState();
}

void hash_update(const SHVar &var, void *state);

#define SH_WIRE_SET_STACK(prefix)                                                              \
  std::deque<std::unordered_set<const SHWire *>> &prefix##WiresStack() {                       \
    thread_local std::deque<std::unordered_set<const SHWire *>> s;                             \
    return s;                                                                                  \
  }                                                                                            \
  std::optional<std::unordered_set<const SHWire *> *> &prefix##WiresStorage() {                \
    thread_local std::optional<std::unordered_set<const SHWire *> *> wiresOpt;                 \
    return wiresOpt;                                                                           \
  }                                                                                            \
  std::unordered_set<const SHWire *> &prefix##Wires() {                                        \
    auto wiresPtr = *prefix##WiresStorage();                                                   \
    assert(wiresPtr);                                                                          \
    return *wiresPtr;                                                                          \
  }                                                                                            \
  void prefix##WiresPush() { prefix##WiresStorage() = &prefix##WiresStack().emplace_front(); } \
  void prefix##WiresPop() {                                                                    \
    prefix##WiresStack().pop_front();                                                          \
    if (prefix##WiresStack().empty()) {                                                        \
      prefix##WiresStorage() = std::nullopt;                                                   \
    } else {                                                                                   \
      prefix##WiresStorage() = &prefix##WiresStack().front();                                  \
    }                                                                                          \
  }

SH_WIRE_SET_STACK(gathering);
SH_WIRE_SET_STACK(hashing);

template <typename T, bool HANDLES_RETURN, bool HASHED>
ALWAYS_INLINE SHWireState shardsActivation(T &shards, SHContext *context, const SHVar &wireInput, SHVar &output,
                                           SHVar *outHash = nullptr) noexcept {
  XXH3_state_s hashState; // optimized out in release if not HASHED
  if constexpr (HASHED) {
    assert(outHash);
    XXH3_INITSTATE(&hashState);
    XXH3_128bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret, XXH_SECRET_DEFAULT_SIZE);
  }
  DEFER(if constexpr (HASHED) {
    auto digest = XXH3_128bits_digest(&hashState);
    outHash->valueType = SHType::Int2;
    outHash->payload.int2Value[0] = int64_t(digest.low64);
    outHash->payload.int2Value[1] = int64_t(digest.high64);
    SHLOG_TRACE("Hash digested {}", *outHash);
  });

  // store initial input
  auto input = wireInput;

  // find len based on shards type
  size_t len;
  if constexpr (std::is_same<T, Shards>::value || std::is_same<T, SHSeq>::value) {
    len = shards.len;
  } else if constexpr (std::is_same<T, std::vector<ShardPtr>>::value) {
    len = shards.size();
  } else {
    len = 0;
    SHLOG_FATAL("Unreachable shardsActivation case");
  }

  for (size_t i = 0; i < len; i++) {
    ShardPtr blk;
    if constexpr (std::is_same<T, Shards>::value) {
      blk = shards.elements[i];
    } else if constexpr (std::is_same<T, SHSeq>::value) {
      blk = shards.elements[i].payload.shardValue;
    } else if constexpr (std::is_same<T, std::vector<ShardPtr>>::value) {
      blk = shards[i];
    } else {
      blk = nullptr;
      SHLOG_FATAL("Unreachable shardsActivation case");
    }

    if constexpr (HASHED) {
      const auto shardHash = blk->hash(blk);
      SHLOG_TRACE("Hashing shard {}", shardHash);
      XXH3_128bits_update(&hashState, &shardHash, sizeof(shardHash));

      SHLOG_TRACE("Hashing input {}", input);
      hash_update(input, &hashState);

      const auto params = blk->parameters(blk);
      for (uint32_t nParam = 0; nParam < params.len; nParam++) {
        const auto param = blk->getParam(blk, nParam);
        SHLOG_TRACE("Hashing param {}", param);
        hash_update(param, &hashState);
      }

      output = activateShard(blk, context, input);
      SHLOG_TRACE("Hashing output {}", output);
      hash_update(output, &hashState);
    } else {
      output = activateShard(blk, context, input);
    }

    // Deal with aftermath of activation
    if (unlikely(!context->shouldContinue())) {
      auto state = context->getState();
      switch (state) {
      case SHWireState::Return:
        if constexpr (HANDLES_RETURN)
          context->continueFlow();
        return SHWireState::Return;
      case SHWireState::Error:
        SHLOG_ERROR("Shard activation error, failed shard: {}, error: {}, line: {}, column: {}", blk->name(blk),
                    context->getErrorMessage(), blk->line, blk->column);
      case SHWireState::Stop:
      case SHWireState::Restart:
        return state;
      case SHWireState::Rebase:
        // reset input to wire one
        // and reset state
        input = wireInput;
        context->continueFlow();
        continue;
      case SHWireState::Continue:
        break;
      }
    }

    // Pass output to next block input
    input = output;
  }
  return SHWireState::Continue;
}

SHWireState activateShards(Shards shards, SHContext *context, const SHVar &wireInput, SHVar &output) noexcept {
  return shardsActivation<Shards, false, false>(shards, context, wireInput, output);
}

SHWireState activateShards2(Shards shards, SHContext *context, const SHVar &wireInput, SHVar &output) noexcept {
  return shardsActivation<Shards, true, false>(shards, context, wireInput, output);
}

SHWireState activateShards(SHSeq shards, SHContext *context, const SHVar &wireInput, SHVar &output) noexcept {
  return shardsActivation<SHSeq, false, false>(shards, context, wireInput, output);
}

SHWireState activateShards2(SHSeq shards, SHContext *context, const SHVar &wireInput, SHVar &output) noexcept {
  return shardsActivation<SHSeq, true, false>(shards, context, wireInput, output);
}

SHWireState activateShards(Shards shards, SHContext *context, const SHVar &wireInput, SHVar &output, SHVar &outHash) noexcept {
  return shardsActivation<Shards, false, true>(shards, context, wireInput, output, &outHash);
}

SHWireState activateShards2(Shards shards, SHContext *context, const SHVar &wireInput, SHVar &output, SHVar &outHash) noexcept {
  return shardsActivation<Shards, true, true>(shards, context, wireInput, output, &outHash);
}

// Lazy and also avoid windows Loader (Dead)Lock
// https://docs.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices?redirectedfrom=MSDN
Shared<boost::asio::thread_pool, SharedThreadPoolConcurrency> SharedThreadPool{};

bool matchTypes(const SHTypeInfo &inputType, const SHTypeInfo &receiverType, bool isParameter, bool strict) {
  if (receiverType.basicType == SHType::Any)
    return true;

  if (inputType.basicType != receiverType.basicType) {
    // Fail if basic type differs
    return false;
  }

  switch (inputType.basicType) {
  case SHType::Object: {
    if (inputType.object.vendorId != receiverType.object.vendorId || inputType.object.typeId != receiverType.object.typeId) {
      return false;
    }
    break;
  }
  case SHType::Enum: {
    // special case: any enum
    if (receiverType.enumeration.vendorId == 0 && receiverType.enumeration.typeId == 0)
      return true;
    // otherwise, exact match
    if (inputType.enumeration.vendorId != receiverType.enumeration.vendorId ||
        inputType.enumeration.typeId != receiverType.enumeration.typeId) {
      return false;
    }
    break;
  }
  case SHType::Seq: {
    if (strict) {
      if (inputType.seqTypes.len > 0 && receiverType.seqTypes.len > 0) {
        // all input types must be in receiver, receiver can have more ofc
        for (uint32_t i = 0; i < inputType.seqTypes.len; i++) {
          for (uint32_t j = 0; j < receiverType.seqTypes.len; j++) {
            if (receiverType.seqTypes.elements[j].basicType == SHType::Any ||
                matchTypes(inputType.seqTypes.elements[i], receiverType.seqTypes.elements[j], isParameter, strict))
              goto matched;
          }
          return false;
        matched:
          continue;
        }
      } // Empty input sequence type indicates [ Any ], receiver type needs to explicitly contain Any to match
      else if (inputType.seqTypes.len == 0 && receiverType.seqTypes.len > 0) {
        for (uint32_t j = 0; j < receiverType.seqTypes.len; j++) {
          if (receiverType.seqTypes.elements[j].basicType == SHType::Any)
            return true;
        }
        return false;
      } else if (inputType.seqTypes.len == 0 || receiverType.seqTypes.len == 0) {
        return false;
      }
      // if a fixed size is requested make sure it fits at least enough elements
      if (receiverType.fixedSize != 0 && receiverType.fixedSize > inputType.fixedSize) {
        return false;
      }
    }
    break;
  }
  case SHType::Table: {
    if (strict) {
      // Table is a complicated one
      // We use it as many things.. one of it as structured data
      // So we have many possible cases:
      // 1. a receiver table with just type info is flexible, accepts only those
      // types but the keys are open to anything, if no types are available, it
      // accepts any type
      // 2. a receiver table with type info and key info is strict, means that
      // input has to match 1:1, an exception is done if the last key is empty
      // as in
      // "" on the receiver side, in such case any input is allowed (types are
      // still checked)
      const auto numInputTypes = inputType.table.types.len;
      const auto numReceiverTypes = receiverType.table.types.len;
      const auto numInputKeys = inputType.table.keys.len;
      const auto numReceiverKeys = receiverType.table.keys.len;
      if (numReceiverKeys == 0) {
        // case 1, consumer is not strict, match types if avail
        // ignore input keys information
        if (numInputTypes == 0) {
          // assume this as an Any
          if (numReceiverTypes == 0)
            return true; // both Any
          auto matched = false;
          SHTypeInfo anyType{SHType::Any};
          for (uint32_t y = 0; y < numReceiverTypes; y++) {
            auto btype = receiverType.table.types.elements[y];
            if (matchTypes(anyType, btype, isParameter, strict)) {
              matched = true;
              break;
            }
          }
          if (!matched) {
            return false;
          }
        } else {
          if (isParameter || numReceiverTypes != 0) {
            // receiver doesn't accept anything, match further
            for (uint32_t i = 0; i < numInputTypes; i++) {
              // Go thru all exposed types and make sure we get a positive match
              // with the consumer
              auto atype = inputType.table.types.elements[i];
              auto matched = false;
              for (uint32_t y = 0; y < numReceiverTypes; y++) {
                auto btype = receiverType.table.types.elements[y];
                if (matchTypes(atype, btype, isParameter, strict)) {
                  matched = true;
                  break;
                }
              }
              if (!matched) {
                return false;
              }
            }
          }
        }
      } else {
        if (!isParameter && numInputKeys == 0 && numInputTypes == 0)
          return true; // update case {} >= .edit-me {"x" 10} > .edit-me

        // Last element being empty ("") indicates that keys not in the type can match
        // in that case they will be matched against the last type element at the same position
        const auto lastElementEmpty = receiverType.table.keys.elements[numReceiverKeys - 1].valueType == SHType::None;
        if (!lastElementEmpty && (numInputKeys != numReceiverKeys || numInputKeys != numInputTypes)) {
          // we need a 1:1 match in this case, fail early
          return false;
        }

        auto missingMatches = numInputKeys;
        for (uint32_t i = 0; i < numInputKeys; i++) {
          auto inputEntryType = inputType.table.types.elements[i];
          auto inputEntryKey = inputType.table.keys.elements[i];
          for (uint32_t y = 0; y < numReceiverKeys; y++) {
            auto receiverEntryType = receiverType.table.types.elements[y];
            auto receiverEntryKey = receiverType.table.keys.elements[y];
            // Either match the expected key's type or compare against the last type (if it's key is "")
            if (inputEntryKey == receiverEntryKey || (lastElementEmpty && y == (numReceiverKeys - 1))) {
              if (matchTypes(inputEntryType, receiverEntryType, isParameter, strict)) {
                missingMatches--;
                y = numReceiverKeys; // break
              } else
                return false; // fail quick in this case
            }
          }
        }

        if (missingMatches)
          return false;
      }
    }
    break;
  }
  default:
    return true;
  }
  return true;
}

struct ValidationContext {
  std::unordered_map<std::string_view, SHExposedTypeInfo> inherited;
  std::unordered_map<std::string_view, SHExposedTypeInfo> exposed;
  std::unordered_set<std::string_view> variables;
  std::unordered_set<std::string_view> references;
  std::unordered_set<SHExposedTypeInfo> required;

  SHTypeInfo previousOutputType{};
  SHTypeInfo originalInputType{};

  Shard *bottom{};
  Shard *next{};
  SHWire *wire{};

  SHValidationCallback cb{};
  void *userData{};

  bool onWorkerThread{false};

  std::unordered_map<std::string_view, SHExposedTypeInfo> *fullRequired{nullptr};
};

void validateConnection(ValidationContext &ctx) {
  auto previousOutput = ctx.previousOutputType;

  auto inputInfos = ctx.bottom->inputTypes(ctx.bottom);
  auto inputMatches = false;
  // validate our generic input
  if (inputInfos.len == 1 && inputInfos.elements[0].basicType == SHType::None) {
    // in this case a None always matches
    inputMatches = true;
  } else {
    for (uint32_t i = 0; inputInfos.len > i; i++) {
      auto &inputInfo = inputInfos.elements[i];
      if (matchTypes(previousOutput, inputInfo, false, true)) {
        inputMatches = true;
        break;
      }
    }
  }

  if (!inputMatches) {
    const auto msg =
        fmt::format("Could not find a matching input type, shard: {} (line: {}, column: {}) expected: {} found instead: {}",
                    ctx.bottom->name(ctx.bottom), ctx.bottom->line, ctx.bottom->column, inputInfos, ctx.previousOutputType);
    ctx.cb(ctx.bottom, SHStringWithLen{msg.data(), msg.size()}, false, ctx.userData);
  }

  // infer and specialize types if we need to
  // If we don't we assume our output will be of the same type of the previous!
  if (ctx.bottom->compose) {
    SHInstanceData data{};
    data.shard = ctx.bottom;
    data.wire = ctx.wire;
    data.inputType = previousOutput;
    data.requiredVariables = ctx.fullRequired;
    if (ctx.next) {
      data.outputTypes = ctx.next->inputTypes(ctx.next);
    }
    data.onWorkerThread = ctx.onWorkerThread;

    // Pass all we got in the context!
    // notice that shards might add new records to this array
    for (auto &pair : ctx.exposed) {
      shards::arrayPush(data.shared, pair.second);
    }
    // and inherited
    for (auto &pair : ctx.inherited) {
      shards::arrayPush(data.shared, pair.second);
    }
    DEFER(shards::arrayFree(data.shared));

    // this ensures e.g. SetVariable exposedVars have right type from the actual
    // input type (previousOutput)!
    auto composeResult = ctx.bottom->compose(ctx.bottom, data);
    if (composeResult.error.code != SH_ERROR_NONE) {
      std::string_view msg(composeResult.error.message.string, composeResult.error.message.len);
      SHLOG_ERROR("Error composing shard: {}, wire: {}", msg, ctx.wire ? ctx.wire->name : "(unwired)");
      throw ComposeError(msg);
    }
    ctx.previousOutputType = composeResult.result;
  } else {
    // Short-cut if it's just one type and not any type
    auto outputTypes = ctx.bottom->outputTypes(ctx.bottom);
    if (outputTypes.len == 1) {
      if (outputTypes.elements[0].basicType != SHType::Any) {
        ctx.previousOutputType = outputTypes.elements[0];
      } else {
        // Any type tho means keep previous output type!
        // Unless we require a specific input type, in that case
        // We assume we are not a passthru shard
        auto inputTypes = ctx.bottom->inputTypes(ctx.bottom);
        if (inputTypes.len == 1 && inputTypes.elements[0].basicType != SHType::Any) {
          ctx.previousOutputType = outputTypes.elements[0];
        }
      }
    } else {
      SHLOG_ERROR("Shard {} needs to implement the compose method", ctx.bottom->name(ctx.bottom));
      throw ComposeError("Shard has multiple possible output types and is "
                         "missing the compose method");
    }
  }

#ifndef NDEBUG
  // do some sanity checks that also provide coverage on outputTypes
  if (!ctx.bottom->compose) {
    auto outputTypes = ctx.bottom->outputTypes(ctx.bottom);
    shards::IterableTypesInfo otypes(outputTypes);
    auto flowStopper = [&]() {
      if (strcmp(ctx.bottom->name(ctx.bottom), "Restart") == 0 || strcmp(ctx.bottom->name(ctx.bottom), "Stop") == 0 ||
          strcmp(ctx.bottom->name(ctx.bottom), "Return") == 0 || strcmp(ctx.bottom->name(ctx.bottom), "Fail") == 0) {
        return true;
      } else {
        return false;
      }
    }();

    auto blockHasValidOutputTypes =
        flowStopper || std::any_of(otypes.begin(), otypes.end(), [&](const auto &t) {
          return t.basicType == SHType::Any ||
                 (t.basicType == SHType::Seq && t.seqTypes.len == 1 && t.seqTypes.elements[0].basicType == SHType::Any &&
                  ctx.previousOutputType.basicType == SHType::Seq) || // any seq
                 (t.basicType == SHType::Table &&
                  // TODO find Any in table types
                  ctx.previousOutputType.basicType == SHType::Table) || // any table
                 t == ctx.previousOutputType;
        });
    if (!blockHasValidOutputTypes) {
      std::string blockName = ctx.bottom->name(ctx.bottom);
      throw std::runtime_error(fmt::format("Block {} doesn't have a valid output type", blockName));
    }
  }
#endif

  // Grab those after type inference!
  auto exposedVars = ctx.bottom->exposedVariables(ctx.bottom);
  // Add the vars we expose
  for (uint32_t i = 0; exposedVars.len > i; i++) {
    auto &exposed_param = exposedVars.elements[i];
    std::string_view name(exposed_param.name);
    ctx.exposed[name] = exposed_param;

    // Reference mutability checks
    if (strcmp(ctx.bottom->name(ctx.bottom), "Ref") == 0) {
      // make sure we are not Ref-ing a Set
      // meaning target would be overwritten, yet Set will try to deallocate
      // it/manage it
      if (ctx.variables.count(name)) {
        // Error
        auto err = fmt::format(
            "Ref variable name already used as Set. Overwriting a previously Set variable with Ref is not allowed, name: {}",
            name);
        ctx.cb(ctx.bottom, SHStringWithLen{err.data(), err.size()}, false, ctx.userData);
      }
      ctx.references.insert(name);
    } else if (strcmp(ctx.bottom->name(ctx.bottom), "Set") == 0) {
      // make sure we are not Set-ing a Ref
      // meaning target memory could be any shard temporary buffer, yet Set will
      // try to deallocate it/manage it
      if (ctx.references.count(name)) {
        // Error
        auto err = fmt::format(
            "Set variable name already used as Ref. Overwriting a previously Ref variable with Set is not allowed, name: {}",
            name);
        ctx.cb(ctx.bottom, SHStringWithLen{err.data(), err.size()}, false, ctx.userData);
      }
      ctx.variables.insert(name);
    } else if (strcmp(ctx.bottom->name(ctx.bottom), "Update") == 0) {
      // make sure we are not Set-ing a Ref
      // meaning target memory could be any shard temporary buffer, yet Set will
      // try to deallocate it/manage it
      if (ctx.references.count(name)) {
        // Error
        auto err = fmt::format("Update variable name already used as Ref. Overwriting a previously Ref variable with Update is "
                               "not allowed, name: {}",
                               name);
        ctx.cb(ctx.bottom, SHStringWithLen{err.data(), err.size()}, false, ctx.userData);
      }
    } else if (strcmp(ctx.bottom->name(ctx.bottom), "Push") == 0) {
      // make sure we are not Push-ing a Ref
      // meaning target memory could be any shard temporary buffer, yet Push
      // will try to deallocate it/manage it
      if (ctx.references.count(name)) {
        // Error
        auto err = fmt::format(
            "Push variable name already used as Ref. Overwriting a previously Ref variable with Push is not allowed, name: {}",
            name);
        ctx.cb(ctx.bottom, SHStringWithLen{err.data(), err.size()}, false, ctx.userData);
      }
      ctx.variables.insert(name);
    }
  }

  // Finally do checks on what we consume
  auto requiredVar = ctx.bottom->requiredVariables(ctx.bottom);

  std::unordered_map<std::string, SHExposedTypeInfo> requiredVars;
  for (uint32_t i = 0; requiredVar.len > i; i++) {
    auto &required_param = requiredVar.elements[i];
    std::string name(required_param.name);
    requiredVars[name] = required_param;
  }

  // make sure we have the vars we need, collect first
  for (const auto &required : requiredVars) {
    auto matching = false;
    SHExposedTypeInfo match{};

    const auto &required_param = required.second;
    std::string_view name(required_param.name);
    if (name.find(' ') != std::string::npos) { // take only the first part of variable name
      // the remaining should be a table key which we don't care here
      name = name.substr(0, name.find(' '));
    }

    auto end = ctx.exposed.end();
    auto findIt = ctx.exposed.find(name);
    if (findIt == end) {
      end = ctx.inherited.end();
      findIt = ctx.inherited.find(name);
    }
    if (findIt == end) {
      auto err = fmt::format("Required variable not found: {}", name);
      // Warning only, delegate compose to decide
      ctx.cb(ctx.bottom, SHStringWithLen{err.data(), err.size()}, true, ctx.userData);
    } else {
      auto exposedType = findIt->second.exposedType;
      auto requiredType = required_param.exposedType;
      // Finally deep compare types
      if (matchTypes(exposedType, requiredType, false, true)) {
        matching = true;
      }
    }

    if (matching) {
      match = required_param;
    }

    if (!matching) {
      std::stringstream ss;
      ss << "Required types do not match currently exposed ones for variable '" << required.first
         << "' required possible types: ";
      auto &type = required.second;
      ss << "{\"" << type.name << "\" (" << type.exposedType << ")} ";

      ss << "exposed types: ";
      for (const auto &info : ctx.exposed) {
        auto &type = info.second;
        ss << "{\"" << type.name << "\" (" << type.exposedType << ")} ";
      }
      for (const auto &info : ctx.inherited) {
        auto &type = info.second;
        ss << "{\"" << type.name << "\" (" << type.exposedType << ")} ";
      }
      auto sss = ss.str();
      ctx.cb(ctx.bottom, SHStringWithLen{sss.data(), sss.size()}, false, ctx.userData);
    } else {
      // Add required stuff that we do not expose ourself
      if (ctx.exposed.find(match.name) == ctx.exposed.end())
        ctx.required.emplace(match);
    }
  }
}

SHComposeResult composeWire(const std::vector<Shard *> &wire, SHValidationCallback callback, void *userData,
                            SHInstanceData data) {
  ValidationContext ctx{};
  ctx.originalInputType = data.inputType;
  ctx.previousOutputType = data.inputType;
  ctx.cb = callback;
  ctx.wire = data.wire;
  ctx.userData = userData;
  ctx.onWorkerThread = data.onWorkerThread;
  ctx.fullRequired = reinterpret_cast<decltype(ValidationContext::fullRequired)>(data.requiredVariables);

  // add externally added variables
  if (ctx.wire) {
    for (const auto &[key, pVar] : ctx.wire->externalVariables) {
      SHVar &var = *pVar;
      assert((var.flags & SHVAR_FLAGS_EXTERNAL) != 0);

      auto hash = deriveTypeHash(var);
      TypeInfo *info = nullptr;
      if (ctx.wire->typesCache.find(hash) == ctx.wire->typesCache.end()) {
        info = &ctx.wire->typesCache.emplace(hash, TypeInfo(var, data)).first->second;
      } else {
        info = &ctx.wire->typesCache.at(hash);
      }

      ctx.inherited[key] = SHExposedTypeInfo{key.c_str(), {}, *info, true /* mutable */};
    }
  }

  if (data.shared.elements) {
    for (uint32_t i = 0; i < data.shared.len; i++) {
      auto &info = data.shared.elements[i];
      ctx.inherited[info.name] = info;
    }
  }

  size_t chsize = wire.size();
  for (size_t i = 0; i < chsize; i++) {
    Shard *blk = wire[i];
    ctx.next = nullptr;
    if (i < chsize - 1)
      ctx.next = wire[i + 1];

    if (strcmp(blk->name(blk), "Input") == 0) {
      // Hard code behavior for Input shard and And and Or, in order to validate
      // with actual wire input the followup
      ctx.previousOutputType = ctx.wire->inputType;
    } else if (strcmp(blk->name(blk), "And") == 0 || strcmp(blk->name(blk), "Or") == 0) {
      // Hard code behavior for Input shard and And and Or, in order to validate
      // with actual wire input the followup
      ctx.previousOutputType = ctx.originalInputType;
    } else {
      ctx.bottom = blk;
      try {
        validateConnection(ctx);
      } catch (...) {
        SHLOG_ERROR("Error validating shard: {}, line: {}, column: {}, wire: {}", blk->name(blk), blk->line, blk->column,
                    ctx.wire ? ctx.wire->name : "(unwired)");
        throw;
      }
    }
  }

  SHComposeResult result = {ctx.previousOutputType};

  for (auto &exposed : ctx.exposed) {
    shards::arrayPush(result.exposedInfo, exposed.second);
  }

  if (ctx.fullRequired) {
    for (auto &req : ctx.required) {
      shards::arrayPush(result.requiredInfo, req);
      (*ctx.fullRequired)[req.name] = req;
    }
  } else {
    for (auto &req : ctx.required) {
      shards::arrayPush(result.requiredInfo, req);
    }
  }

  if (wire.size() > 0) {
    auto &last = wire.back();
    if (strcmp(last->name(last), "Restart") == 0 || strcmp(last->name(last), "Stop") == 0 ||
        strcmp(last->name(last), "Return") == 0 || strcmp(last->name(last), "Fail") == 0) {
      result.flowStopper = true;
    }
  }

  return result;
}

SHComposeResult composeWire(const SHWire *wire, SHValidationCallback callback, void *userData, SHInstanceData data) {
  // settle input type of wire before compose
  if (wire->shards.size() > 0 && strncmp(wire->shards[0]->name(wire->shards[0]), "Expect", 6) == 0) {
    // If first shard is an Expect, this wire can accept ANY input type as the type is checked at runtime
    wire->inputType = SHTypeInfo{SHType::Any};
    wire->ignoreInputTypeCheck = true;
  } else if (wire->shards.size() > 0 && !std::any_of(wire->shards.begin(), wire->shards.end(), [&](const auto &shard) {
               return strcmp(shard->name(shard), "Input") == 0;
             })) {
    // If first shard is a plain None, mark this wire has None input
    // But make sure we have no (Input) shards
    auto inTypes = wire->shards[0]->inputTypes(wire->shards[0]);
    if (inTypes.len == 1 && inTypes.elements[0].basicType == SHType::None) {
      wire->inputType = SHTypeInfo{};
      wire->ignoreInputTypeCheck = true;
    } else {
      wire->inputType = data.inputType;
      wire->ignoreInputTypeCheck = false;
    }
  } else {
    wire->inputType = data.inputType;
    wire->ignoreInputTypeCheck = false;
  }

  assert(wire == data.wire); // caller must pass the same wire as data.wire

  auto res = composeWire(wire->shards, callback, userData, data);

  // set output type
  wire->outputType = res.outputType;

  const_cast<SHWire *>(wire)->dispatcher.trigger(SHWire::OnComposedEvent{wire});

  return res;
}

SHComposeResult composeWire(const Shards wire, SHValidationCallback callback, void *userData, SHInstanceData data) {
  std::vector<Shard *> shards;
  for (uint32_t i = 0; wire.len > i; i++) {
    shards.push_back(wire.elements[i]);
  }
  return composeWire(shards, callback, userData, data);
}

SHComposeResult composeWire(const SHSeq wire, SHValidationCallback callback, void *userData, SHInstanceData data) {
  std::vector<Shard *> shards;
  for (uint32_t i = 0; wire.len > i; i++) {
    shards.push_back(wire.elements[i].payload.shardValue);
  }
  return composeWire(shards, callback, userData, data);
}

void freeDerivedInfo(SHTypeInfo info) {
  switch (info.basicType) {
  case SHType::Seq: {
    for (uint32_t i = 0; info.seqTypes.len > i; i++) {
      freeDerivedInfo(info.seqTypes.elements[i]);
    }
    shards::arrayFree(info.seqTypes);
  } break;
  case SHType::Set: {
    for (uint32_t i = 0; info.setTypes.len > i; i++) {
      freeDerivedInfo(info.setTypes.elements[i]);
    }
    shards::arrayFree(info.setTypes);
  } break;
  case SHType::Table: {
    for (uint32_t i = 0; info.table.types.len > i; i++) {
      freeDerivedInfo(info.table.types.elements[i]);
    }
    for (uint32_t i = 0; info.table.keys.len > i; i++) {
      destroyVar(info.table.keys.elements[i]);
    }
    shards::arrayFree(info.table.types);
    shards::arrayFree(info.table.keys);
  } break;
  default:
    break;
  };
}

SHTypeInfo deriveTypeInfo(const SHVar &value, const SHInstanceData &data, std::vector<SHExposedTypeInfo> *expInfo) {
  // We need to guess a valid SHTypeInfo for this var in order to validate
  // Build a SHTypeInfo for the var
  // this is not complete at all, missing Array and SHType::ContextVar for example
  SHTypeInfo varType{};
  varType.basicType = value.valueType;
  varType.innerType = value.innerType;
  switch (value.valueType) {
  case SHType::Object: {
    varType.object.vendorId = value.payload.objectVendorId;
    varType.object.typeId = value.payload.objectTypeId;
    break;
  }
  case SHType::Enum: {
    varType.enumeration.vendorId = value.payload.enumVendorId;
    varType.enumeration.typeId = value.payload.enumTypeId;
    break;
  }
  case SHType::Seq: {
    std::unordered_set<SHTypeInfo> types;
    for (uint32_t i = 0; i < value.payload.seqValue.len; i++) {
      auto derived = deriveTypeInfo(value.payload.seqValue.elements[i], data, expInfo);
      if (!types.count(derived)) {
        shards::arrayPush(varType.seqTypes, derived);
        types.insert(derived);
      }
    }
    varType.fixedSize = value.payload.seqValue.len;
    break;
  }
  case SHType::Table: {
    auto &t = value.payload.tableValue;
    SHTableIterator tit;
    t.api->tableGetIterator(t, &tit);
    SHVar k;
    SHVar v;
    while (t.api->tableNext(t, &tit, &k, &v)) {
      auto derived = deriveTypeInfo(v, data, expInfo);
      shards::arrayPush(varType.table.types, derived);
      auto idx = varType.table.keys.len;
      shards::arrayResize(varType.table.keys, idx + 1);
      cloneVar(varType.table.keys.elements[idx], k);
    }
  } break;
  case SHType::Set: {
    auto &s = value.payload.setValue;
    SHSetIterator sit;
    s.api->setGetIterator(s, &sit);
    SHVar v;
    while (s.api->setNext(s, &sit, &v)) {
      auto derived = deriveTypeInfo(v, data, expInfo);
      shards::arrayPush(varType.setTypes, derived);
    }
  } break;
  case SHType::ContextVar: {
    if (expInfo) {
      auto sv = SHSTRVIEW(value);
      const auto varName = sv;
      for (auto info : data.shared) {
        std::string_view name(info.name);
        if (name == sv) {
          expInfo->push_back(SHExposedTypeInfo{.name = info.name, .exposedType = info.exposedType});
          return cloneTypeInfo(info.exposedType);
        }
      }
      SHLOG_ERROR("Could not find variable {} when deriving type info", varName);
      throw std::runtime_error("Could not find variable when deriving type info");
    }
    // if we reach this point, no variable was found...
    // not our job to trigger errors, just continue
    // specifically we are used to verify parameters as well
    // and in that case data is empty
  } break;
  default:
    break;
  };
  return varType;
}

SHTypeInfo cloneTypeInfo(const SHTypeInfo &other) {
  // We need to guess a valid SHTypeInfo for this var in order to validate
  // Build a SHTypeInfo for the var
  // this is not complete at all, missing Array and SHType::ContextVar for example
  SHTypeInfo varType;
  memcpy(&varType, &other, sizeof(SHTypeInfo));
  switch (varType.basicType) {
  case SHType::ContextVar:
  case SHType::Set:
  case SHType::Seq: {
    varType.seqTypes = {};
    for (uint32_t i = 0; i < other.seqTypes.len; i++) {
      auto cloned = cloneTypeInfo(other.seqTypes.elements[i]);
      shards::arrayPush(varType.seqTypes, cloned);
    }
    break;
  }
  case SHType::Table: {
    varType.table = {};
    for (uint32_t i = 0; i < other.table.types.len; i++) {
      auto cloned = cloneTypeInfo(other.table.types.elements[i]);
      shards::arrayPush(varType.table.types, cloned);
    }
    for (uint32_t i = 0; i < other.table.keys.len; i++) {
      auto idx = varType.table.keys.len;
      shards::arrayResize(varType.table.keys, idx + 1);
      cloneVar(varType.table.keys.elements[idx], other.table.keys.elements[i]);
    }
    break;
  }
  default:
    break;
  };
  return varType;
}

// this is potentially called from unsafe code (e.g. networking)
// let's do some crude stack protection here
thread_local int deriveTypeHashRecursionCounter;
constexpr int MAX_DERIVED_TYPE_HASH_RECURSION = 100;

uint64_t _deriveTypeHash(const SHVar &value);

void updateTypeHash(const SHVar &var, XXH3_state_s *state) {
  // this is not complete at all, missing Array and SHType::ContextVar for example
  XXH3_64bits_update(state, &var.valueType, sizeof(var.valueType));
  XXH3_64bits_update(state, &var.innerType, sizeof(var.innerType));

  switch (var.valueType) {
  case SHType::Object: {
    XXH3_64bits_update(state, &var.payload.objectVendorId, sizeof(var.payload.objectVendorId));
    XXH3_64bits_update(state, &var.payload.objectTypeId, sizeof(var.payload.objectTypeId));
    break;
  }
  case SHType::Enum: {
    XXH3_64bits_update(state, &var.payload.enumVendorId, sizeof(var.payload.enumVendorId));
    XXH3_64bits_update(state, &var.payload.enumTypeId, sizeof(var.payload.enumTypeId));
    break;
  }
  case SHType::Seq: {
    std::set<uint64_t, std::less<uint64_t>, stack_allocator<uint64_t>> hashes;
    for (uint32_t i = 0; i < var.payload.seqValue.len; i++) {
      auto typeHash = _deriveTypeHash(var.payload.seqValue.elements[i]);
      hashes.insert(typeHash);
    }
    constexpr auto recursive = false;
    XXH3_64bits_update(state, &recursive, sizeof(recursive));
    for (const uint64_t hash : hashes) {
      XXH3_64bits_update(state, &hash, sizeof(uint64_t));
    }
  } break;
  case SHType::Table: {
    auto &t = var.payload.tableValue;
    SHTableIterator tit;
    t.api->tableGetIterator(t, &tit);
    SHVar k;
    SHVar v;
    while (t.api->tableNext(t, &tit, &k, &v)) {
      auto hk = shards::hash(k);
      XXH3_64bits_update(state, &hk.payload.int2Value, sizeof(SHInt2));
      auto hv = _deriveTypeHash(v);
      XXH3_64bits_update(state, &hv, sizeof(uint64_t));
    }
  } break;
  case SHType::Set: {
    // this is unsafe because allocates on the stack
    // but we need to sort hashes
    std::set<uint64_t, std::less<uint64_t>, stack_allocator<uint64_t>> hashes;
    // set is unordered so just collect
    auto &s = var.payload.setValue;
    SHSetIterator sit;
    s.api->setGetIterator(s, &sit);
    SHVar v;
    while (s.api->setNext(s, &sit, &v)) {
      hashes.insert(_deriveTypeHash(v));
    }
    constexpr auto recursive = false;
    XXH3_64bits_update(state, &recursive, sizeof(recursive));
    for (const uint64_t hash : hashes) {
      XXH3_64bits_update(state, &hash, sizeof(uint64_t));
    }
  } break;
  default:
    break;
  };
}

uint64_t _deriveTypeHash(const SHVar &value) {
  if (deriveTypeHashRecursionCounter >= MAX_DERIVED_TYPE_HASH_RECURSION)
    throw SHException("deriveTypeHash maximum recursion exceeded");
  deriveTypeHashRecursionCounter++;
  DEFER(deriveTypeHashRecursionCounter--);

  XXH3_state_s hashState;
  XXH3_INITSTATE(&hashState);

  XXH3_64bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret, XXH_SECRET_DEFAULT_SIZE);

  updateTypeHash(value, &hashState);

  return XXH3_64bits_digest(&hashState);
}

uint64_t deriveTypeHash(const SHVar &value) {
  deriveTypeHashRecursionCounter = 0;
  return _deriveTypeHash(value);
}

uint64_t deriveTypeHash(const SHTypeInfo &value);

void updateTypeHash(const SHTypeInfo &t, XXH3_state_s *state) {
  XXH3_64bits_update(state, &t.basicType, sizeof(t.basicType));
  XXH3_64bits_update(state, &t.innerType, sizeof(t.innerType));

  switch (t.basicType) {
  case SHType::Object: {
    XXH3_64bits_update(state, &t.object.vendorId, sizeof(t.object.vendorId));
    XXH3_64bits_update(state, &t.object.typeId, sizeof(t.object.typeId));
  } break;
  case SHType::Enum: {
    XXH3_64bits_update(state, &t.enumeration.vendorId, sizeof(t.enumeration.vendorId));
    XXH3_64bits_update(state, &t.enumeration.typeId, sizeof(t.enumeration.typeId));
  } break;
  case SHType::Seq: {
    // this is unsafe because allocates on the stack, but faster...
    std::set<uint64_t, std::less<uint64_t>, stack_allocator<uint64_t>> hashes;
    bool recursive = false;

    for (uint32_t i = 0; i < t.seqTypes.len; i++) {
      if (t.seqTypes.elements[i].recursiveSelf) {
        recursive = true;
      } else {
        auto typeHash = deriveTypeHash(t.seqTypes.elements[i]);
        hashes.insert(typeHash);
      }
    }
    XXH3_64bits_update(state, &recursive, sizeof(recursive));
    for (const auto hash : hashes) {
      XXH3_64bits_update(state, &hash, sizeof(hash));
    }
  } break;
  case SHType::Table: {
    if (t.table.keys.len == t.table.types.len) {
      for (uint32_t i = 0; i < t.table.types.len; i++) {
        auto keyHash = shards::hash(t.table.keys.elements[i]);
        XXH3_64bits_update(state, &keyHash.payload.int2Value, sizeof(SHInt2));
        auto typeHash = deriveTypeHash(t.table.types.elements[i]);
        XXH3_64bits_update(state, &typeHash, sizeof(uint64_t));
      }
    } else {
      std::set<uint64_t, std::less<uint64_t>, stack_allocator<uint64_t>> hashes;
      for (uint32_t i = 0; i < t.table.types.len; i++) {
        auto typeHash = deriveTypeHash(t.table.types.elements[i]);
        hashes.insert(typeHash);
      }
      for (const auto hash : hashes) {
        XXH3_64bits_update(state, &hash, sizeof(hash));
      }
    }
  } break;
  case SHType::Set: {
    // this is unsafe because allocates on the stack, but faster...
    std::set<uint64_t, std::less<uint64_t>, stack_allocator<uint64_t>> hashes;
    bool recursive = false;
    for (uint32_t i = 0; i < t.setTypes.len; i++) {
      if (t.setTypes.elements[i].recursiveSelf) {
        recursive = true;
      } else {
        auto typeHash = deriveTypeHash(t.setTypes.elements[i]);
        hashes.insert(typeHash);
      }
    }
    XXH3_64bits_update(state, &recursive, sizeof(recursive));
    for (const auto hash : hashes) {
      XXH3_64bits_update(state, &hash, sizeof(hash));
    }
  } break;
  default:
    break;
  };
}

uint64_t deriveTypeHash(const SHTypeInfo &value) {
  XXH3_state_s hashState;
  XXH3_INITSTATE(&hashState);

  XXH3_64bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret, XXH_SECRET_DEFAULT_SIZE);

  updateTypeHash(value, &hashState);

  return XXH3_64bits_digest(&hashState);
}

bool validateSetParam(Shard *shard, int index, const SHVar &value, SHValidationCallback callback, void *userData) {
  auto params = shard->parameters(shard);
  if (params.len <= (uint32_t)index) {
    std::string_view err("Parameter index out of range");
    callback(shard, SHStringWithLen{err.data(), err.size()}, false, userData);
    return false;
  }

  auto param = params.elements[index];

  // Build a SHTypeInfo for the var
  SHInstanceData data{};
  auto varType = deriveTypeInfo(value, data);
  DEFER({ freeDerivedInfo(varType); });

  for (uint32_t i = 0; param.valueTypes.len > i; i++) {
    // This only does a quick check to see if the type is roughly correct
    // ContextVariable types will be checked in validateConnection based on requiredVariables
    if (matchTypes(varType, param.valueTypes.elements[i], true, true)) {
      return true; // we are good just exit
    }
  }

  std::string err(fmt::format("Parameter {} not accepting this kind of variable: {} (valid types: {})", param.name, varType,
                              param.valueTypes));
  callback(shard, SHStringWithLen{err.data(), err.size()}, false, userData);

  return false;
}

void error_handler(int err_sig) {
  std::signal(err_sig, SIG_DFL);

  auto printTrace = false;

  switch (err_sig) {
  case SIGINT:
  case SIGTERM:
    SHLOG_INFO("Exiting due to INT/TERM signal");
    shards::GetGlobals().SigIntTerm++;
    if (shards::GetGlobals().SigIntTerm > 5)
      std::exit(-1);
    spdlog::shutdown();
    break;
  case SIGFPE:
    SHLOG_ERROR("Fatal SIGFPE");
    printTrace = true;
    break;
  case SIGILL:
    SHLOG_ERROR("Fatal SIGILL");
    printTrace = true;
    break;
  case SIGABRT:
    SHLOG_ERROR("Fatal SIGABRT");
    printTrace = true;
    break;
  case SIGSEGV:
    SHLOG_ERROR("Fatal SIGSEGV");
    printTrace = true;
    break;
  }

  if (printTrace) {
#ifndef __EMSCRIPTEN__
    SHLOG_ERROR(boost::stacktrace::stacktrace());
#endif
  }

  std::raise(err_sig);
}

#ifdef _WIN32
#include "debugapi.h"
static bool isDebuggerPresent() { return (bool)IsDebuggerPresent(); }
#else
constexpr bool isDebuggerPresent() { return false; }
#endif

void installSignalHandlers() {
  if (!isDebuggerPresent()) {
    std::signal(SIGINT, &error_handler);
    std::signal(SIGTERM, &error_handler);
    std::signal(SIGFPE, &error_handler);
    std::signal(SIGILL, &error_handler);
    std::signal(SIGABRT, &error_handler);
    std::signal(SIGSEGV, &error_handler);
  }
}

Weave &Weave::shard(std::string_view name, std::vector<Var> params) {
  auto blk = createShard(name.data());
  if (!blk) {
    SHLOG_ERROR("Shard {} was not found", name);
    throw SHException("Shard not found");
  }

  blk->setup(blk);

  const auto psize = params.size();
  for (size_t i = 0; i < psize; i++) {
    // skip Any, as they mean default value
    if (params[i] != Var::Any)
      blk->setParam(blk, int(i), &params[i]);
  }

  _shards.emplace_back(blk);
  return *this;
}

Weave &Weave::let(Var value) {
  auto blk = createShard("Const");
  blk->setup(blk);
  blk->setParam(blk, 0, &value);
  _shards.emplace_back(blk);
  return *this;
}

Wire::Wire(std::string_view name) : _wire(SHWire::make(name)) {}

Wire &Wire::looped(bool looped) {
  _wire->looped = looped;
  return *this;
}

Wire &Wire::unsafe(bool unsafe) {
  _wire->unsafe = unsafe;
  return *this;
}

Wire &Wire::stackSize(size_t stackSize) {
  _wire->stackSize = stackSize;
  return *this;
}

Wire &Wire::name(std::string_view name) {
  _wire->name = name;
  return *this;
}

Wire &Wire::shard(std::string_view name, std::vector<Var> params) {
  auto blk = createShard(name.data());
  if (!blk) {
    SHLOG_ERROR("Shard {} was not found", name);
    throw SHException("Shard not found");
  }

  blk->setup(blk);

  const auto psize = params.size();
  for (size_t i = 0; i < psize; i++) {
    // skip Any, as they mean default value
    if (params[i] != Var::Any)
      blk->setParam(blk, int(i), &params[i]);
  }

  _wire->addShard(blk);
  return *this;
}

Wire &Wire::let(Var value) {
  auto blk = createShard("Const");
  blk->setup(blk);
  blk->setParam(blk, 0, &value);
  _wire->addShard(blk);
  return *this;
}

SHWireRef Wire::weakRef() const { return SHWire::weakRef(_wire); }

Var::Var(const Wire &wire) : Var(wire.weakRef()) {}

SHRunWireOutput runWire(SHWire *wire, SHContext *context, const SHVar &wireInput) {
  ZoneScoped;
  ZoneName(wire->name.c_str(), wire->name.size());

  memset(&wire->previousOutput, 0x0, sizeof(SHVar));
  wire->currentInput = wireInput;
  wire->state = SHWire::State::Iterating;
  wire->context = context;
  DEFER({ wire->state = SHWire::State::IterationEnded; });

  wire->dispatcher.trigger(SHWire::OnUpdateEvent{wire});

  try {
    auto state =
        shardsActivation<std::vector<ShardPtr>, false, false>(wire->shards, context, wire->currentInput, wire->previousOutput);
    switch (state) {
    case SHWireState::Return:
      return {context->getFlowStorage(), SHRunWireOutputState::Stopped};
    case SHWireState::Restart:
      return {context->getFlowStorage(), SHRunWireOutputState::Restarted};
    case SHWireState::Error:
      // shardsActivation handles error logging and such
      assert(context->failed());
      return {wire->previousOutput, SHRunWireOutputState::Failed};
    case SHWireState::Stop:
      assert(!context->failed());
      return {context->getFlowStorage(), SHRunWireOutputState::Stopped};
    case SHWireState::Rebase:
      // Handled inside shardsActivation
      SHLOG_FATAL("Invalid wire state");
    case SHWireState::Continue:
      break;
    }
  }
#ifndef __EMSCRIPTEN__
  catch (boost::context::detail::forced_unwind const &e) {
    throw; // required for Boost Coroutine!
  }
#endif
  catch (...) {
    // shardsActivation handles error logging and such
    return {wire->previousOutput, SHRunWireOutputState::Failed};
  }

  return {wire->previousOutput, SHRunWireOutputState::Running};
}

#ifndef __EMSCRIPTEN__
boost::context::continuation run(SHWire *wire, SHFlow *flow, boost::context::continuation &&sink)
#else
void run(SHWire *wire, SHFlow *flow, SHCoro *coro)
#endif
{
  SHLOG_DEBUG("Wire {} rolling", wire->name);

  auto running = true;

  // we need this cos by the end of this call we might get suspended/resumed and state changes! this wont
  bool failed = false;

  // Reset state
  wire->state = SHWire::State::Prepared;
  wire->finishedOutput = Var::Empty;
  wire->finishedError.clear();

  // Create a new context and copy the sink in
  SHFlow anonFlow{wire};
#ifndef __EMSCRIPTEN__
  SHContext context(std::move(sink), wire, flow ? flow : &anonFlow);
#else
  SHContext context(coro, wire, flow ? flow : &anonFlow);
#endif

  // if the wire had a context (Stepped wires in wires.cpp)
  // copy some stuff from it
  if (wire->context) {
    context.wireStack = wire->context->wireStack;
    // need to add back ourself
    context.wireStack.push_back(wire);
  }

  // also populate context in wire
  wire->context = &context;

  // We pre-rolled our coro, suspend here before actually starting.
  // This allows us to allocate the stack ahead of time.
  // And call warmup on all the shards!
  try {
    wire->warmup(&context);
  } catch (...) {
    // inside warmup we re-throw, we handle logging and such there
    wire->state = SHWire::State::Failed;
    SHLOG_ERROR("Wire {} warmup failed", wire->name);
    goto endOfWire;
  }

// yield after warming up
#ifndef __EMSCRIPTEN__
  context.continuation = context.continuation.resume();
#else
  context.continuation->yield();
#endif

  // RESUMING

  SHLOG_DEBUG("Wire {} starting", wire->name);

  if (context.shouldStop()) {
    // We might have stopped before even starting!
    SHLOG_ERROR("Wire {} stopped before starting", wire->name);
    goto endOfWire;
  }

  wire->dispatcher.trigger(SHWire::OnStartEvent{wire});

  while (running) {
    running = wire->looped;

    // reset context state
    context.continueFlow();

    auto runRes = runWire(wire, &context, wire->currentInput);
    if (unlikely(runRes.state == SHRunWireOutputState::Failed)) {
      SHLOG_DEBUG("Wire {} failed", wire->name);
      wire->state = SHWire::State::Failed;
      failed = true;
      context.stopFlow(runRes.output);
      break;
    } else if (unlikely(runRes.state == SHRunWireOutputState::Stopped)) {
      SHLOG_DEBUG("Wire {} stopped", wire->name);
      context.stopFlow(runRes.output);
      // also replace the previous output with actual output
      // as it's likely coming from flowStorage of context!
      wire->previousOutput = runRes.output;
      break;
    } else if (unlikely(runRes.state == SHRunWireOutputState::Restarted)) {
      // must clone over currentInput!
      // restart overwrites currentInput on purpose
      wire->currentInput = context.getFlowStorage();
    }

    if (!wire->unsafe && wire->looped) {
      // Ensure no while(true), yield anyway every run
      context.next = SHDuration(0);
#ifndef __EMSCRIPTEN__
      context.continuation = context.continuation.resume();
#else
      context.continuation->yield();
#endif

      // RESUMING

      // This is delayed upon continuation!!
      if (context.shouldStop()) {
        SHLOG_DEBUG("Wire {} aborted on resume", wire->name);
        break;
      }
    }
  }

endOfWire:
  wire->finishedOutput = wire->previousOutput;
  if (failed || context.failed()) {
    wire->finishedError = context.getErrorMessage();
    if (wire->finishedError.empty()) {
      wire->finishedError = "Generic error";
    }
    SHLOG_DEBUG("Wire {} failed with error {}", wire->name, wire->finishedError);
  }

  // run cleanup on all the shards
  // ensure stop state is set
  context.stopFlow(wire->previousOutput);
  wire->cleanup(true);

  // Need to take care that we might have stopped the wire very early due to
  // errors and the next eventual stop() should avoid resuming
  if (wire->state != SHWire::State::Failed)
    wire->state = SHWire::State::Ended;

  SHLOG_DEBUG("Wire {} ended", wire->name);

  wire->dispatcher.trigger(SHWire::OnStopEvent{wire});

#ifndef __EMSCRIPTEN__
  return std::move(context.continuation);
#else
  context.continuation->yield();
#endif

  // we should never resume here!
  SHLOG_FATAL("Wire {} resumed after ending", wire->name);
}

Globals &GetGlobals() {
  static Globals globals;
  return globals;
}

static UntrackedUnorderedMap<std::string, EventDispatcher> dispatchers;
static std::shared_mutex mutex;
EventDispatcher &getEventDispatcher(const std::string &name) {

  std::shared_lock<decltype(mutex)> _l(mutex);
  auto it = dispatchers.find(name);
  if (it == dispatchers.end()) {
    _l.unlock();
    std::scoped_lock<decltype(mutex)> _l1(mutex);
    auto &result = dispatchers[name];
    result.name = name;
    return result;
  } else {
    return it->second;
  }
}

NO_INLINE void _destroyVarSlow(SHVar &var) {
  switch (var.valueType) {
  case SHType::String:
  case SHType::Path:
  case SHType::ContextVar:
#if 0
    assert(var.payload.stringCapacity >= 7 && "string capacity is too small, it should be at least 7");
    if (var.payload.stringCapacity > 7) {
      delete[] var.payload.stringValue;
    } else {
      memset(var.shortString, 0, 7);
      assert(var.shortString[7] == 0 && "0 terminator should be 0 always");
    }
#else
    delete[] var.payload.stringValue;
#endif
    break;
  case SHType::Bytes:
#if 0
    assert(var.payload.bytesCapacity >= 8 && "bytes capacity is too small, it should be at least 8");
    if (var.payload.bytesCapacity > 8) {
      delete[] var.payload.bytesValue;
    } else {
      memset(var.shortBytes, 0, 8);
    }
#else
    delete[] var.payload.bytesValue;
#endif
    break;
  case SHType::Seq: {
    // notice we use .cap! because we make sure to 0 new empty elements
    for (size_t i = var.payload.seqValue.cap; i > 0; i--) {
      destroyVar(var.payload.seqValue.elements[i - 1]);
    }
    shards::arrayFree(var.payload.seqValue);
  } break;
  case SHType::Table: {
    assert(var.payload.tableValue.api == &GetGlobals().TableInterface);
    assert(var.payload.tableValue.opaque);
    auto map = (SHMap *)var.payload.tableValue.opaque;
    delete map;
    var.version = 0;
  } break;
  case SHType::Set: {
    assert(var.payload.setValue.api == &GetGlobals().SetInterface);
    assert(var.payload.setValue.opaque);
    auto set = (SHHashSet *)var.payload.setValue.opaque;
    delete set;
  } break;
  case SHType::ShardRef:
    decRef(var.payload.shardValue);
    break;
  case SHType::Type:
    freeDerivedInfo(*var.payload.typeValue);
    delete var.payload.typeValue;
    break;
  default:
    break;
  };
}

NO_INLINE void _cloneVarSlow(SHVar &dst, const SHVar &src) {
  assert((dst.flags & SHVAR_FLAGS_FOREIGN) != SHVAR_FLAGS_FOREIGN && "cannot clone into a foreign var");
  switch (src.valueType) {
  case SHType::Seq: {
    uint32_t srcLen = src.payload.seqValue.len;

    // try our best to re-use memory
    if (dst.valueType != SHType::Seq) {
      destroyVar(dst);
      dst.valueType = SHType::Seq;
    }

    shards::arrayResize(dst.payload.seqValue, srcLen);

    if (src.payload.seqValue.elements == dst.payload.seqValue.elements)
      return;

    for (uint32_t i = 0; i < srcLen; i++) {
      const auto &subsrc = src.payload.seqValue.elements[i];
      cloneVar(dst.payload.seqValue.elements[i], subsrc);
    }
  } break;
  case SHType::Path:
  case SHType::ContextVar:
  case SHType::String: {
    auto srcSize = src.payload.stringLen > 0 || src.payload.stringValue == nullptr ? src.payload.stringLen
                                                                                   : uint32_t(strlen(src.payload.stringValue));
    if (dst.valueType != src.valueType || dst.payload.stringCapacity < srcSize) {
      destroyVar(dst);
      dst.valueType = src.valueType;
#if 0
      if (srcSize <= 7) {
        // short string, no need to allocate
        // capacity is 8 but last is 0 terminator
        dst.payload.stringValue = dst.shortString;
        dst.payload.stringCapacity = 7; // this also marks it as short string, lucky 7
      } else
#endif
      {
        // allocate a 0 terminator too
        dst.payload.stringValue = new char[srcSize + 1];
        dst.payload.stringCapacity = srcSize;
      }
    } else {
      if (src.payload.stringValue == dst.payload.stringValue && src.payload.stringLen == dst.payload.stringLen)
        return;
    }

    if (srcSize > 0) {
      assert(src.payload.stringValue != nullptr && "string value is null but length is not 0");
      memcpy((void *)dst.payload.stringValue, (void *)src.payload.stringValue, srcSize);
    }

    assert(dst.payload.stringValue && "destination stringValue cannot be null");

    // make sure to 0 terminate
    ((char *)dst.payload.stringValue)[srcSize] = 0;

    // fill the len field
    dst.payload.stringLen = srcSize;
  } break;
  case SHType::Image: {
    auto spixsize = 1;
    auto dpixsize = 1;
    if ((src.payload.imageValue.flags & SHIMAGE_FLAGS_16BITS_INT) == SHIMAGE_FLAGS_16BITS_INT)
      spixsize = 2;
    else if ((src.payload.imageValue.flags & SHIMAGE_FLAGS_32BITS_FLOAT) == SHIMAGE_FLAGS_32BITS_FLOAT)
      spixsize = 4;
    if ((dst.payload.imageValue.flags & SHIMAGE_FLAGS_16BITS_INT) == SHIMAGE_FLAGS_16BITS_INT)
      dpixsize = 2;
    else if ((dst.payload.imageValue.flags & SHIMAGE_FLAGS_32BITS_FLOAT) == SHIMAGE_FLAGS_32BITS_FLOAT)
      dpixsize = 4;

    size_t srcImgSize = src.payload.imageValue.height * src.payload.imageValue.width * src.payload.imageValue.channels * spixsize;
    size_t dstCapacity =
        dst.payload.imageValue.height * dst.payload.imageValue.width * dst.payload.imageValue.channels * dpixsize;
    if (dst.valueType != SHType::Image || srcImgSize > dstCapacity) {
      destroyVar(dst);
      dst.valueType = SHType::Image;
      dst.payload.imageValue.data = new uint8_t[srcImgSize];
    }

    dst.version++;

    dst.payload.imageValue.flags = src.payload.imageValue.flags;
    dst.payload.imageValue.height = src.payload.imageValue.height;
    dst.payload.imageValue.width = src.payload.imageValue.width;
    dst.payload.imageValue.channels = src.payload.imageValue.channels;

    if (src.payload.imageValue.data == dst.payload.imageValue.data)
      return;

    memcpy(dst.payload.imageValue.data, src.payload.imageValue.data, srcImgSize);
  } break;
  case SHType::Audio: {
    size_t srcSize = src.payload.audioValue.nsamples * src.payload.audioValue.channels * sizeof(float);
    size_t dstCapacity = dst.payload.audioValue.nsamples * dst.payload.audioValue.channels * sizeof(float);
    if (dst.valueType != SHType::Audio || srcSize > dstCapacity) {
      destroyVar(dst);
      dst.valueType = SHType::Audio;
      dst.payload.audioValue.samples = new float[src.payload.audioValue.nsamples * src.payload.audioValue.channels];
    }

    dst.payload.audioValue.sampleRate = src.payload.audioValue.sampleRate;
    dst.payload.audioValue.nsamples = src.payload.audioValue.nsamples;
    dst.payload.audioValue.channels = src.payload.audioValue.channels;

    if (src.payload.audioValue.samples == dst.payload.audioValue.samples)
      return;

    memcpy(dst.payload.audioValue.samples, src.payload.audioValue.samples, srcSize);
  } break;
  case SHType::Table: {
    SHMap *map;
    if (dst.valueType == SHType::Table) {
      // This code checks if the source and destination of a copy are the same.
      if (src.payload.tableValue.opaque == dst.payload.tableValue.opaque)
        return;

      // also we assume mutable tables are of our internal type!!
      assert(dst.payload.tableValue.api == &GetGlobals().TableInterface);

      map = (SHMap *)dst.payload.tableValue.opaque;

      // let's do some magic here, while KISS at same time
      if (dst.payload.tableValue.api == src.payload.tableValue.api) {
        auto sMap = (SHMap *)src.payload.tableValue.opaque;
        if (sMap->size() == map->size()) {
          auto it = sMap->begin();
          auto dit = map->begin();
          // copy values fast, hoping keys are the same
          // we might end up with some extra copies if keys are not the same but
          // given shards nature, it's unlikely it will be the majority of cases
          while (it != sMap->end()) {
            if (it->first != dit->first)
              goto early_exit;

            cloneVar(dit->second, it->second);

            ++it;
            ++dit;
          }
          return;
        }
      }

    early_exit:
      // ok clear destination and copy values the old way
      map->clear();
    } else {
      destroyVar(dst);
      dst.valueType = SHType::Table;
      dst.payload.tableValue.api = &GetGlobals().TableInterface;
      map = new SHMap();
      dst.payload.tableValue.opaque = map;
    }

    dst.version++;

    auto &t = src.payload.tableValue;
    SHTableIterator tit;
    t.api->tableGetIterator(t, &tit);
    SHVar k;
    SHVar v;
    while (t.api->tableNext(t, &tit, &k, &v)) {
      (*map)[k] = v;
    }
  } break;
  case SHType::Set: {
    SHHashSet *set;
    if (dst.valueType == SHType::Set) {
      if (src.payload.setValue.opaque == dst.payload.setValue.opaque)
        return;
      assert(dst.payload.setValue.api == &GetGlobals().SetInterface);
      set = (SHHashSet *)dst.payload.setValue.opaque;
      set->clear();
    } else {
      destroyVar(dst);
      dst.valueType = SHType::Set;
      dst.payload.setValue.api = &GetGlobals().SetInterface;
      set = new SHHashSet();
      dst.payload.setValue.opaque = set;
    }

    auto &s = src.payload.setValue;
    SHSetIterator sit;
    s.api->setGetIterator(s, &sit);
    SHVar v;
    while (s.api->setNext(s, &sit, &v)) {
      (*set).emplace(v);
    }
  } break;
  case SHType::Bytes: {
    if (dst.valueType != SHType::Bytes || dst.payload.bytesCapacity < src.payload.bytesSize) {
      destroyVar(dst);
      dst.valueType = SHType::Bytes;
#if 0
      if (src.payload.bytesSize <= 8) {
        // small bytes are stored directly in the payload
        dst.payload.bytesValue = dst.shortBytes;
        dst.payload.bytesCapacity = 8;
      } else
#endif
      {
        dst.payload.bytesValue = new uint8_t[src.payload.bytesSize];
        dst.payload.bytesCapacity = src.payload.bytesSize;
      }
    } else {
      if (src.payload.bytesValue == dst.payload.bytesValue && src.payload.bytesSize == dst.payload.bytesSize)
        return;
    }

    dst.payload.bytesSize = src.payload.bytesSize;
    memcpy((void *)dst.payload.bytesValue, (void *)src.payload.bytesValue, src.payload.bytesSize);
  } break;
  case SHType::Array: {
    auto srcLen = src.payload.arrayValue.len;

    // try our best to re-use memory
    if (dst.valueType != SHType::Array) {
      destroyVar(dst);
      dst.valueType = SHType::Array;
    }

    if (src.payload.arrayValue.elements == dst.payload.arrayValue.elements)
      return;

    dst.innerType = src.innerType;
    shards::arrayResize(dst.payload.arrayValue, srcLen);
    // array holds only blittables and is packed so a single memcpy is enough
    memcpy(&dst.payload.arrayValue.elements[0], &src.payload.arrayValue.elements[0], sizeof(SHVarPayload) * srcLen);
  } break;
  case SHType::Wire:
    if (dst.valueType == SHType::Wire) {
      auto &aWire = SHWire::sharedFromRef(src.payload.wireValue);
      auto &bWire = SHWire::sharedFromRef(dst.payload.wireValue);
      if (aWire == bWire)
        return;
    }

    destroyVar(dst);

    dst.valueType = SHType::Wire;
    dst.payload.wireValue = SHWire::addRef(src.payload.wireValue);
    break;
  case SHType::ShardRef:
    destroyVar(dst);
    dst.valueType = SHType::ShardRef;
    dst.payload.shardValue = src.payload.shardValue;
    incRef(dst.payload.shardValue);
    break;
  case SHType::Object:
    if (dst != src) {
      destroyVar(dst);
    }

    dst.valueType = SHType::Object;
    dst.payload.objectValue = src.payload.objectValue;
    dst.payload.objectVendorId = src.payload.objectVendorId;
    dst.payload.objectTypeId = src.payload.objectTypeId;

    if ((src.flags & SHVAR_FLAGS_USES_OBJINFO) == SHVAR_FLAGS_USES_OBJINFO && src.objectInfo) {
      // in this case the custom object needs actual destruction
      dst.flags |= SHVAR_FLAGS_USES_OBJINFO;
      dst.objectInfo = src.objectInfo;
      if (src.objectInfo->reference)
        dst.objectInfo->reference(dst.payload.objectValue);
    }
    break;
  case SHType::Type:
    destroyVar(dst);
    dst.payload.typeValue = new SHTypeInfo(cloneTypeInfo(*src.payload.typeValue));
    dst.valueType = SHType::Type;
    break;
  default:
    SHLOG_FATAL("Unhandled type {}", src.valueType);
    break;
  };
}

void _gatherShards(const ShardsCollection &coll, std::vector<ShardInfo> &out, const SHWire *wire) {
  // TODO out should be a set?
  switch (coll.index()) {
  case 0: {
    // wire
    auto wire = std::get<const SHWire *>(coll);
    if (!gatheringWires().count(wire)) {
      gatheringWires().insert(wire);
      for (auto blk : wire->shards) {
        _gatherShards(blk, out, wire);
      }
    }
  } break;
  case 1: {
    // Single shard
    auto blk = std::get<ShardPtr>(coll);
    std::string_view name(blk->name(blk));
    out.emplace_back(name, blk, wire);
    // Also find nested shards
    const auto params = blk->parameters(blk);
    for (uint32_t i = 0; i < params.len; i++) {
      const auto &param = params.elements[i];
      const auto &types = param.valueTypes;
      bool potential = false;
      for (uint32_t j = 0; j < types.len; j++) {
        const auto &type = types.elements[j];
        if (type.basicType == SHType::ShardRef || type.basicType == SHType::Wire) {
          potential = true;
        } else if (type.basicType == SHType::Seq) {
          const auto &stypes = type.seqTypes;
          for (uint32_t k = 0; k < stypes.len; k++) {
            if (stypes.elements[k].basicType == SHType::ShardRef) {
              potential = true;
            }
          }
        }
      }
      if (potential)
        _gatherShards(blk->getParam(blk, i), out, wire);
    }
  } break;
  case 2: {
    // Shards seq
    auto bs = std::get<Shards>(coll);
    for (uint32_t i = 0; i < bs.len; i++) {
      _gatherShards(bs.elements[i], out, wire);
    }
  } break;
  case 3: {
    // Var
    auto var = std::get<SHVar>(coll);
    if (var.valueType == SHType::ShardRef) {
      _gatherShards(var.payload.shardValue, out, wire);
    } else if (var.valueType == SHType::Wire) {
      auto &wire = SHWire::sharedFromRef(var.payload.wireValue);
      _gatherShards(wire.get(), out, wire.get());
    } else if (var.valueType == SHType::Seq) {
      auto bs = var.payload.seqValue;
      for (uint32_t i = 0; i < bs.len; i++) {
        _gatherShards(bs.elements[i], out, wire);
      }
    }
  } break;
  default:
    SHLOG_FATAL("invalid state");
  }
}

void gatherShards(const ShardsCollection &coll, std::vector<ShardInfo> &out) {
  gatheringWiresPush();
  DEFER(gatheringWiresPop());
  _gatherShards(coll, out, coll.index() == 0 ? std::get<const SHWire *>(coll) : nullptr);
}

SHVar hash(const SHVar &var) {
  hashingWiresPush();
  DEFER(hashingWiresPop());

  XXH3_state_s hashState;
  XXH3_INITSTATE(&hashState);
  XXH3_128bits_reset_withSecret(&hashState, CUSTOM_XXH3_kSecret, XXH_SECRET_DEFAULT_SIZE);

  hash_update(var, &hashState);

  auto digest = XXH3_128bits_digest(&hashState);
  return Var(int64_t(digest.low64), int64_t(digest.high64));
}

void hash_update(const SHVar &var, void *state) {
  auto hashState = reinterpret_cast<XXH3_state_s *>(state);

  auto error = XXH3_128bits_update(hashState, &var.valueType, sizeof(var.valueType));
  assert(error == XXH_OK);

  switch (var.valueType) {
  case SHType::Type: {
    updateTypeHash(*var.payload.typeValue, hashState);

  } break;
  case SHType::Bytes: {
    error = XXH3_128bits_update(hashState, var.payload.bytesValue, size_t(var.payload.bytesSize));
    assert(error == XXH_OK);
  } break;
  case SHType::Path:
  case SHType::ContextVar:
  case SHType::String: {
    error = XXH3_128bits_update(hashState, var.payload.stringValue,
                                size_t(var.payload.stringLen > 0 || var.payload.stringValue == nullptr
                                           ? var.payload.stringLen
                                           : strlen(var.payload.stringValue)));
    assert(error == XXH_OK);
  } break;
  case SHType::Image: {
    SHImage i = var.payload.imageValue;
    i.data = nullptr;
    error = XXH3_128bits_update(hashState, &i, sizeof(SHImage));
    assert(error == XXH_OK);
    auto pixsize = getPixelSize(var);
    error = XXH3_128bits_update(
        hashState, var.payload.imageValue.data,
        size_t(var.payload.imageValue.channels * var.payload.imageValue.width * var.payload.imageValue.height * pixsize));
    assert(error == XXH_OK);
  } break;
  case SHType::Audio: {
    SHAudio a = var.payload.audioValue;
    a.samples = nullptr;
    error = XXH3_128bits_update(hashState, &a, sizeof(SHAudio));
    assert(error == XXH_OK);
    error = XXH3_128bits_update(hashState, var.payload.audioValue.samples,
                                size_t(var.payload.audioValue.channels * var.payload.audioValue.nsamples * sizeof(float)));
    assert(error == XXH_OK);
  } break;
  case SHType::Seq: {
    for (uint32_t i = 0; i < var.payload.seqValue.len; i++) {
      hash_update(var.payload.seqValue.elements[i], state);
    }
  } break;
  case SHType::Array: {
    for (uint32_t i = 0; i < var.payload.arrayValue.len; i++) {
      SHVar tmp{}; // only of blittable types and hash uses just type, so no init
                   // needed
      tmp.valueType = var.innerType;
      tmp.payload = var.payload.arrayValue.elements[i];
      hash_update(tmp, state);
    }
  } break;
  case SHType::Table: {
    // table is sorted, do all in 1 iteration
    auto &t = var.payload.tableValue;
    SHTableIterator it;
    t.api->tableGetIterator(t, &it);
    SHVar key;
    SHVar value;
    while (t.api->tableNext(t, &it, &key, &value)) {
      const auto kh = hash(key);
      XXH3_128bits_update(hashState, &kh, sizeof(SHInt2));
      const auto h = hash(value);
      XXH3_128bits_update(hashState, &h, sizeof(SHInt2));
    }
  } break;
  case SHType::Set: {
    boost::container::small_vector<std::pair<uint64_t, uint64_t>, 8> hashes;

    // just store hashes, sort and actually combine later
    auto &s = var.payload.setValue;
    SHSetIterator it;
    s.api->setGetIterator(s, &it);
    SHVar value;
    while (s.api->setNext(s, &it, &value)) {
      const auto h = hash(value);
      hashes.emplace_back(uint64_t(h.payload.int2Value[0]), uint64_t(h.payload.int2Value[1]));
    }

    std::sort(hashes.begin(), hashes.end());
    for (const auto &hash : hashes) {
      XXH3_128bits_update(hashState, &hash, sizeof(uint64_t));
    }
  } break;
  case SHType::ShardRef: {
    auto blk = var.payload.shardValue;
    auto name = blk->name(blk);
    auto error = XXH3_128bits_update(hashState, name, strlen(name));
    assert(error == XXH_OK);

    auto params = blk->parameters(blk);
    for (uint32_t i = 0; i < params.len; i++) {
      auto pval = blk->getParam(blk, int(i));
      hash_update(pval, state);
    }

    if (blk->getState) {
      auto bstate = blk->getState(blk);
      hash_update(bstate, state);
    }
  } break;
  case SHType::Wire: {
    auto wire = SHWire::sharedFromRef(var.payload.wireValue);
    if (hashingWires().count(wire.get()) == 0) {
      hashingWires().insert(wire.get());

      error = XXH3_128bits_update(hashState, wire->name.c_str(), wire->name.length());
      assert(error == XXH_OK);

      error = XXH3_128bits_update(hashState, &wire->looped, sizeof(wire->looped));
      assert(error == XXH_OK);

      error = XXH3_128bits_update(hashState, &wire->unsafe, sizeof(wire->unsafe));
      assert(error == XXH_OK);

      error = XXH3_128bits_update(hashState, &wire->pure, sizeof(wire->pure));
      assert(error == XXH_OK);

      for (auto &blk : wire->shards) {
        SHVar tmp{};
        tmp.valueType = SHType::ShardRef;
        tmp.payload.shardValue = blk;
        hash_update(tmp, state);
      }

      for (auto &wireVar : wire->variables) {
        error = XXH3_128bits_update(hashState, wireVar.first.c_str(), wireVar.first.length());
        assert(error == XXH_OK);
        hash_update(wireVar.second, state);
      }
    }
  } break;
  case SHType::Object: {
    error = XXH3_128bits_update(hashState, &var.payload.objectVendorId, sizeof(var.payload.objectVendorId));
    assert(error == XXH_OK);
    error = XXH3_128bits_update(hashState, &var.payload.objectTypeId, sizeof(var.payload.objectTypeId));
    assert(error == XXH_OK);
    if ((var.flags & SHVAR_FLAGS_USES_OBJINFO) == SHVAR_FLAGS_USES_OBJINFO && var.objectInfo && var.objectInfo->hash) {
      // hash of the hash...
      auto objHash = var.objectInfo->hash(var.payload.objectValue);
      error = XXH3_128bits_update(hashState, &objHash, sizeof(uint64_t));
      assert(error == XXH_OK);
    } else {
      // this will be valid only within this process memory space
      // better then nothing
      error = XXH3_128bits_update(hashState, &var.payload.objectValue, sizeof(var.payload.objectValue));
      assert(error == XXH_OK);
    }
  } break;
  case SHType::None:
  case SHType::Any:
  case SHType::EndOfBlittableTypes:
    break;
  case SHType::Enum:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHEnum));
    assert(error == XXH_OK);
    break;
  case SHType::Bool:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHBool));
    assert(error == XXH_OK);
    break;
  case SHType::Int:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHInt));
    assert(error == XXH_OK);
    break;
  case SHType::Int2:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHInt2));
    assert(error == XXH_OK);
    break;
  case SHType::Int3:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHInt3));
    assert(error == XXH_OK);
    break;
  case SHType::Int4:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHInt4));
    assert(error == XXH_OK);
    break;
  case SHType::Int8:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHInt8));
    assert(error == XXH_OK);
    break;
  case SHType::Int16:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHInt16));
    assert(error == XXH_OK);
    break;
  case SHType::Color:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHColor));
    assert(error == XXH_OK);
    break;
  case SHType::Float:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHFloat));
    assert(error == XXH_OK);
    break;
  case SHType::Float2:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHFloat2));
    assert(error == XXH_OK);
    break;
  case SHType::Float3:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHFloat3));
    assert(error == XXH_OK);
    break;
  case SHType::Float4:
    error = XXH3_128bits_update(hashState, &var.payload, sizeof(SHFloat4));
    assert(error == XXH_OK);
    break;
  }
}

SHString getString(uint32_t crc) {
  assert(shards::GetGlobals().CompressedStrings);
  auto s = (*shards::GetGlobals().CompressedStrings)[crc].string;
  return s != nullptr ? s : "";
}

void setString(uint32_t crc, SHString str) {
  assert(shards::GetGlobals().CompressedStrings);
  (*shards::GetGlobals().CompressedStrings)[crc].string = str;
  (*shards::GetGlobals().CompressedStrings)[crc].crc = crc;
}

void abortWire(SHContext *ctx, std::string_view errorText) { ctx->cancelFlow(errorText); }

void triggerVarValueChange(SHContext *context, const SHVar *name, const SHVar *var) {
  if ((var->flags & SHVAR_FLAGS_EXPOSED) == 0)
    return;

  auto &w = context->main;
  auto nameStr = SHSTRVIEW((*name));
  OnExposedVarSet ev{w->id, nameStr, *var, context->currentWire()};
  w->dispatcher.trigger(ev);
}

void triggerVarValueChange(SHWire *w, const SHVar *name, const SHVar *var) {
  if ((var->flags & SHVAR_FLAGS_EXPOSED) == 0)
    return;

  auto nameStr = SHSTRVIEW((*name));
  OnExposedVarSet ev{w->id, nameStr, *var, w};
  w->dispatcher.trigger(ev);
}
}; // namespace shards

// NO NAMESPACE here!

void SHWire::destroy() {
  for (auto it = shards.rbegin(); it != shards.rend(); ++it) {
    (*it)->cleanup(*it);
  }
  for (auto it = shards.rbegin(); it != shards.rend(); ++it) {
    decRef(*it);
  }

  // find dangling variables, notice but do not destroy
  for (auto var : variables) {
    if (var.second.refcount > 0) {
      SHLOG_ERROR("Found a dangling variable: {}, wire: {}", var.first, name);
    }
  }

  if (composeResult) {
    shards::arrayFree(composeResult->requiredInfo);
    shards::arrayFree(composeResult->exposedInfo);
  }

  auto n = mesh.lock();
  if (n) {
    n->visitedWires.erase(this);
  }

  if (stackMem) {
    ::operator delete[](stackMem, std::align_val_t{16});
  }
}

void SHWire::warmup(SHContext *context) {
  if (!warmedUp) {
    SHLOG_TRACE("Running warmup on wire: {}", name);

    // we likely need this early!
    mesh = context->main->mesh;
    warmedUp = true;

    context->wireStack.push_back(this);
    DEFER({ context->wireStack.pop_back(); });
    for (auto blk : shards) {
      try {
        if (blk->warmup) {
          auto status = blk->warmup(blk, context);
          if (status.code != SH_ERROR_NONE) {
            std::string_view msg(status.message.string, status.message.len);
            SHLOG_ERROR("Warmup failed on wire: {}, shard: {} (line: {}, column: {})", name, blk->name(blk), blk->line,
                        blk->column);
            throw shards::WarmupError(msg);
          }
        }
        if (context->failed()) {
          throw shards::WarmupError(context->getErrorMessage());
        }
      } catch (const std::exception &e) {
        SHLOG_ERROR("Shard warmup error, failed shard: {}", blk->name(blk));
        SHLOG_ERROR(e.what());
        // if the failure is from an exception context might not be uptodate
        if (!context->failed()) {
          context->cancelFlow(e.what());
        }
        throw;
      } catch (...) {
        SHLOG_ERROR("Shard warmup error, failed shard: {}", blk->name(blk));
        if (!context->failed()) {
          context->cancelFlow("foreign exception failure, check logs");
        }
        throw;
      }
    }

    SHLOG_TRACE("Ran warmup on wire: {}", name);
  } else {
    SHLOG_TRACE("Warmup already run on wire: {}", name);
  }
}

void SHWire::cleanup(bool force) {
  if (warmedUp && (force || wireUsers.size() == 0)) {
    SHLOG_TRACE("Running cleanup on wire: {} users count: {}", name, wireUsers.size());

    dispatcher.trigger(SHWire::OnCleanupEvent{this});

    warmedUp = false;

    // Run cleanup on all shards, prepare them for a new start if necessary
    // Do this in reverse to allow a safer cleanup
    for (auto it = shards.rbegin(); it != shards.rend(); ++it) {
      auto blk = *it;
      try {
        blk->cleanup(blk);
      }
#ifndef __EMSCRIPTEN__
      catch (boost::context::detail::forced_unwind const &e) {
        throw; // required for Boost Coroutine!
      }
#endif
      catch (const std::exception &e) {
        SHLOG_ERROR("Shard cleanup error, failed shard: {}, error: {}", blk->name(blk), e.what());
      } catch (...) {
        SHLOG_ERROR("Shard cleanup error, failed shard: {}", blk->name(blk));
      }
    }

    // Also clear all variables reporting dangling ones
    for (auto var : variables) {
      if (var.second.refcount > 0) {
        SHLOG_ERROR("Found a dangling variable: {} in wire: {}", var.first, name);
      }
    }
    variables.clear();

    // finally reset the mesh
    auto n = mesh.lock();
    if (n) {
      n->visitedWires.erase(this);
    }
    mesh.reset();

    resumer = nullptr;

    SHLOG_TRACE("Ran cleanup on wire: {}", name);
  }
}

void shInit() {
  static bool globalInitDone = false;
  if (globalInitDone)
    return;
  globalInitDone = true;

#ifdef TRACY_ENABLE
  tracyInit();
#endif

  logging::setupDefaultLoggerConditional();

  if (GetGlobals().RootPath.size() > 0) {
    // set root path as current directory
    fs::current_path(GetGlobals().RootPath);
  } else {
    // set current path as root path
    auto cp = fs::current_path();
    GetGlobals().RootPath = cp.string();
  }

#ifdef SH_USE_UBSAN_REPORT
  auto absPath = fs::absolute(GetGlobals().RootPath);
  auto absPathStr = absPath.string();
  SHLOG_TRACE("Setting UBSAN report path to: {}", absPathStr);
  __sanitizer_set_report_path(absPathStr.c_str());
#endif

// UTF8 on windows
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  namespace fs = boost::filesystem;
  if (GetGlobals().ExePath.size() > 0) {
    auto pluginPath = fs::absolute(GetGlobals().ExePath) / "shards";
    auto pluginPathStr = pluginPath.wstring();
    SHLOG_DEBUG("Adding dll path: {}", pluginPath.string());
    AddDllDirectory(pluginPathStr.c_str());
  }
  if (GetGlobals().RootPath.size() > 0) {
    auto pluginPath = fs::absolute(GetGlobals().RootPath) / "shards";
    auto pluginPathStr = pluginPath.wstring();
    SHLOG_DEBUG("Adding dll path: {}", pluginPath.string());
    AddDllDirectory(pluginPathStr.c_str());
  }
#endif

  SHLOG_DEBUG("Hardware concurrency: {}", std::thread::hardware_concurrency());

  static_assert(sizeof(SHVarPayload) == 16);
  static_assert(sizeof(SHVar) == 32);
  static_assert(sizeof(SHMapIt) <= sizeof(SHTableIterator));
  static_assert(sizeof(SHHashSetIt) <= sizeof(SHSetIterator));
  static_assert(sizeof(OwnedVar) == sizeof(SHVar));
  static_assert(sizeof(TableVar) == sizeof(SHVar));
  static_assert(sizeof(SeqVar) == sizeof(SHVar));

  shards::registerShards();

#ifdef __EMSCRIPTEN__
  sh_emscripten_init();
  // fill up some interface so we don't need to know mem offsets JS side
  EM_ASM({ Module["SHCore"] = {}; });
  emscripten::val shInterface = emscripten::val::module_property("SHCore");
  SHCore *iface = shardsInterface(SHARDS_CURRENT_ABI);
  shInterface.set("log", emscripten::val(reinterpret_cast<uintptr_t>(iface->log)));
  shInterface.set("createMesh", emscripten::val(reinterpret_cast<uintptr_t>(iface->createMesh)));
  shInterface.set("destroyMesh", emscripten::val(reinterpret_cast<uintptr_t>(iface->destroyMesh)));
  shInterface.set("schedule", emscripten::val(reinterpret_cast<uintptr_t>(iface->schedule)));
  shInterface.set("unschedule", emscripten::val(reinterpret_cast<uintptr_t>(iface->unschedule)));
  shInterface.set("tick", emscripten::val(reinterpret_cast<uintptr_t>(iface->tick)));
  shInterface.set("sleep", emscripten::val(reinterpret_cast<uintptr_t>(iface->sleep)));
  shInterface.set("getGlobalWire", emscripten::val(reinterpret_cast<uintptr_t>(iface->getGlobalWire)));
  emscripten_get_now(); // force emscripten to link this call
#endif
}

#define API_TRY_CALL(_name_, _shard_)                       \
  {                                                         \
    try {                                                   \
      _shard_                                               \
    } catch (const std::exception &ex) {                    \
      SHLOG_ERROR(#_name_ " failed, error: {}", ex.what()); \
    }                                                       \
  }

bool sh_current_interface_loaded{false};
SHCore sh_current_interface{};

extern "C" {
SHVar *getWireVariable(SHWireRef wireRef, const char *name, uint32_t nameLen) {
  auto &wire = SHWire::sharedFromRef(wireRef);
  std::string nameView{name, nameLen};
  auto it = wire->externalVariables.find(nameView);
  if (it != wire->externalVariables.end()) {
    return it->second;
  } else {
    auto it2 = wire->variables.find(nameView);
    if (it2 != wire->variables.end()) {
      return &it2->second;
    }
  }
  return nullptr;
}

void triggerVarValueChange(SHContext *ctx, const SHVar *name, const SHVar *var) { shards::triggerVarValueChange(ctx, name, var); }

SHContext *getWireContext(SHWireRef wireRef) {
  auto &wire = SHWire::sharedFromRef(wireRef);
  return wire->context;
}

SHCore *__cdecl shardsInterface(uint32_t abi_version) {
  // for now we ignore abi_version
  if (sh_current_interface_loaded)
    return &sh_current_interface;

  // Load everything we know if we did not yet!
  try {
    shInit();
  } catch (const std::exception &ex) {
    SHLOG_ERROR("Failed to register core shards, error: {}", ex.what());
    return nullptr;
  }

  if (SHARDS_CURRENT_ABI != abi_version) {
    SHLOG_ERROR("A plugin requested an invalid ABI version.");
    return nullptr;
  }

  auto result = &sh_current_interface;
  sh_current_interface_loaded = true;

  result->alloc = [](uint32_t size) -> void * {
    auto mem = ::operator new(size, std::align_val_t{16});
    memset(mem, 0, size);
    return mem;
  };

  result->free = [](void *ptr) { ::operator delete(ptr, std::align_val_t{16}); };

  result->registerShard = [](const char *fullName, SHShardConstructor constructor) noexcept {
    API_TRY_CALL(registerShard, shards::registerShard(fullName, constructor);)
  };

  result->registerObjectType = [](int32_t vendorId, int32_t typeId, SHObjectInfo info) noexcept {
    API_TRY_CALL(registerObjectType, shards::registerObjectType(vendorId, typeId, info);)
  };

  result->findObjectTypeId = [](SHStringWithLen name) noexcept {
    return shards::findObjectTypeId(std::string_view{name.string, name.len});
  };

  result->registerEnumType = [](int32_t vendorId, int32_t typeId, SHEnumInfo info) noexcept {
    API_TRY_CALL(registerEnumType, shards::registerEnumType(vendorId, typeId, info);)
  };

  result->findEnumId = [](SHStringWithLen name) noexcept { return shards::findEnumId(std::string_view{name.string, name.len}); };

  result->registerRunLoopCallback = [](const char *eventName, SHCallback callback) noexcept {
    API_TRY_CALL(registerRunLoopCallback, shards::registerRunLoopCallback(eventName, callback);)
  };

  result->registerExitCallback = [](const char *eventName, SHCallback callback) noexcept {
    API_TRY_CALL(registerExitCallback, shards::registerExitCallback(eventName, callback);)
  };

  result->unregisterRunLoopCallback = [](const char *eventName) noexcept {
    API_TRY_CALL(unregisterRunLoopCallback, shards::unregisterRunLoopCallback(eventName);)
  };

  result->unregisterExitCallback = [](const char *eventName) noexcept {
    API_TRY_CALL(unregisterExitCallback, shards::unregisterExitCallback(eventName);)
  };

  result->referenceVariable = [](SHContext *context, SHStringWithLen name) noexcept {
    std::string_view nameView{name.string, name.len};
    return shards::referenceVariable(context, nameView);
  };

  result->referenceWireVariable = [](SHWireRef wire, SHStringWithLen name) noexcept {
    std::string_view nameView{name.string, name.len};
    return shards::referenceWireVariable(wire, nameView);
  };

  result->releaseVariable = [](SHVar *variable) noexcept { return shards::releaseVariable(variable); };

  result->setExternalVariable = [](SHWireRef wire, SHStringWithLen name, SHVar *pVar) noexcept {
    std::string nameView{name.string, name.len};
    auto &sc = SHWire::sharedFromRef(wire);
    sc->externalVariables[nameView] = pVar;
  };

  result->removeExternalVariable = [](SHWireRef wire, SHStringWithLen name) noexcept {
    std::string nameView{name.string, name.len};
    auto &sc = SHWire::sharedFromRef(wire);
    sc->externalVariables.erase(nameView);
  };

  result->allocExternalVariable = [](SHWireRef wire, SHStringWithLen name) noexcept {
    std::string nameView{name.string, name.len};
    auto &sc = SHWire::sharedFromRef(wire);
    auto res = new (std::align_val_t{16}) SHVar();
    sc->externalVariables[nameView] = res;
    return res;
  };

  result->freeExternalVariable = [](SHWireRef wire, SHStringWithLen name) noexcept {
    std::string nameView{name.string, name.len};
    auto &sc = SHWire::sharedFromRef(wire);
    auto var = sc->externalVariables[nameView];
    if (var) {
      ::operator delete(var, std::align_val_t{16});
    }
    sc->externalVariables.erase(nameView);
  };

  result->suspend = [](SHContext *context, double seconds) noexcept {
    try {
      return shards::suspend(context, seconds);
    } catch (const shards::ActivationError &ex) {
      SHLOG_ERROR(ex.what());
      return SHWireState::Stop;
    }
  };

  result->getState = [](SHContext *context) noexcept { return context->getState(); };

  result->abortWire = [](SHContext *context, SHStringWithLen message) noexcept {
    std::string_view messageView{message.string, message.len};
    context->cancelFlow(messageView);
  };

  result->cloneVar = [](SHVar *dst, const SHVar *src) noexcept { shards::cloneVar(*dst, *src); };

  result->destroyVar = [](SHVar *var) noexcept { shards::destroyVar(*var); };

  result->hashVar = [](const SHVar *var) noexcept { return shards::hash(*var); };

#define SH_ARRAY_IMPL(_arr_, _val_, _name_)                                                                    \
  result->_name_##Free = [](_arr_ *seq) noexcept { shards::arrayFree(*seq); };                                 \
                                                                                                               \
  result->_name_##Resize = [](_arr_ *seq, uint32_t size) noexcept { shards::arrayResize(*seq, size); };        \
                                                                                                               \
  result->_name_##Push = [](_arr_ *seq, const _val_ *value) noexcept { shards::arrayPush(*seq, *value); };     \
                                                                                                               \
  result->_name_##Insert = [](_arr_ *seq, uint32_t index, const _val_ *value) noexcept {                       \
    shards::arrayInsert(*seq, index, *value);                                                                  \
  };                                                                                                           \
                                                                                                               \
  result->_name_##Pop = [](_arr_ *seq) noexcept { return shards::arrayPop<_arr_, _val_>(*seq); };              \
                                                                                                               \
  result->_name_##FastDelete = [](_arr_ *seq, uint32_t index) noexcept { shards::arrayDelFast(*seq, index); }; \
                                                                                                               \
  result->_name_##SlowDelete = [](_arr_ *seq, uint32_t index) noexcept { shards::arrayDel(*seq, index); }

  SH_ARRAY_IMPL(SHSeq, SHVar, seq);
  SH_ARRAY_IMPL(SHTypesInfo, SHTypeInfo, types);
  SH_ARRAY_IMPL(SHParametersInfo, SHParameterInfo, params);
  SH_ARRAY_IMPL(Shards, ShardPtr, shards);
  SH_ARRAY_IMPL(SHExposedTypesInfo, SHExposedTypeInfo, expTypes);
  SH_ARRAY_IMPL(SHEnums, SHEnum, enums);
  SH_ARRAY_IMPL(SHStrings, SHString, strings);

  result->tableNew = []() noexcept {
    SHTable res;
    res.api = &shards::GetGlobals().TableInterface;
    res.opaque = new shards::SHMap();
    return res;
  };

  result->setNew = []() noexcept {
    SHSet res;
    res.api = &shards::GetGlobals().SetInterface;
    res.opaque = new shards::SHHashSet();
    return res;
  };

  result->composeWire = [](SHWireRef wire, SHValidationCallback callback, void *userData, SHInstanceData data) noexcept {
    auto &sc = SHWire::sharedFromRef(wire);
    try {
      return composeWire(sc.get(), callback, userData, data);
    } catch (const std::exception &e) {
      SHComposeResult res{};
      res.failed = true;
      auto msgTmp = shards::Var(e.what());
      shards::cloneVar(res.failureMessage, msgTmp);
      return res;
    } catch (...) {
      SHComposeResult res{};
      res.failed = true;
      auto msgTmp = shards::Var("foreign exception failure during composeWire");
      shards::cloneVar(res.failureMessage, msgTmp);
      return res;
    }
  };

  result->runWire = [](SHWireRef wire, SHContext *context, const SHVar *input) noexcept {
    auto &sc = SHWire::sharedFromRef(wire);
    return shards::runSubWire(sc.get(), context, *input);
  };

  result->composeShards = [](Shards shards, SHValidationCallback callback, void *userData, SHInstanceData data) noexcept {
    try {
      return shards::composeWire(shards, callback, userData, data);
    } catch (const std::exception &e) {
      SHLOG_TRACE("composeShards failed: {}", e.what());
      SHComposeResult res{};
      res.failed = true;
      auto msgTmp = shards::Var(e.what());
      shards::cloneVar(res.failureMessage, msgTmp);
      return res;
    } catch (...) {
      SHLOG_TRACE("composeShards failed: ...");
      SHComposeResult res{};
      res.failed = true;
      auto msgTmp = shards::Var("foreign exception failure during composeWire");
      shards::cloneVar(res.failureMessage, msgTmp);
      return res;
    }
  };

  result->validateSetParam = [](Shard *shard, int index, const SHVar *param, SHValidationCallback callback,
                                void *userData) noexcept {
    return shards::validateSetParam(shard, index, *param, callback, userData);
  };

  result->runShards = [](Shards shards, SHContext *context, const SHVar *input, SHVar *output) noexcept {
    return shards::activateShards(shards, context, *input, *output);
  };

  result->runShards2 = [](Shards shards, SHContext *context, const SHVar *input, SHVar *output) noexcept {
    return shards::activateShards2(shards, context, *input, *output);
  };

  result->runShardsHashed = [](Shards shards, SHContext *context, const SHVar *input, SHVar *output, SHVar *outHash) noexcept {
    return shards::activateShards(shards, context, *input, *output, *outHash);
  };

  result->runShardsHashed2 = [](Shards shards, SHContext *context, const SHVar *input, SHVar *output, SHVar *outHash) noexcept {
    return shards::activateShards2(shards, context, *input, *output, *outHash);
  };

  result->getWireInfo = [](SHWireRef wireref) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    auto wire = sc.get();
    SHWireInfo info{SHStringWithLen{wire->name.c_str(), wire->name.size()},
                    wire->looped,
                    wire->unsafe,
                    wire,
                    {!wire->shards.empty() ? &wire->shards[0] : nullptr, uint32_t(wire->shards.size()), 0},
                    shards::isRunning(wire),
                    wire->state == SHWire::State::Failed || !wire->finishedError.empty(),
                    SHStringWithLen{wire->finishedError.c_str(), wire->finishedError.size()},
                    &wire->finishedOutput};
    return info;
  };

  result->log = [](SHStringWithLen msg) noexcept {
    std::string_view sv(msg.string, msg.len);
    SHLOG_INFO(sv);
  };

  result->logLevel = [](int level, SHStringWithLen msg) noexcept {
    std::string_view sv(msg.string, msg.len);
    spdlog::default_logger_raw()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, (spdlog::level::level_enum)level,
                                      sv);
  };

  result->createShard = [](SHStringWithLen name) noexcept {
    std::string_view sv(name.string, name.len);
    auto shard = shards::createShard(sv);
    if (shard) {
      assert(shard->refCount == 0 && "shard should have zero refcount");
      incRef(shard);
    }
    return shard;
  };

  result->releaseShard = [](struct Shard *shard) noexcept { decRef(shard); };

  result->createWire = []() noexcept {
    auto wire = SHWire::make();
    return wire->newRef();
  };

  result->setWireName = [](SHWireRef wireref, SHStringWithLen name) noexcept {
    std::string_view sv(name.string, name.len);
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->name = sv;
  };

  result->setWireLooped = [](SHWireRef wireref, SHBool looped) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->looped = looped;
  };

  result->setWireUnsafe = [](SHWireRef wireref, SHBool unsafe) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->unsafe = unsafe;
  };

  result->setWirePure = [](SHWireRef wireref, SHBool pure) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->pure = pure;
  };

  result->setWireStackSize = [](SHWireRef wireref, size_t size) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->stackSize = size;
  };

  result->addShard = [](SHWireRef wireref, ShardPtr blk) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->addShard(blk);
  };

  result->removeShard = [](SHWireRef wireref, ShardPtr blk) noexcept {
    auto &sc = SHWire::sharedFromRef(wireref);
    sc->removeShard(blk);
  };

  result->destroyWire = [](SHWireRef wire) noexcept { SHWire::deleteRef(wire); };

  result->stopWire = [](SHWireRef wire) {
    auto &sc = SHWire::sharedFromRef(wire);
    SHVar output{};
    shards::stop(sc.get(), &output);
    return output;
  };

  result->destroyWire = [](SHWireRef wire) noexcept { SHWire::deleteRef(wire); };

  result->destroyWire = [](SHWireRef wire) noexcept { SHWire::deleteRef(wire); };

  result->getGlobalWire = [](SHStringWithLen name) noexcept {
    std::string sv(name.string, name.len);
    auto it = shards::GetGlobals().GlobalWires.find(std::move(sv));
    if (it != shards::GetGlobals().GlobalWires.end()) {
      return SHWire::weakRef(it->second);
    } else {
      return (SHWireRef) nullptr;
    }
  };

  result->setGlobalWire = [](SHStringWithLen name, SHWireRef wire) noexcept {
    std::string sv(name.string, name.len);
    shards::GetGlobals().GlobalWires[std::move(sv)] = SHWire::sharedFromRef(wire);
  };

  result->unsetGlobalWire = [](SHStringWithLen name) noexcept {
    std::string sv(name.string, name.len);
    auto it = shards::GetGlobals().GlobalWires.find(std::move(sv));
    if (it != shards::GetGlobals().GlobalWires.end()) {
      shards::GetGlobals().GlobalWires.erase(it);
    }
  };

  result->createMesh = []() noexcept {
    auto mesh = SHMesh::makePtr();
    SHLOG_TRACE("createMesh {}", (void *)(*mesh).get());
    return reinterpret_cast<SHMeshRef>(mesh);
  };

  result->destroyMesh = [](SHMeshRef mesh) noexcept {
    auto smesh = reinterpret_cast<std::shared_ptr<SHMesh> *>(mesh);
    SHLOG_TRACE("destroyMesh {}", (void *)(*smesh).get());
    delete smesh;
  };

  result->schedule = [](SHMeshRef mesh, SHWireRef wire) noexcept {
    try {
      auto smesh = reinterpret_cast<std::shared_ptr<SHMesh> *>(mesh);
      (*smesh)->schedule(SHWire::sharedFromRef(wire));
    } catch (const std::exception &e) {
      SHLOG_ERROR("Errors while scheduling: {}", e.what());
    } catch (...) {
      SHLOG_ERROR("Errors while scheduling");
    }
  };

  result->unschedule = [](SHMeshRef mesh, SHWireRef wire) noexcept {
    auto smesh = reinterpret_cast<std::shared_ptr<SHMesh> *>(mesh);
    (*smesh)->remove(SHWire::sharedFromRef(wire));
  };

  result->tick = [](SHMeshRef mesh) noexcept {
    auto smesh = reinterpret_cast<std::shared_ptr<SHMesh> *>(mesh);
    (*smesh)->tick();
    if ((*smesh)->empty())
      return false;
    else
      return true;
  };

  result->terminate = [](SHMeshRef mesh) noexcept {
    auto smesh = reinterpret_cast<std::shared_ptr<SHMesh> *>(mesh);
    (*smesh)->terminate();
  };

  result->sleep = [](double seconds, bool runCallbacks) noexcept { shards::sleep(seconds, runCallbacks); };

  result->getRootPath = []() noexcept { return shards::GetGlobals().RootPath.c_str(); };

  result->setRootPath = [](const char *p) noexcept {
    shards::GetGlobals().RootPath = p;
    shards::loadExternalShards(p);
    fs::current_path(p);
  };

  result->asyncActivate = [](SHContext *context, void *userData, SHAsyncActivateProc call, SHAsyncCancelProc cancel_call) {
    return shards::awaitne(
        context, [=] { return call(context, userData); },
        [=] {
          if (cancel_call)
            cancel_call(context, userData);
        });
  };

  result->getShards = []() {
    SHStrings s{};
    for (auto [name, _] : shards::GetGlobals().ShardsRegister) {
      shards::arrayPush(s, name.data());
    }
    return s;
  };

  result->readCachedString = [](uint32_t crc) {
    auto s = shards::getString(crc);
    return SHOptionalString{s, crc};
  };

  result->writeCachedString = [](uint32_t crc, SHString str) {
    shards::setString(crc, str);
    return SHOptionalString{str, crc};
  };

  result->decompressStrings = []() {
#ifdef SH_COMPRESSED_STRINGS
    shards::decompressStrings();
#endif
  };

  result->isEqualVar = [](const SHVar *v1, const SHVar *v2) -> SHBool { return *v1 == *v2; };

  result->isEqualType = [](const SHTypeInfo *t1, const SHTypeInfo *t2) -> SHBool { return *t1 == *t2; };

  result->deriveTypeInfo = [](const SHVar *v, const struct SHInstanceData *data) -> SHTypeInfo {
    return deriveTypeInfo(*v, *data);
  };

  result->freeDerivedTypeInfo = [](SHTypeInfo *t) { freeDerivedInfo(*t); };

  result->findEnumInfo = &shards::findEnumInfo;

  result->findObjectInfo = &shards::findObjectInfo;

  result->type2Name = [](SHType type) { return type2Name_raw(type); };

  return result;
}
}

namespace shards {
void decRef(ShardPtr shard) {
  auto atomicRefCount = boost::atomics::make_atomic_ref(shard->refCount);
  assert(atomicRefCount > 0);
  if (atomicRefCount.fetch_sub(1) == 1) {
    // SHLOG_TRACE("DecRef 0 shard {:x} {}", (size_t)shard, shard->name(shard));
    shard->destroy(shard);
  }
}

void incRef(ShardPtr shard) {
  auto atomicRefCount = boost::atomics::make_atomic_ref(shard->refCount);
  if (atomicRefCount.fetch_add(1) == 0) {
    // SHLOG_TRACE("IncRef 0 shard {:x} {}", (size_t)shard, shard->name(shard));
  }
}
} // namespace shards

#ifdef TRACY_ENABLE
void *operator new(std::size_t count) {
  void *ptr = std::malloc(count);
  if (shards::tracyInitialized)
    TracyAlloc(ptr, count);
  return ptr;
}

void *operator new[](std::size_t count) {
  void *ptr = std::malloc(count);
  if (shards::tracyInitialized)
    TracyAlloc(ptr, count);
  return ptr;
}

void *operator new(std::size_t count, std::align_val_t alignment) {
  std::size_t align_value = static_cast<std::size_t>(alignment);
  std::size_t aligned_count = (count + align_value - 1) / align_value * align_value;
#ifdef WIN32
  void *ptr = _aligned_malloc(aligned_count, align_value);
#else
  void *ptr = std::aligned_alloc(align_value, aligned_count);
#endif
  if (shards::tracyInitialized)
    TracyAlloc(ptr, count);
  return ptr;
}

void *operator new[](std::size_t count, std::align_val_t alignment) {
  std::size_t align_value = static_cast<std::size_t>(alignment);
  std::size_t aligned_count = (count + align_value - 1) / align_value * align_value;
#ifdef WIN32
  void *ptr = _aligned_malloc(aligned_count, align_value);
#else
  void *ptr = std::aligned_alloc(align_value, aligned_count);
#endif
  if (shards::tracyInitialized)
    TracyAlloc(ptr, count);
  return ptr;
}

void operator delete(void *ptr) noexcept {
  if (shards::tracyInitialized)
    TracyFree(ptr);
  std::free(ptr);
}

void operator delete[](void *ptr) noexcept {
  if (shards::tracyInitialized)
    TracyFree(ptr);
  std::free(ptr);
}

void operator delete(void *ptr, std::align_val_t alignment) noexcept {
  if (shards::tracyInitialized)
    TracyFree(ptr);
#ifdef WIN32
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void operator delete[](void *ptr, std::align_val_t alignment) noexcept {
  if (shards::tracyInitialized)
    TracyFree(ptr);
#ifdef WIN32
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void operator delete(void *ptr, std::size_t count) noexcept {
  if (shards::tracyInitialized)
    TracyFree(ptr);
  std::free(ptr);
}

void operator delete[](void *ptr, std::size_t count) noexcept {
  if (shards::tracyInitialized)
    TracyFree(ptr);
  std::free(ptr);
}

#endif
