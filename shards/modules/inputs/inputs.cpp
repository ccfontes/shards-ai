/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2021 Fragcolor Pte. Ltd. */

#include <SDL.h>
#include "input/events.hpp"
#include "inputs.hpp"
#include "shards/linalg_shim.hpp"
#include <shards/shards.h>
#include <shards/shards.hpp>
#include <shards/core/shared.hpp>
#include <shards/input/master.hpp>
#include <shards/modules/gfx/gfx.hpp>
#include <shards/modules/gfx/window.hpp>
#include <SDL_keyboard.h>
#include <SDL_keycode.h>
#include <shards/core/params.hpp>
#include <variant>

using namespace linalg::aliases;

namespace shards {
namespace input {

struct Base {
  RequiredInputContext _inputContext;

  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }

  void baseWarmup(SHContext *context) { _inputContext.warmup(context); }
  void baseCleanup() { _inputContext.cleanup(); }
  SHExposedTypesInfo baseRequiredVariables() {
    static auto e = exposedTypesOf(decltype(_inputContext)::getExposedTypeInfo());
    return e;
  }

  SHExposedTypesInfo requiredVariables() { return baseRequiredVariables(); }
  void warmup(SHContext *context) { baseWarmup(context); }
  void cleanup() { baseCleanup(); }
};

struct MousePixelPos : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::NoneType; }
  static SHTypesInfo outputTypes() { return CoreInfo::Int2Type; }

  SHTypeInfo compose(const SHInstanceData &data) {
    return CoreInfo::Int2Type;
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    auto &state = _inputContext->getState();
    auto &region = state.region;
    float2 scale = float2(region.pixelSize) / region.size;
    return toVar(int2(state.cursorPosition * scale));
  }
};

struct MouseDelta : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::NoneType; }
  static SHTypesInfo outputTypes() { return CoreInfo::Float2Type; }

  SHTypeInfo compose(const SHInstanceData &data) { return CoreInfo::Float2Type; }

  SHVar activate(SHContext *context, const SHVar &input) {
    float2 delta{};
    for (auto &event : _inputContext->getEvents()) {
      if (const PointerMoveEvent *pme = std::get_if<PointerMoveEvent>(&event)) {
        delta += pme->delta;
      }
    }
    return toVar(delta);
  }
};

struct MousePos : public Base {
  int _width;
  int _height;

  static SHTypesInfo inputTypes() { return CoreInfo::NoneType; }
  static SHTypesInfo outputTypes() { return CoreInfo::Float2Type; }

  SHTypeInfo compose(const SHInstanceData &data) { return CoreInfo::Float2Type; }

  SHVar activate(SHContext *context, const SHVar &input) {
    float2 cursorPosition = _inputContext->getState().cursorPosition;
    return toVar(cursorPosition);
  }
};

struct InputRegionSize : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::NoneType; }
  static SHTypesInfo outputTypes() { return CoreInfo::Int2Type; }

  SHTypeInfo compose(const SHInstanceData &data) { return CoreInfo::Float2Type; }

  SHVar activate(SHContext *context, const SHVar &input) {
    int2 size = (int2)_inputContext->getState().region.size;
    return toVar(size);
  }
};

struct Mouse : public Base {
  ParamVar _hidden{Var(false)};
  bool _isHidden{false};
  ParamVar _captured{Var(false)};
  bool _isCaptured{false};
  ParamVar _relative{Var(false)};
  bool _isRelative{false};
  SDL_Window *_capturedWindow{};

  static inline Parameters params{
      {"Hidden", SHCCSTR("If the cursor should be hidden."), {CoreInfo::BoolType}},
      {"Capture", SHCCSTR("If the mouse should be confined to the application window."), {CoreInfo::BoolType}},
      {"Relative", SHCCSTR("If the mouse should only report relative movements."), {CoreInfo::BoolType}}};

  static SHParametersInfo parameters() { return params; }

  void setParam(int index, const SHVar &value) {
    switch (index) {
    case 0:
      _hidden = value;
      break;
    case 1:
      _captured = value;
      break;
    case 2:
      _relative = value;
      break;
    default:
      break;
    }
  }

  SHVar getParam(int index) {
    switch (index) {
    case 0:
      return _hidden;
    case 1:
      return _captured;
    case 2:
      return _relative;
    default:
      throw InvalidParameterIndex();
    }
  }

  SHTypeInfo compose(const SHInstanceData &data) { return data.inputType; }

  void warmup(SHContext *context) {
    _hidden.warmup(context);
    _captured.warmup(context);
    _relative.warmup(context);
    baseWarmup(context);
  }

  void cleanup() {
    _hidden.cleanup();
    _captured.cleanup();
    _relative.cleanup();

    setHidden(false);
    setCaptured(false);
    setRelative(false);
  }

  void setHidden(bool hidden) {
    if (hidden != _isHidden) {
      // SDL_ShowCursor(hidden ? SDL_DISABLE : SDL_ENABLE);
      _isHidden = hidden;
    }
  }

  void setRelative(bool relative) {
    if (relative != _isRelative) {
      // SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);
      _isRelative = relative;
    }
  }

  void setCaptured(bool captured) {
    if (captured != _isCaptured) {
      // SDL_Window *windowToCapture = _windowContext->getSdlWindow();
      // SDL_SetWindowGrab(windowToCapture, captured ? SDL_TRUE : SDL_FALSE);
      // _capturedWindow = captured ? windowToCapture : nullptr;
      _isCaptured = captured;
    }
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    // setHidden(_hidden.get().payload.boolValue);
    // setCaptured(_captured.get().payload.boolValue);
    // setRelative(_relative.get().payload.boolValue);
    // TODO

    return input;
  }
};

template <bool Pressed> struct MouseUpDown : public Base {
  static inline Parameters params{
      {"Left", SHCCSTR("The action to perform when the left mouse button is pressed down."), {CoreInfo::ShardsOrNone}},
      {"Right",
       SHCCSTR("The action to perform when the right mouse button is pressed "
               "down."),
       {CoreInfo::ShardsOrNone}},
      {"Middle",
       SHCCSTR("The action to perform when the middle mouse button is pressed "
               "down."),
       {CoreInfo::ShardsOrNone}}};

  static SHParametersInfo parameters() { return params; }

  void setParam(int index, const SHVar &value) {
    switch (index) {
    case 0:
      _leftButton = value;
      break;
    case 1:
      _rightButton = value;
      break;
    case 2:
      _middleButton = value;
      break;
    default:
      break;
    }
  }

  SHVar getParam(int index) {
    switch (index) {
    case 0:
      return _leftButton;
    case 1:
      return _rightButton;
    case 2:
      return _middleButton;
    default:
      throw InvalidParameterIndex();
    }
  }

  ShardsVar _leftButton{};
  ShardsVar _rightButton{};
  ShardsVar _middleButton{};

  SHTypeInfo compose(const SHInstanceData &data) {
    _leftButton.compose(data);
    _rightButton.compose(data);
    _middleButton.compose(data);

    return data.inputType;
  }

  void cleanup() {
    _leftButton.cleanup();
    _rightButton.cleanup();
    _middleButton.cleanup();
    baseCleanup();
  }

  void warmup(SHContext *context) {
    _leftButton.warmup(context);
    _rightButton.warmup(context);
    _middleButton.warmup(context);
    baseWarmup(context);
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    // TODO: Input
    for (auto &event : _inputContext->getEvents()) {
      if (const PointerButtonEvent *pbe = std::get_if<PointerButtonEvent>(&event)) {
        if (pbe->pressed == Pressed) {
          SHVar output{};
          if (pbe->index == SDL_BUTTON_LEFT) {
            _leftButton.activate(context, input, output);
          } else if (pbe->index == SDL_BUTTON_RIGHT) {
            _rightButton.activate(context, input, output);
          } else if (pbe->index == SDL_BUTTON_MIDDLE) {
            _middleButton.activate(context, input, output);
          }
        }
      }
    }
    return input;
  }
};

static inline std::map<std::string, SDL_Keycode> KeycodeMap = {
    // note: keep the same order as in SDL_keycode.h
    {"return", SDLK_RETURN},
    {"escape", SDLK_ESCAPE},
    {"backspace", SDLK_BACKSPACE},
    {"tab", SDLK_TAB},
    {"space", SDLK_SPACE},
    {"f1", SDLK_F1},
    {"f2", SDLK_F2},
    {"f3", SDLK_F3},
    {"f4", SDLK_F4},
    {"f5", SDLK_F5},
    {"f6", SDLK_F6},
    {"f7", SDLK_F7},
    {"f8", SDLK_F8},
    {"f9", SDLK_F9},
    {"f10", SDLK_F10},
    {"f11", SDLK_F11},
    {"f12", SDLK_F12},
    {"pause", SDLK_PAUSE},
    {"insert", SDLK_INSERT},
    {"home", SDLK_HOME},
    {"pageUp", SDLK_PAGEUP},
    {"delete", SDLK_DELETE},
    {"end", SDLK_END},
    {"pageDown", SDLK_PAGEDOWN},
    {"right", SDLK_RIGHT},
    {"left", SDLK_LEFT},
    {"down", SDLK_DOWN},
    {"up", SDLK_UP},
    {"divide", SDLK_KP_DIVIDE},
    {"multiply", SDLK_KP_MULTIPLY},
    {"subtract", SDLK_KP_MINUS},
    {"add", SDLK_KP_PLUS},
    {"enter", SDLK_KP_ENTER},
    {"num1", SDLK_KP_1},
    {"num2", SDLK_KP_2},
    {"num3", SDLK_KP_3},
    {"num4", SDLK_KP_4},
    {"num5", SDLK_KP_5},
    {"num6", SDLK_KP_6},
    {"num7", SDLK_KP_7},
    {"num8", SDLK_KP_8},
    {"num9", SDLK_KP_9},
    {"num0", SDLK_KP_0},
    {"leftCtrl", SDLK_LCTRL},
    {"leftShift", SDLK_LSHIFT},
    {"leftAlt", SDLK_LALT},
    {"rightCtrl", SDLK_RCTRL},
    {"rightShift", SDLK_RSHIFT},
    {"rightAlt", SDLK_RALT},
};

inline SDL_Keycode keyStringToKeyCode(const std::string &str) {
  if (str.empty()) {
    return SDLK_UNKNOWN;
  }

  if (str.length() == 1) {
    if ((str[0] >= ' ' && str[0] <= '@') || (str[0] >= '[' && str[0] <= 'z')) {
      return SDL_Keycode(str[0]);
    }
    if (str[0] >= 'A' && str[0] <= 'Z') {
      return SDL_Keycode(str[0] + 32);
    }
  }

  auto it = KeycodeMap.find(str);
  if (it != KeycodeMap.end())
    return it->second;

  throw SHException(fmt::format("Unknown key identifier: {}", str));
}

template <bool Pressed> struct KeyUpDown : public Base {
  static SHParametersInfo parameters() { return _params; }

  void setParam(int index, const SHVar &value) {
    switch (index) {
    case 0: {
      if (value.valueType == SHType::None) {
        _key.clear();
      } else {
        _key = SHSTRVIEW(value);
      }
      _keyCode = keyStringToKeyCode(_key);
    } break;
    case 1:
      _shards = value;
      break;
    case 2:
      _repeat = value.payload.boolValue;
      break;
    default:
      break;
    }
  }

  SHVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(_key);
    case 1:
      return _shards;
    case 2:
      return Var(_repeat);
    default:
      throw InvalidParameterIndex();
    }
  }

  void cleanup() {
    _shards.cleanup();
    baseCleanup();
  }

  void warmup(SHContext *context) {
    _shards.warmup(context);
    baseWarmup(context);
  }

  SHTypeInfo compose(const SHInstanceData &data) {
    _shards.compose(data);
    return data.inputType;
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    auto &events = _inputContext->getEvents();
    for (auto &event : events) {
      if (const KeyEvent *ke = std::get_if<KeyEvent>(&event)) {
        if (ke->pressed == Pressed && ke->key == _keyCode) {

          if (_repeat || ke->repeat == 0) {
            SHVar output{};
            _shards.activate(context, input, output);
          }
        }
      }
    }

    return input;
  }

private:
  static inline Parameters _params = {
      {"Key", SHCCSTR("TODO!"), {{CoreInfo::StringType}}},
      {"Action", SHCCSTR("TODO!"), {CoreInfo::ShardsOrNone}},
      {"Repeat", SHCCSTR("TODO!"), {{CoreInfo::BoolType}}},
  };

  ShardsVar _shards{};
  std::string _key;
  SDL_Keycode _keyCode;
  bool _repeat{false};
};

struct IsKeyDown : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::NoneType; }
  static SHTypesInfo outputTypes() { return CoreInfo::BoolType; }

  SDL_Keycode _keyCode;
  std::string _keyName;

  static inline Parameters _params = {
      {"Key", SHCCSTR("The name of the key to check."), {{CoreInfo::StringType}}},
  };

  static SHParametersInfo parameters() { return _params; }

  void setParam(int index, const SHVar &value) {
    switch (index) {
    case 0: {
      if (value.valueType == SHType::None) {
        _keyName.clear();
      } else {
        _keyName = SHSTRVIEW(value);
      }
      _keyCode = keyStringToKeyCode(_keyName);
    } break;
    default:
      break;
    }
  }

  SHVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(_keyName);
    default:
      throw InvalidParameterIndex();
    }
  }

  void cleanup() { baseCleanup(); }
  void warmup(SHContext *context) { baseWarmup(context); }

  SHVar activate(SHContext *context, const SHVar &input) { return Var(_inputContext->getState().isKeyHeld(_keyCode)); }
};

struct HandleURL : public Base {
  std::string _url;

  PARAM(ShardsVar, _action, "Action", "The Shards to run if a text/file drop event happened.", {CoreInfo::ShardsOrNone});
  PARAM_IMPL(PARAM_IMPL_FOR(_action));

  void cleanup() {
    PARAM_CLEANUP();

    Base::cleanup();
  }

  void warmup(SHContext *context) {
    Base::warmup(context);

    PARAM_WARMUP(context);
  }

  SHTypeInfo compose(const SHInstanceData &data) {
    auto dataCopy = data;
    dataCopy.inputType = CoreInfo::StringType;
    _action.compose(data);
    return data.inputType;
  }

  static SHTypeInfo inputType() { return CoreInfo::AnyType; }
  static SHTypeInfo outputType() { return CoreInfo::AnyType; }

  SHVar activate(SHContext *context, const SHVar &input) {
    // TODO: Input
    // for (auto &ev : _windowContext->events) {
    //   if (ev.type == SDL_DROPFILE || ev.type == SDL_DROPTEXT) {
    //     _url.clear();
    //     auto url = ev.drop.file;
    //     _url.append(url);
    //     SDL_free(url);

    //     SHVar output{};
    //     _action.activate(context, Var(_url), output);

    //     break;
    //   }
    // }

    return input;
  }
};

} // namespace input
} // namespace shards

SHARDS_REGISTER_FN(inputs) {
  using namespace shards::input;
  REGISTER_SHARD("Inputs.Size", InputRegionSize);
  REGISTER_SHARD("Inputs.MousePixelPos", MousePixelPos);
  REGISTER_SHARD("Inputs.MousePos", MousePos);
  REGISTER_SHARD("Inputs.MouseDelta", MouseDelta);
  REGISTER_SHARD("Inputs.Mouse", Mouse);
  REGISTER_SHARD("Inputs.IsKeyDown", IsKeyDown);
  REGISTER_SHARD("Inputs.HandleURL", HandleURL);

  using MouseDown = MouseUpDown<true>;
  using MouseUp = MouseUpDown<false>;
  REGISTER_SHARD("Inputs.MouseDown", MouseDown);
  REGISTER_SHARD("Inputs.MouseUp", MouseUp);

  using KeyDown = KeyUpDown<true>;
  using KeyUp = KeyUpDown<false>;
  REGISTER_SHARD("Inputs.KeyDown", KeyDown);
  REGISTER_SHARD("Inputs.KeyUp", KeyUp);
}
