// SPDX-License-Identifier: GPL-2.0

#[allow(
    clippy::all,
    deref_nullptr,
    dead_code,
    non_camel_case_types,
    non_upper_case_globals,
    non_snake_case,
    improper_ctypes
)]

mod bindings_raw {
    #[cfg(feature = "generate")]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

    #[cfg(not(feature = "generate"))]
    include!("bindings.rs");
}
pub use bindings_raw::*;
