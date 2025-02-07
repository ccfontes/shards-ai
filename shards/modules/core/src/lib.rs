#[macro_use]
extern crate shards;

#[macro_use]
extern crate lazy_static;

pub mod date;
pub mod casting;

#[no_mangle]
pub extern "C" fn shardsRegister_core_rust(core: *mut shards::shardsc::SHCore) {
  unsafe {
    shards::core::Core = core;
  }

  date::registerShards();
  casting::registerShards();
}
