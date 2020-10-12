use std::env;
use std::path;

fn main() {
    let src = env::var("CARGO_MANIFEST_DIR").unwrap();
    let dst = path::Path::new(&env::var("OUT_DIR").unwrap()).join("built.rs");
    let mut options = built::Options::default();
    options.set_git(true);
    options.set_dependencies(true);
    built::write_built_file_with_opts(&options, src.as_ref(), &dst)
        .expect("Failed to acquire build-time information");
}
