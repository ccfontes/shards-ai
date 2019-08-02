#define String MalString
#include "MAL.h"
#include "Environment.h"
#include "StaticList.h"
#include "Types.h"
#undef String
#include <algorithm>
#define CHAINBLOCKS_RUNTIME
#include "../core/runtime.hpp"

#define CHECK_ARGS_IS(expected) \
    checkArgsIs(name.c_str(), expected, \
                  std::distance(argsBegin, argsEnd))

#define CHECK_ARGS_BETWEEN(min, max) \
    checkArgsBetween(name.c_str(), min, max, \
                       std::distance(argsBegin, argsEnd))

#define CHECK_ARGS_AT_LEAST(expected) \
    checkArgsAtLeast(name.c_str(), expected, \
                        std::distance(argsBegin, argsEnd))

#define ARG(type, name) type* name = VALUE_CAST(type, *argsBegin++)

static StaticList<malBuiltIn*> handlers;
#define FUNCNAME(uniq) builtIn ## uniq
#define HRECNAME(uniq) handler ## uniq
#define BUILTIN_DEF(uniq, symbol) \
    static malBuiltIn::ApplyFunc FUNCNAME(uniq); \
    static StaticList<malBuiltIn*>::Node HRECNAME(uniq) \
        (handlers, new malBuiltIn(symbol, FUNCNAME(uniq))); \
    malValuePtr FUNCNAME(uniq)(const MalString& name, \
        malValueIter argsBegin, malValueIter argsEnd)

#define BUILTIN(symbol)  BUILTIN_DEF(__LINE__, symbol)

#define BUILTIN_ISA(symbol, type) \
    BUILTIN(symbol) { \
        CHECK_ARGS_IS(1); \
        return mal::boolean(DYNAMIC_CAST(type, *argsBegin)); \
    }

extern void NimMain();

void registerKeywords(malEnvPtr env);

void installCBCore(malEnvPtr env) 
{
  NimMain();
  
  registerKeywords(env);
  
  for (auto it = handlers.begin(), end = handlers.end(); it != end; ++it) 
  {
    malBuiltIn* handler = *it;
    env->set(handler->name(), handler);
  }
}

class malCBBlock : public malValue {
public:
    malCBBlock(MalString name) : m_block(chainblocks::createBlock(name.c_str())) { }

    malCBBlock(CBRuntimeBlock* block) : m_block(block) { }
    
    malCBBlock(const malCBBlock& that, malValuePtr meta) : malValue(meta), m_block(that.m_block) { }
    
    ~malCBBlock()
    {
      if(m_block)
        m_block->destroy(m_block);
    }
    
    virtual MalString print(bool readably) const 
    { 
      std::ostringstream stream;
      stream << "Block: " << m_block->name(m_block);
      return stream.str();
    }
    
    CBRuntimeBlock* value() const { return m_block; }

    void consume() 
    {
      m_block = nullptr;
    }
    
    virtual bool doIsEqualTo(const malValue* rhs) const
    {
      return m_block == static_cast<const malCBBlock*>(rhs)->m_block;
    }
    
    WITH_META(malCBBlock);
private:
    CBRuntimeBlock* m_block;
};

class malCBChain : public malValue {
public:
    malCBChain(MalString name) : m_chain(new CBChain(name.c_str())) { }
    
    malCBChain(const malCBChain& that, malValuePtr meta) : malValue(meta), m_chain(that.m_chain) { }
    
    ~malCBChain()
    {
      delete m_chain;
    }
    
    virtual MalString print(bool readably) const 
    { 
      std::ostringstream stream;
      stream << "CBChain: " << m_chain->name;
      return stream.str();
    }
    
    CBChain* value() const { return m_chain; }
    
    virtual bool doIsEqualTo(const malValue* rhs) const
    {
      return m_chain == static_cast<const malCBChain*>(rhs)->m_chain;
    }
    
    WITH_META(malCBChain);
private:
    CBChain* m_chain;
};

class malCBNode : public malValue {
public:
    malCBNode() : m_node(new CBNode()) { }
    
    malCBNode(const malCBNode& that, malValuePtr meta) : malValue(meta), m_node(that.m_node) { }
    
    ~malCBNode()
    {
      delete m_node;
    }
    
    virtual MalString print(bool readably) const 
    { 
      std::ostringstream stream;
      stream << "CBNode";
      return stream.str();
    }

    CBNode* value() const { return m_node; }

    void schedule(malCBChain* chain)
    {
      m_node->schedule(chain->value());
      m_scheduledChains.push_back(chain->acquire());
    }
    
    virtual bool doIsEqualTo(const malValue* rhs) const
    {
      return m_node == static_cast<const malCBNode*>(rhs)->m_node;
    }
    
    WITH_META(malCBNode);
private:
    CBNode* m_node;
    std::vector<const RefCounted*> m_scheduledChains;
};

class malCBVar : public malValue {
public:
    malCBVar(CBVar var) : m_var(var) { }
    
    malCBVar(const malCBVar& that, malValuePtr meta) : malValue(meta) 
    {
      chainblocks::destroyVar(m_var);
      chainblocks::cloneVar(m_var, that.m_var);
    }

    ~malCBVar()
    {
      chainblocks::destroyVar(m_var);
    }
    
    virtual MalString print(bool readably) const 
    { 
      std::ostringstream stream;
      stream << "CBVar: " << m_var;
      return stream.str();
    }
    
    virtual bool doIsEqualTo(const malValue* rhs) const
    {
      return m_var == static_cast<const malCBVar*>(rhs)->m_var;
    }

    CBVar value() const { return m_var; }
    
    WITH_META(malCBVar);
private:
    CBVar m_var;
};

BUILTIN("Node")
{
  return malValuePtr(new malCBNode());
}

#define WRAP_TO_CONST(_var_) auto constBlock = chainblocks::createBlock("Const");\
  constBlock->setup(constBlock);\
  constBlock->setParam(constBlock, 0, _var_);\
  return constBlock

// Helper to generate const blocks automatically inferring types
CBRuntimeBlock* blockify(malValuePtr arg)
{
  if (arg == mal::nilValue())
  {
    // Wrap none into const
    CBVar var;
    var.valueType = None;
    var.payload.chainState = Continue;
    WRAP_TO_CONST(var);
  }
  else if (const malString* v = DYNAMIC_CAST(malString, arg)) 
  {
    // Wrap a string into a const block!
    CBVar strVar;
    strVar.valueType = String;
    strVar.payload.stringValue = v->value().c_str();
    WRAP_TO_CONST(strVar);
  }
  else if (const malInteger* v = DYNAMIC_CAST(malInteger, arg)) 
  {
    // Wrap a string into a const block!
    CBVar var;
    var.valueType = Int;
    var.payload.intValue = v->value();
    WRAP_TO_CONST(var);
  }
  else if (const malFloat* v = DYNAMIC_CAST(malFloat, arg)) 
  {
    // Wrap a string into a const block!
    CBVar var;
    var.valueType = Float;
    var.payload.floatValue = v->value();
    WRAP_TO_CONST(var);
  }
  else if (arg == mal::trueValue())
  {
    // Wrap none into const
    CBVar var;
    var.valueType = Bool;
    var.payload.boolValue = true;
    WRAP_TO_CONST(var);
  }
  else if (arg == mal::falseValue())
  {
    // Wrap none into const
    CBVar var;
    var.valueType = Bool;
    var.payload.boolValue = false;
    WRAP_TO_CONST(var);
  }
  else if (const malCBVar* v = DYNAMIC_CAST(malCBVar, arg)) 
  {
    WRAP_TO_CONST(v->value());
  }
  else if (const malCBBlock* v = DYNAMIC_CAST(malCBBlock, arg)) 
  {
    auto block = v->value();
    const_cast<malCBBlock*>(v)->consume(); // Blocks are unique, consume before use, assume goes inside another block or
    return block;
  }
  else
  {
    throw chainblocks::CBException("Invalid argument");
  }
}

CBVar varify(malValuePtr arg)
{
  if (arg == mal::nilValue())
  {
    CBVar var;
    var.valueType = None;
    var.payload.chainState = Continue;
    return var;
  }
  else if (const malString* v = DYNAMIC_CAST(malString, arg)) 
  {
    CBVar strVar;
    strVar.valueType = String;
    strVar.payload.stringValue = v->value().c_str();
    return strVar;
  }
  else if (const malInteger* v = DYNAMIC_CAST(malInteger, arg)) 
  {
    CBVar var;
    var.valueType = Int;
    var.payload.intValue = v->value();
    return var;
  }
  else if (const malFloat* v = DYNAMIC_CAST(malFloat, arg)) 
  {
    CBVar var;
    var.valueType = Float;
    var.payload.floatValue = v->value();
    return var;
  }
  else if (const malSequence* v = DYNAMIC_CAST(malSequence, arg)) 
  {
    CBVar var;
    var.valueType = Seq;
    var.payload.seqValue = nullptr;
    var.payload.seqLen = -1;
    auto count = v->count();
    for(auto i = 0; i < count; i++)
    {
      auto val = v->item(i);
      auto subVar = varify(val);
      stbds_arrpush(var.payload.seqValue, subVar);
    }
    return var;
  }
  // else if (const malHash* v = DYNAMIC_CAST(malHash, arg)) 
  // {
  //   CBVar var;
  //   var.valueType = Seq;
  //   var.payload.seqValue = nullptr;
  //   var.payload.seqLen = -1;
  //   auto count = v->count();
  //   for(auto i = 0; i < count; i++)
  //   {
  //     auto val = v->item(i);
  //     auto subVar = varify(val);
  //     stbds_arrpush(var.payload.seqValue, subVar);
  //   }
  //   return var;
  // }
  else if (arg == mal::trueValue())
  {
    CBVar var;
    var.valueType = Bool;
    var.payload.boolValue = true;
    return var;
  }
  else if (arg == mal::falseValue())
  {
    CBVar var;
    var.valueType = Bool;
    var.payload.boolValue = false;
    return var;
  }
  else if (const malCBVar* v = DYNAMIC_CAST(malCBVar, arg)) 
  {
    return v->value();
  }
  else if (const malCBBlock* v = DYNAMIC_CAST(malCBBlock, arg)) 
  {
    auto block = v->value();
    const_cast<malCBBlock*>(v)->consume(); // Blocks are unique, consume before use, assume goes inside another block or
    CBVar var;
    var.valueType = Block;
    var.payload.blockValue = block;
    return var;
  }
  else
  {
    throw chainblocks::CBException("Invalid variable");
  }
}

int findParamIndex(std::string name, CBParametersInfo params)
{
  for(auto i = 0; i < stbds_arrlen(params); i++)
  {
    auto param = params[i];
    if(param.name == name)
      return i;
  }
  return -1;
}

void validationCallback(const CBRuntimeBlock* errorBlock, const char* errorTxt, bool nonfatalWarning, void* userData)
{
  auto failed = reinterpret_cast<bool*>(userData);
  auto block = const_cast<CBRuntimeBlock*>(errorBlock);
  if(!nonfatalWarning)
  {
    *failed = true;
    LOG(ERROR) << "Parameter validation failed: " << errorTxt << " block: " << block->name(block);
  }
  else
  {
    LOG(INFO) << "Parameter validation warning: " << errorTxt << " block: " << block->name(block);
  }
}

void setBlockParameters(CBRuntimeBlock* block, malValueIter begin, malValueIter end)
{
  auto argsBegin = begin;
  auto argsEnd = end;
  auto params = block->parameters(block);
  auto pindex = 0;
  while(argsBegin != argsEnd)
  {
    auto arg = *argsBegin++;
    if(const malKeyword* v = DYNAMIC_CAST(malKeyword, arg))
    {
      // In this case it's a keyworded call from now on
      pindex = -1;
      
      // Jump to next value straight, expecting a value
      auto value = *argsBegin++;
      auto paramName = v->value().substr(1);
      auto idx = findParamIndex(paramName, params);
      if(unlikely(idx == -1))
      {
        LOG(ERROR) << "Parameter not found: " << paramName << " block: " << block->name(block);
        throw chainblocks::CBException("Parameter not found");
      }
      else
      {
        auto var = varify(value);
        bool failed = false;
        validateSetParam(block, idx, var, validationCallback, &failed);
        if(failed)
          throw chainblocks::CBException("Parameter validation failed");
        block->setParam(block, idx, var);
      }
    }
    else if(pindex == -1)
    {
      // We expected a keyword! fail
      LOG(ERROR) << "Keyword expected, block: " << block->name(block);
      throw chainblocks::CBException("Keyword expected");
    }
    else
    {
      auto var = varify(arg);
      bool failed = false;
      validateSetParam(block, pindex, var, validationCallback, &failed);
      if(failed)
        throw chainblocks::CBException("Parameter validation failed");
      block->setParam(block, pindex, var);
      
      pindex++;
    }
  }
}

BUILTIN("Chain")
{
  CHECK_ARGS_AT_LEAST(1);
  ARG(malString, chainName);
  auto mchain = new malCBChain(chainName->value());
  auto chain = mchain->value();
  while(argsBegin != argsEnd)
  {
    auto arg = *argsBegin++;
    chain->addBlock(blockify(arg));
  }
  return malValuePtr(mchain);
}

BUILTIN("schedule")
{
  CHECK_ARGS_IS(2);
  ARG(malCBNode, node);
  ARG(malCBChain, chain);
  node->schedule(chain);
  return mal::nilValue();
}

BUILTIN("#")
{
  CHECK_ARGS_IS(1);
  ARG(malString, value);
  CBVar tmp;
  tmp.valueType = ContextVar;
  tmp.payload.stringValue = value->value().c_str();
  auto var = CBVar();
  chainblocks::cloneVar(var, tmp);
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Enum")
{
  CHECK_ARGS_IS(3);
  ARG(malInteger, value0);
  ARG(malInteger, value1);
  ARG(malInteger, value2);
  CBVar var;
  var.valueType = Enum;
  var.payload.enumVendorId = static_cast<int32_t>(value0->value());
  var.payload.enumTypeId = static_cast<int32_t>(value1->value());
  var.payload.enumValue = static_cast<CBEnum>(value2->value());
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Int")
{
  CHECK_ARGS_IS(1);
  ARG(malInteger, value);
  CBVar var;
  var.valueType = Int;
  var.payload.intValue = value->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Int2")
{
  CHECK_ARGS_IS(2);
  ARG(malInteger, value0);
  ARG(malInteger, value1);
  CBVar var;
  var.valueType = Int2;
  var.payload.int2Value[0] = value0->value();
  var.payload.int2Value[1] = value1->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Int3")
{
  CHECK_ARGS_IS(3);
  ARG(malInteger, value0);
  ARG(malInteger, value1);
  ARG(malInteger, value2);
  CBVar var;
  var.valueType = Int3;
  var.payload.int3Value[0] = value0->value();
  var.payload.int3Value[1] = value1->value();
  var.payload.int3Value[2] = value2->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Int4")
{
  CHECK_ARGS_IS(4);
  ARG(malInteger, value0);
  ARG(malInteger, value1);
  ARG(malInteger, value2);
  ARG(malInteger, value3);
  CBVar var;
  var.valueType = Int4;
  var.payload.int4Value[0] = value0->value();
  var.payload.int4Value[1] = value1->value();
  var.payload.int4Value[2] = value2->value();
  var.payload.int4Value[3] = value3->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Color")
{
  CHECK_ARGS_IS(4);
  ARG(malInteger, value0);
  ARG(malInteger, value1);
  ARG(malInteger, value2);
  ARG(malInteger, value3);
  CBVar var;
  var.valueType = Color;
  var.payload.colorValue.r = static_cast<uint8_t>(value0->value());
  var.payload.colorValue.g = static_cast<uint8_t>(value1->value());
  var.payload.colorValue.b = static_cast<uint8_t>(value2->value());
  var.payload.colorValue.a = static_cast<uint8_t>(value3->value());
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Float")
{
  CHECK_ARGS_IS(1);
  ARG(malFloat, value);
  CBVar var;
  var.valueType = Float;
  var.payload.floatValue = value->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Float2")
{
  CHECK_ARGS_IS(2);
  ARG(malFloat, value0);
  ARG(malFloat, value1);
  CBVar var;
  var.valueType = Float2;
  var.payload.float2Value[0] = value0->value();
  var.payload.float2Value[1] = value1->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Float3")
{
  CHECK_ARGS_IS(3);
  ARG(malFloat, value0);
  ARG(malFloat, value1);
  ARG(malFloat, value2);
  CBVar var;
  var.valueType = Float3;
  var.payload.float3Value[0] = value0->value();
  var.payload.float3Value[1] = value1->value();
  var.payload.float3Value[2] = value2->value();
  return malValuePtr(new malCBVar(var));
}

BUILTIN("Float4")
{
  CHECK_ARGS_IS(4);
  ARG(malFloat, value0);
  ARG(malFloat, value1);
  ARG(malFloat, value2);
  ARG(malFloat, value3);
  CBVar var;
  var.valueType = Float4;
  var.payload.float4Value[0] = value0->value();
  var.payload.float4Value[1] = value1->value();
  var.payload.float4Value[2] = value2->value();
  var.payload.float4Value[3] = value3->value();
  return malValuePtr(new malCBVar(var));
}

// Mostly internal use

BUILTIN_ISA("__CBVar?__", malCBVar);

BUILTIN("__createBlock__")
{
  CHECK_ARGS_IS(2);
  ARG(malString, blkName);
  return malValuePtr(new malCBBlock(blkName->value()));
}

BUILTIN("__setParam__")
{
  CHECK_ARGS_IS(3);
  ARG(malCBBlock, blk);
  ARG(malInteger, index);
  ARG(malCBVar, var);
  
  blk->value()->setParam(blk->value(), index->value(), var->value());
  
  return mal::nilValue();
}

#ifdef HAS_CB_GENERATED
#include "CBGenerated.hpp"
#endif