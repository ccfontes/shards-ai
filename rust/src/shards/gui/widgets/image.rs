/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use egui::Ui;
use egui::Vec2;

use super::image_util;
use super::Image;
use crate::shard::Shard;
use crate::shards::gui::util;
use crate::shards::gui::FLOAT2_VAR_SLICE;
use crate::shards::gui::PARENTS_UI_NAME;
use crate::shardsc::gfx_TexturePtr;
use crate::shardsc::gfx_TexturePtr_getResolution_ext;
use crate::shardsc::SHImage;
use crate::shardsc::SHType_Image;
use crate::shardsc::SHType_Object;
use crate::shardsc::SHIMAGE_FLAGS_PREMULTIPLIED_ALPHA;
use crate::types::Context;
use crate::types::ExposedTypes;
use crate::types::InstanceData;
use crate::types::OptionalString;
use crate::types::ParamVar;
use crate::types::Parameters;
use crate::types::Type;
use crate::types::Types;
use crate::types::Var;

lazy_static! {
  static ref IMAGE_PARAMETERS: Parameters = vec![(
    cstr!("Scale"),
    cstr!("Scaling to apply to the source image"),
    FLOAT2_VAR_SLICE,
  )
    .into(),];
}

impl Default for Image {
  fn default() -> Self {
    let mut parents = ParamVar::default();
    parents.set_name(PARENTS_UI_NAME);
    Self {
      parents,
      requiring: Vec::new(),
      scale: ParamVar::new((1.0, 1.0).into()),
      cached_ui_image: Default::default(),
    }
  }
}

impl Shard for Image {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.Image")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.Image-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.Image"
  }

  fn help(&mut self) -> OptionalString {
    OptionalString(shccstr!("Display an image in the UI."))
  }

  fn inputTypes(&mut self) -> &Types {
    &image_util::TEXTURE_OR_IMAGE_TYPES
  }

  fn inputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!("The image to display"))
  }

  fn outputTypes(&mut self) -> &Types {
    &image_util::TEXTURE_OR_IMAGE_TYPES
  }

  fn outputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!("The output of this shard will be its input."))
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&IMAGE_PARAMETERS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    match index {
      0 => Ok(self.scale.set_param(value)),
      _ => Err("Invalid parameter index"),
    }
  }

  fn getParam(&mut self, index: i32) -> Var {
    match index {
      0 => self.scale.get_param(),
      _ => Var::default(),
    }
  }

  fn requiredVariables(&mut self) -> Option<&ExposedTypes> {
    self.requiring.clear();

    // Add UI.Parents to the list of required variables
    util::require_parents(&mut self.requiring, &self.parents);

    Some(&self.requiring)
  }

  fn hasCompose() -> bool {
    true
  }

  fn compose(&mut self, data: &InstanceData) -> Result<Type, &str> {
    match data.inputType.basicType {
      SHType_Image => decl_override_activate! {
        data.activate = Image::image_activate;
      },
      SHType_Object if unsafe { data.inputType.details.object.typeId } == image_util::TextureCC => {
        decl_override_activate! {
          data.activate = Image::texture_activate;
        }
      }
      _ => (),
    }
    // Always passthrough the input
    Ok(data.inputType)
  }

  fn warmup(&mut self, context: &Context) -> Result<(), &str> {
    self.parents.warmup(context);
    self.scale.warmup(context);

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.scale.cleanup();
    self.parents.cleanup();

    Ok(())
  }

  fn activate(&mut self, _context: &Context, _input: &Var) -> Result<Var, &str> {
    Err("Invalid input type")
  }
}

impl Image {
  fn activateImage(&mut self, _context: &Context, input: &Var) -> Result<Var, &str> {
    if let Some(ui) = util::get_current_parent(*self.parents.get())? {
      let texture = image_util::ui_image_cached(&mut self.cached_ui_image, input, ui)?;

      let scale = image_util::get_scale(&self.scale, ui)?;
      ui.image(texture, texture.size_vec2() * scale);

      Ok(*input)
    } else {
      Err("No UI parent")
    }
  }

  fn activateTexture(&mut self, _context: &Context, input: &Var) -> Result<Var, &str> {
    if let Some(ui) = util::get_current_parent(*self.parents.get())? {
      let (texture_id, texture_size) = image_util::ui_image_texture(input)?;

      let scale = image_util::get_scale(&self.scale, ui)?;
      ui.image(texture_id, texture_size * scale);

      Ok(*input)
    } else {
      Err("No UI parent")
    }
  }

  impl_override_activate! {
    extern "C" fn image_activate() -> Var {
      Image::activateImage()
    }
  }

  impl_override_activate! {
    extern "C" fn texture_activate() -> Var {
      Image::activateTexture()
    }
  }
}
