#include "../runtime.hpp"

namespace chainblocks {
// Register Const
RUNTIME_CORE_BLOCK_FACTORY(Const);
RUNTIME_BLOCK_destroy(Const);
RUNTIME_BLOCK_inputTypes(Const);
RUNTIME_BLOCK_outputTypes(Const);
RUNTIME_BLOCK_parameters(Const);
RUNTIME_BLOCK_inferTypes(Const);
RUNTIME_BLOCK_setParam(Const);
RUNTIME_BLOCK_getParam(Const);
RUNTIME_BLOCK_activate(Const);
RUNTIME_BLOCK_END(Const);

// Register Sleep
RUNTIME_CORE_BLOCK_FACTORY(Sleep);
RUNTIME_BLOCK_inputTypes(Sleep);
RUNTIME_BLOCK_outputTypes(Sleep);
RUNTIME_BLOCK_parameters(Sleep);
RUNTIME_BLOCK_setParam(Sleep);
RUNTIME_BLOCK_getParam(Sleep);
RUNTIME_BLOCK_activate(Sleep);
RUNTIME_BLOCK_END(Sleep);

// Register And
RUNTIME_CORE_BLOCK_FACTORY(And);
RUNTIME_BLOCK_inputTypes(And);
RUNTIME_BLOCK_outputTypes(And);
RUNTIME_BLOCK_activate(And);
RUNTIME_BLOCK_END(And);

// Register Or
RUNTIME_CORE_BLOCK_FACTORY(Or);
RUNTIME_BLOCK_inputTypes(Or);
RUNTIME_BLOCK_outputTypes(Or);
RUNTIME_BLOCK_activate(Or);
RUNTIME_BLOCK_END(Or);

// Register Stop
RUNTIME_CORE_BLOCK_FACTORY(Stop);
RUNTIME_BLOCK_inputTypes(Stop);
RUNTIME_BLOCK_outputTypes(Stop);
RUNTIME_BLOCK_activate(Stop);
RUNTIME_BLOCK_END(Stop);

// Register Restart
RUNTIME_CORE_BLOCK_FACTORY(Restart);
RUNTIME_BLOCK_inputTypes(Restart);
RUNTIME_BLOCK_outputTypes(Restart);
RUNTIME_BLOCK_activate(Restart);
RUNTIME_BLOCK_END(Restart);

// Register Return
RUNTIME_CORE_BLOCK_FACTORY(Return);
RUNTIME_BLOCK_inputTypes(Return);
RUNTIME_BLOCK_outputTypes(Return);
RUNTIME_BLOCK_activate(Return);
RUNTIME_BLOCK_END(Return);

// Register Set
RUNTIME_CORE_BLOCK_FACTORY(Set);
RUNTIME_BLOCK_cleanup(Set);
RUNTIME_BLOCK_inputTypes(Set);
RUNTIME_BLOCK_outputTypes(Set);
RUNTIME_BLOCK_parameters(Set);
RUNTIME_BLOCK_inferTypes(Set);
RUNTIME_BLOCK_exposedVariables(Set);
RUNTIME_BLOCK_setParam(Set);
RUNTIME_BLOCK_getParam(Set);
RUNTIME_BLOCK_activate(Set);
RUNTIME_BLOCK_END(Set);

// Register Update
RUNTIME_CORE_BLOCK_FACTORY(Update);
RUNTIME_BLOCK_cleanup(Update);
RUNTIME_BLOCK_inputTypes(Update);
RUNTIME_BLOCK_outputTypes(Update);
RUNTIME_BLOCK_parameters(Update);
RUNTIME_BLOCK_inferTypes(Update);
RUNTIME_BLOCK_consumedVariables(Update);
RUNTIME_BLOCK_setParam(Update);
RUNTIME_BLOCK_getParam(Update);
RUNTIME_BLOCK_activate(Update);
RUNTIME_BLOCK_END(Update);

// Register Push
RUNTIME_CORE_BLOCK_FACTORY(Push);
RUNTIME_BLOCK_destroy(Push);
RUNTIME_BLOCK_cleanup(Push);
RUNTIME_BLOCK_inputTypes(Push);
RUNTIME_BLOCK_outputTypes(Push);
RUNTIME_BLOCK_parameters(Push);
RUNTIME_BLOCK_inferTypes(Push);
RUNTIME_BLOCK_exposedVariables(Push);
RUNTIME_BLOCK_setParam(Push);
RUNTIME_BLOCK_getParam(Push);
RUNTIME_BLOCK_activate(Push);
RUNTIME_BLOCK_END(Push);

// Register Pop
RUNTIME_CORE_BLOCK_FACTORY(Pop);
RUNTIME_BLOCK_cleanup(Pop);
RUNTIME_BLOCK_destroy(Pop);
RUNTIME_BLOCK_inputTypes(Pop);
RUNTIME_BLOCK_outputTypes(Pop);
RUNTIME_BLOCK_parameters(Pop);
RUNTIME_BLOCK_inferTypes(Pop);
RUNTIME_BLOCK_consumedVariables(Pop);
RUNTIME_BLOCK_setParam(Pop);
RUNTIME_BLOCK_getParam(Pop);
RUNTIME_BLOCK_activate(Pop);
RUNTIME_BLOCK_END(Pop);

// Register Count
RUNTIME_CORE_BLOCK_FACTORY(Count);
RUNTIME_BLOCK_inputTypes(Count);
RUNTIME_BLOCK_outputTypes(Count);
RUNTIME_BLOCK_parameters(Count);
RUNTIME_BLOCK_setParam(Count);
RUNTIME_BLOCK_getParam(Count);
RUNTIME_BLOCK_activate(Count);
RUNTIME_BLOCK_END(Count);

// Register Clear
RUNTIME_CORE_BLOCK_FACTORY(Clear);
RUNTIME_BLOCK_cleanup(Clear);
RUNTIME_BLOCK_inputTypes(Clear);
RUNTIME_BLOCK_outputTypes(Clear);
RUNTIME_BLOCK_parameters(Clear);
RUNTIME_BLOCK_inferTypes(Clear);
RUNTIME_BLOCK_consumedVariables(Clear);
RUNTIME_BLOCK_setParam(Clear);
RUNTIME_BLOCK_getParam(Clear);
RUNTIME_BLOCK_activate(Clear);
RUNTIME_BLOCK_END(Clear);

// Register Get
RUNTIME_CORE_BLOCK_FACTORY(Get);
RUNTIME_BLOCK_cleanup(Get);
RUNTIME_BLOCK_destroy(Get);
RUNTIME_BLOCK_inputTypes(Get);
RUNTIME_BLOCK_outputTypes(Get);
RUNTIME_BLOCK_parameters(Get);
RUNTIME_BLOCK_inferTypes(Get);
RUNTIME_BLOCK_consumedVariables(Get);
RUNTIME_BLOCK_setParam(Get);
RUNTIME_BLOCK_getParam(Get);
RUNTIME_BLOCK_activate(Get);
RUNTIME_BLOCK_END(Get);

// Register Swap
RUNTIME_CORE_BLOCK_FACTORY(Swap);
RUNTIME_BLOCK_cleanup(Swap);
RUNTIME_BLOCK_inputTypes(Swap);
RUNTIME_BLOCK_outputTypes(Swap);
RUNTIME_BLOCK_parameters(Swap);
RUNTIME_BLOCK_consumedVariables(Swap);
RUNTIME_BLOCK_setParam(Swap);
RUNTIME_BLOCK_getParam(Swap);
RUNTIME_BLOCK_activate(Swap);
RUNTIME_BLOCK_END(Swap);

// Register Take
RUNTIME_CORE_BLOCK_FACTORY(Take);
RUNTIME_BLOCK_destroy(Take);
RUNTIME_BLOCK_inputTypes(Take);
RUNTIME_BLOCK_outputTypes(Take);
RUNTIME_BLOCK_parameters(Take);
RUNTIME_BLOCK_inferTypes(Take);
RUNTIME_BLOCK_setParam(Take);
RUNTIME_BLOCK_getParam(Take);
RUNTIME_BLOCK_activate(Take);
RUNTIME_BLOCK_END(Take);

// Register Repeat
RUNTIME_CORE_BLOCK_FACTORY(Repeat);
RUNTIME_BLOCK_inputTypes(Repeat);
RUNTIME_BLOCK_outputTypes(Repeat);
RUNTIME_BLOCK_parameters(Repeat);
RUNTIME_BLOCK_setParam(Repeat);
RUNTIME_BLOCK_getParam(Repeat);
RUNTIME_BLOCK_activate(Repeat);
RUNTIME_BLOCK_destroy(Repeat);
RUNTIME_BLOCK_exposedVariables(Repeat);
RUNTIME_BLOCK_inferTypes(Repeat);
RUNTIME_BLOCK_END(Repeat);

// Register Sort
RUNTIME_CORE_BLOCK_FACTORY(Sort);
RUNTIME_BLOCK_inputTypes(Sort);
RUNTIME_BLOCK_outputTypes(Sort);
RUNTIME_BLOCK_activate(Sort);
RUNTIME_BLOCK_parameters(Sort);
RUNTIME_BLOCK_setParam(Sort);
RUNTIME_BLOCK_getParam(Sort);
RUNTIME_BLOCK_cleanup(Sort);
RUNTIME_BLOCK_END(Sort);

LOGIC_OP_DESC(Is);
LOGIC_OP_DESC(IsNot);
LOGIC_OP_DESC(IsMore);
LOGIC_OP_DESC(IsLess);
LOGIC_OP_DESC(IsMoreEqual);
LOGIC_OP_DESC(IsLessEqual);

LOGIC_OP_DESC(Any);
LOGIC_OP_DESC(All);
LOGIC_OP_DESC(AnyNot);
LOGIC_OP_DESC(AllNot);
LOGIC_OP_DESC(AnyMore);
LOGIC_OP_DESC(AllMore);
LOGIC_OP_DESC(AnyLess);
LOGIC_OP_DESC(AllLess);
LOGIC_OP_DESC(AnyMoreEqual);
LOGIC_OP_DESC(AllMoreEqual);
LOGIC_OP_DESC(AnyLessEqual);
LOGIC_OP_DESC(AllLessEqual);

namespace Math {
MATH_BINARY_BLOCK(Add);
MATH_BINARY_BLOCK(Subtract);
MATH_BINARY_BLOCK(Multiply);
MATH_BINARY_BLOCK(Divide);
MATH_BINARY_BLOCK(Xor);
MATH_BINARY_BLOCK(And);
MATH_BINARY_BLOCK(Or);
MATH_BINARY_BLOCK(Mod);
MATH_BINARY_BLOCK(LShift);
MATH_BINARY_BLOCK(RShift);

MATH_UNARY_BLOCK(Abs);
MATH_UNARY_BLOCK(Exp);
MATH_UNARY_BLOCK(Exp2);
MATH_UNARY_BLOCK(Expm1);
MATH_UNARY_BLOCK(Log);
MATH_UNARY_BLOCK(Log10);
MATH_UNARY_BLOCK(Log2);
MATH_UNARY_BLOCK(Log1p);
MATH_UNARY_BLOCK(Sqrt);
MATH_UNARY_BLOCK(Cbrt);
MATH_UNARY_BLOCK(Sin);
MATH_UNARY_BLOCK(Cos);
MATH_UNARY_BLOCK(Tan);
MATH_UNARY_BLOCK(Asin);
MATH_UNARY_BLOCK(Acos);
MATH_UNARY_BLOCK(Atan);
MATH_UNARY_BLOCK(Sinh);
MATH_UNARY_BLOCK(Cosh);
MATH_UNARY_BLOCK(Tanh);
MATH_UNARY_BLOCK(Asinh);
MATH_UNARY_BLOCK(Acosh);
MATH_UNARY_BLOCK(Atanh);
MATH_UNARY_BLOCK(Erf);
MATH_UNARY_BLOCK(Erfc);
MATH_UNARY_BLOCK(TGamma);
MATH_UNARY_BLOCK(LGamma);
MATH_UNARY_BLOCK(Ceil);
MATH_UNARY_BLOCK(Floor);
MATH_UNARY_BLOCK(Trunc);
MATH_UNARY_BLOCK(Round);
}; // namespace Math

void registerBlocksCoreBlocks() {
  REGISTER_CORE_BLOCK(Const);
  REGISTER_CORE_BLOCK(Set);
  REGISTER_CORE_BLOCK(Update);
  REGISTER_CORE_BLOCK(Push);
  REGISTER_CORE_BLOCK(Pop);
  REGISTER_CORE_BLOCK(Clear);
  REGISTER_CORE_BLOCK(Count);
  REGISTER_CORE_BLOCK(Get);
  REGISTER_CORE_BLOCK(Swap);
  REGISTER_CORE_BLOCK(Sleep);
  REGISTER_CORE_BLOCK(Restart);
  REGISTER_CORE_BLOCK(Return);
  REGISTER_CORE_BLOCK(Stop);
  REGISTER_CORE_BLOCK(And);
  REGISTER_CORE_BLOCK(Or);
  REGISTER_CORE_BLOCK(Take);
  REGISTER_CORE_BLOCK(Repeat);
  REGISTER_CORE_BLOCK(Sort);
  REGISTER_CORE_BLOCK(Is);
  REGISTER_CORE_BLOCK(IsNot);
  REGISTER_CORE_BLOCK(IsMore);
  REGISTER_CORE_BLOCK(IsLess);
  REGISTER_CORE_BLOCK(IsMoreEqual);
  REGISTER_CORE_BLOCK(IsLessEqual);
  REGISTER_CORE_BLOCK(Any);
  REGISTER_CORE_BLOCK(All);
  REGISTER_CORE_BLOCK(AnyNot);
  REGISTER_CORE_BLOCK(AllNot);
  REGISTER_CORE_BLOCK(AnyMore);
  REGISTER_CORE_BLOCK(AllMore);
  REGISTER_CORE_BLOCK(AnyLess);
  REGISTER_CORE_BLOCK(AllLess);
  REGISTER_CORE_BLOCK(AnyMoreEqual);
  REGISTER_CORE_BLOCK(AllMoreEqual);
  REGISTER_CORE_BLOCK(AnyLessEqual);
  REGISTER_CORE_BLOCK(AllLessEqual);

  REGISTER_BLOCK(Math, Add);
  REGISTER_BLOCK(Math, Subtract);
  REGISTER_BLOCK(Math, Multiply);
  REGISTER_BLOCK(Math, Divide);
  REGISTER_BLOCK(Math, Xor);
  REGISTER_BLOCK(Math, And);
  REGISTER_BLOCK(Math, Or);
  REGISTER_BLOCK(Math, Mod);
  REGISTER_BLOCK(Math, LShift);
  REGISTER_BLOCK(Math, RShift);

  REGISTER_BLOCK(Math, Abs);
  REGISTER_BLOCK(Math, Exp);
  REGISTER_BLOCK(Math, Exp2);
  REGISTER_BLOCK(Math, Expm1);
  REGISTER_BLOCK(Math, Log);
  REGISTER_BLOCK(Math, Log10);
  REGISTER_BLOCK(Math, Log2);
  REGISTER_BLOCK(Math, Log1p);
  REGISTER_BLOCK(Math, Sqrt);
  REGISTER_BLOCK(Math, Cbrt);
  REGISTER_BLOCK(Math, Sin);
  REGISTER_BLOCK(Math, Cos);
  REGISTER_BLOCK(Math, Tan);
  REGISTER_BLOCK(Math, Asin);
  REGISTER_BLOCK(Math, Acos);
  REGISTER_BLOCK(Math, Atan);
  REGISTER_BLOCK(Math, Sinh);
  REGISTER_BLOCK(Math, Cosh);
  REGISTER_BLOCK(Math, Tanh);
  REGISTER_BLOCK(Math, Asinh);
  REGISTER_BLOCK(Math, Acosh);
  REGISTER_BLOCK(Math, Atanh);
  REGISTER_BLOCK(Math, Erf);
  REGISTER_BLOCK(Math, Erfc);
  REGISTER_BLOCK(Math, TGamma);
  REGISTER_BLOCK(Math, LGamma);
  REGISTER_BLOCK(Math, Ceil);
  REGISTER_BLOCK(Math, Floor);
  REGISTER_BLOCK(Math, Trunc);
  REGISTER_BLOCK(Math, Round);
}
}; // namespace chainblocks