/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::bindings::gfx_TexturePtr;
use crate::bindings::gfx_TexturePtr_getResolution_ext;
use shards::fourCharacterCode;
use shards::shardsc::SHImage;
use shards::shardsc::SHIMAGE_FLAGS_PREMULTIPLIED_ALPHA;
use shards::types::common_type;
use shards::types::ParamVar;
use shards::types::Type;
use shards::types::Var;
use shards::types::FRAG_CC;
use std::ptr::null_mut;

pub const TEXTURE_CC: i32 = fourCharacterCode(*b"tex_");

lazy_static! {
  pub static ref TEXTURE_TYPE: Type = Type::object(FRAG_CC, TEXTURE_CC);
  pub static ref TEXTURE_OR_IMAGE_TYPES: Vec<Type> = vec![common_type::image, *TEXTURE_TYPE];
}

pub fn get_scale(scale_var: &ParamVar) -> Result<egui::Vec2, &'static str> {
  let scale: (f32, f32) = scale_var.get().try_into()?;
  Ok(egui::vec2(scale.0, scale.1))
}

pub struct CachedUIImage {
  texture_handle: Option<egui::TextureHandle>,
  prev_ptr: *mut u8,
}

impl Default for CachedUIImage {
  fn default() -> Self {
    Self {
      texture_handle: None,
      prev_ptr: null_mut(),
    }
  }
}

impl CachedUIImage {
  pub fn invalidate(&mut self) {
    self.prev_ptr = null_mut();
  }

  pub fn get_egui_texture_from_image<'a>(
    &'a mut self,
    input: &Var,
    ui: &egui::Ui,
  ) -> Result<&'a egui::TextureHandle, &'static str> {
    let shimage: &SHImage = input.try_into()?;
    let ptr = shimage.data;
    Ok(if ptr != self.prev_ptr {
      let image: egui::ColorImage = into_egui_image(shimage);
      self.prev_ptr = ptr;
      self.texture_handle.insert(ui.ctx().load_texture(
        format!("UI.Image: {:p}", shimage.data),
        image,
        Default::default(),
      ))
    } else {
      self.texture_handle.as_ref().unwrap()
    })
  }
}

pub fn get_egui_texture_from_gfx(
  input: &Var,
) -> Result<(egui::TextureId, egui::Vec2), &'static str> {
  let texture_ptr: *mut gfx_TexturePtr =
    Var::from_object_ptr_mut_ref::<gfx_TexturePtr>(input, &TEXTURE_TYPE)?;
  let texture_size = {
    let texture_res = unsafe { gfx_TexturePtr_getResolution_ext(texture_ptr) };
    egui::vec2(texture_res.x as f32, texture_res.y as f32)
  };

  Ok((
    egui::epaint::TextureId::User(texture_ptr as u64),
    texture_size,
  ))
}

fn into_egui_image(image: &SHImage) -> egui::ColorImage {
  assert_eq!(image.channels, 4);

  let size = [image.width as _, image.height as _];
  let rgba = unsafe {
    core::slice::from_raw_parts(
      image.data,
      image.width as usize * image.channels as usize * image.height as usize,
    )
  };

  if image.flags & SHIMAGE_FLAGS_PREMULTIPLIED_ALPHA as u8
    == SHIMAGE_FLAGS_PREMULTIPLIED_ALPHA as u8
  {
    let pixels = rgba
      .chunks_exact(4)
      .map(|p| egui::Color32::from_rgba_premultiplied(p[0], p[1], p[2], p[3]))
      .collect();
    egui::ColorImage { size, pixels }
  } else {
    egui::ColorImage::from_rgba_unmultiplied(size, rgba)
  }
}
