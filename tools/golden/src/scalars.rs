//! Rust's f32 min/max/clamp on ties (equal values, -0 against +0), evaluated at run time.
use std::fmt::Write as _;
use std::hint::black_box;

pub fn csv() -> String {
    let vals = [-0.0f32, 0.0, 1.0, -1.0, 0.5, 1e-9];
    let mut s = String::from("a,b,min,max,clamp01,clampz\n");
    for a in vals {
        for b in vals {
            let (a, b) = (black_box(a), black_box(b));
            writeln!(s, "{},{},{},{},{},{}", a, b, a.min(b), a.max(b), a.clamp(0.0, 1.0), a.clamp(-0.0, 0.0)).unwrap();
        }
    }
    s
}
