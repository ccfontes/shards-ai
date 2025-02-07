#ifndef DB3B2F03_117E_49DC_93AA_28A94118C09D
#define DB3B2F03_117E_49DC_93AA_28A94118C09D

#include "renderer_types.hpp"
#include "drawable_processor.hpp"
#include "sampler_cache.hpp"
#include "view.hpp"
#include "gfx_wgpu.hpp"
#include "platform.hpp"

namespace gfx::detail {

// Defines some helper functions for implementing drawable processors
struct BindGroupBuilder {
  using allocator_type = shards::pmr::PolymorphicAllocator<>;

  shards::pmr::vector<WGPUBindGroupEntry> entries;

  BindGroupBuilder(allocator_type allocator) : entries(allocator) {}

  void addBinding(const detail::BufferBinding &binding, WGPUBuffer buffer, size_t numArrayElements = 1, size_t arrayIndex = 0) {
    auto &entry = entries.emplace_back();
    entry.binding = binding.index;
    entry.size = binding.layout.getArrayStride() * numArrayElements;
    entry.offset = binding.layout.getArrayStride() * arrayIndex;
    entry.buffer = buffer;
  }

  void addTextureBinding(const gfx::shader::TextureBinding &binding, WGPUTextureView texture, WGPUSampler sampler) {
    auto &entry = entries.emplace_back();
    entry.binding = binding.binding;
    entry.textureView = texture;

    auto &samplerEntry = entries.emplace_back();
    samplerEntry.binding = binding.defaultSamplerBinding;
    samplerEntry.sampler = sampler;
  }

  WGPUBindGroup finalize(WGPUDevice device, WGPUBindGroupLayout layout, const char *label = nullptr) {
    WGPUBindGroupDescriptor desc{
        .label = label,
        .layout = layout,
        .entryCount = uint32_t(entries.size()),
        .entries = entries.data(),
    };
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &desc);
    assert(bindGroup);
    return bindGroup;
  }
};

// Defines a range of drawable
struct DrawGroup {
  typedef std::vector<UniqueId> ContainerType;

  size_t startIndex;
  size_t numInstances;

  DrawGroup(size_t startIndex, size_t numInstances) : startIndex(startIndex), numInstances(numInstances) {}

  ContainerType::const_iterator getBegin(const ContainerType &drawables) const {
    return std::next(drawables.begin(), startIndex);
  }
  ContainerType::const_iterator getEnd(const ContainerType &drawables) const {
    return std::next(drawables.begin(), startIndex + numInstances);
  }
};

// Creates draw groups ranges based on DrawGroupKey
// contiguous elements with matching keys will be grouped into the same draw group
template <typename TKeygen, typename TAlloc>
inline void groupDrawables(size_t numDrawables, std::vector<detail::DrawGroup, TAlloc> &groups, TKeygen &&keyGenerator) {
  if (numDrawables > 0) {
    size_t groupStart = 0;
    auto currentDrawGroupKey = keyGenerator(0);

    auto finishGroup = [&](size_t currentIndex) {
      size_t groupLength = currentIndex - groupStart;
      if (groupLength > 0) {
        groups.emplace_back(groupStart, groupLength);
      }
    };
    for (size_t i = 1; i < numDrawables; i++) {
      auto drawGroupKey = keyGenerator(i);
      if (drawGroupKey != currentDrawGroupKey) {
        finishGroup(i);
        groupStart = i;
        currentDrawGroupKey = drawGroupKey;
      }
    }
    finishGroup(numDrawables);
  }
}

inline void packDrawData(uint8_t *outData, size_t outSize, const UniformBufferLayout &layout,
                         const ParameterStorage &parameterStorage) {
  size_t layoutIndex = 0;
  for (const std::string &fieldName : layout.fieldNames) {
#if GFX_ANDROID
    auto drawDataIt = parameterStorage.basic.find(fieldName.c_str());
#else
    auto drawDataIt = parameterStorage.basic.find<std::string>(fieldName);
#endif
    if (drawDataIt != parameterStorage.basic.end()) {
      const UniformLayout &itemLayout = layout.items[layoutIndex];
      NumFieldType drawDataFieldType = getParamVariantType(drawDataIt->second);
      if (itemLayout.type != drawDataFieldType) {
        SPDLOG_LOGGER_WARN(getLogger(), "Shader field \"{}\" type mismatch layout:{} parameterStorage:{}", fieldName,
                           itemLayout.type, drawDataFieldType);
      } else {
        packParamVariant(outData + itemLayout.offset, outSize - itemLayout.offset, drawDataIt->second);
      }
    }
    layoutIndex++;
  }
}

inline void setViewParameters(ParameterStorage &outDrawData, const ViewData &viewData) {
  outDrawData.setParam("view", viewData.view->view);
  outDrawData.setParam("invView", viewData.cachedView.invViewTransform);
  outDrawData.setParam("proj", viewData.cachedView.projectionTransform);
  outDrawData.setParam("invProj", viewData.cachedView.invProjectionTransform);
  outDrawData.setParam("viewport", float4(float(viewData.viewport.x), float(viewData.viewport.y), float(viewData.viewport.width),
                                          float(viewData.viewport.height)));
}

struct TextureData {
  const TextureContextData *data{};
  WGPUSampler sampler{};
  UniqueId id{};

  operator bool() const { return data != nullptr; }
};

template <typename TTextureBindings>
auto mapTextureBinding(const TTextureBindings &textureBindings, const char *name) -> int32_t {
  auto it = std::find_if(textureBindings.begin(), textureBindings.end(), [&](auto it) { return it.name == name; });
  if (it != textureBindings.end())
    return int32_t(it - textureBindings.begin());
  return -1;
};

template <typename TTextureBindings, typename TOutTextures>
auto setTextureParameter(const TTextureBindings &textureBindings, TOutTextures &outTextures, Context &context,
                         SamplerCache &samplerCache, size_t frameCounter, const char *name, const TexturePtr &texture) {
  int32_t targetSlot = mapTextureBinding(textureBindings, name);
  if (targetSlot >= 0) {
    bool isFilterable = textureBindings[targetSlot].type.format == TextureSampleType::Float;
    outTextures[targetSlot].data = &texture->createContextDataConditional(context);
    outTextures[targetSlot].sampler = samplerCache.getDefaultSampler(frameCounter, isFilterable, texture);
    outTextures[targetSlot].id = texture->getId();
  }
};

template <typename TTextureBindings, typename TOutTextures, typename TSrc>
auto applyParameters(const TTextureBindings &textureBindings, TOutTextures &outTextures, Context &context,
                     SamplerCache &samplerCache, size_t frameCounter, ParameterStorage &dstParams, const TSrc &srcParam) {
  for (auto &param : srcParam.basic)
    dstParams.setParam(param.first.c_str(), param.second);
  for (auto &param : srcParam.textures)
    setTextureParameter(textureBindings, outTextures, context, samplerCache, frameCounter, param.first.c_str(), param.second.texture);
};

} // namespace gfx::detail

#endif /* DB3B2F03_117E_49DC_93AA_28A94118C09D */
