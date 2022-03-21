#[cfg(feature = "generate")]
extern crate bindgen;
#[cfg(feature = "generate")]
use std::env;
#[cfg(feature = "generate")]
use std::path::PathBuf;

#[cfg(feature = "generate")]
fn generate_bindings() {
    // Tell cargo to invalidate the built crate whenever the wrapper changes
    println!("cargo:rerun-if-changed=wrapper.h");

    // The bindgen::Builder is the main entry point
    // to bindgen, and lets you build up options for
    // the resulting bindings.
    let bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("wrapper.h")
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

fn build_i2c() {
    // Tell Cargo that if the given file changes, to rerun this build script.
    println!("cargo:rerun-if-changed=./");
    println!("cargo:rustc-link-search=/lib/");
    println!("cargo:rustc-link-lib=xenstore");
    println!("cargo:rustc-link-lib=xenforeignmemory");
    println!("cargo:rustc-link-lib=xenevtchn");
    println!("cargo:rustc-link-lib=xendevicemodel");
    println!("cargo:rustc-link-lib=xentoolcore");
    println!("cargo:rustc-link-lib=xentoollog");
    println!("cargo:rustc-link-lib=xencall");
    println!("cargo:rustc-link-lib=xenctrl");

    let files = vec![
        "device.c",
        "xs_dev.c",
        "demu.c",
        "virtio/i2c.c",
        "virtio/core.c",
        "virtio/mmio.c",
        "util/init.c",
        "util/rbtree.c",
        "util/read-write.c",
        "util/util.c",
    ];

    // Use the `cc` crate to build a C file and statically link it.
    cc::Build::new()
        .files(files)
        .define("_GNU_SOURCE", None)
        .define("MAP_IN_ADVANCE", None)
        .include("include")
        .warnings(false)
        .compile("demu");
}

fn main() {
    #[cfg(feature = "generate")]
    generate_bindings();
    build_i2c();
}
