#ifndef GFX_FWD
#define GFX_FWD

#include <memory>

namespace gfx {

struct Context;
struct Pipeline;

struct DrawQueue;
typedef std::shared_ptr<DrawQueue> DrawQueuePtr;

struct IDrawable;
typedef std::shared_ptr<IDrawable> DrawablePtr;

struct Mesh;
typedef std::shared_ptr<Mesh> MeshPtr;

struct Feature;
typedef std::shared_ptr<Feature> FeaturePtr;
typedef std::weak_ptr<Feature> FeatureWeakPtr;

struct IPipelineModifier;
typedef std::shared_ptr<IPipelineModifier> PipelineModifierPtr;

struct View;
typedef std::shared_ptr<View> ViewPtr;

struct Material;
typedef std::shared_ptr<Material> MaterialPtr;

struct Texture;
typedef std::shared_ptr<Texture> TexturePtr;

struct RenderTargetAttachment;
typedef std::shared_ptr<RenderTargetAttachment> RenderTargetAttachmentPtr;

struct RenderTarget;
typedef std::shared_ptr<RenderTarget> RenderTargetPtr;

namespace detail {
struct IDrawableProcessor;
typedef std::shared_ptr<IDrawableProcessor> DrawableProcessorPtr;
} // namespace detail
} // namespace gfx

#endif // GFX_FWD
