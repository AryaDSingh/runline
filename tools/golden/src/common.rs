//! Shared by every golden module: the random source, number printing, and file writing.
use glam::Vec3;

/// Deterministic LCG, so the files only change when the reference does.
pub struct Lcg(pub u64);
impl Lcg {
    pub fn next(&mut self) -> f32 {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((self.0 >> 40) as f32) / ((1u64 << 24) as f32)
    }
    pub fn range(&mut self, lo: f32, hi: f32) -> f32 { lo + (hi - lo) * self.next() }
    /// Snap a third of values to 0 so the zero-delta branch is exercised.
    pub fn axis(&mut self, m: f32) -> f32 { if self.next() < 0.33 { 0.0 } else { self.range(-m, m) } }
}

// `{}` prints an f32 in the shortest form that round-trips, so C++'s stof reads back the same bits.
pub fn f(v: Vec3) -> String { format!("{},{},{}", v.x, v.y, v.z) }

pub fn golden_dir() -> String {
    concat!(env!("CARGO_MANIFEST_DIR"), "/../../RunlineCore/tests/golden").to_string()
}

pub fn write(dir: &str, name: &str, body: String) {
    std::fs::write(format!("{dir}/{name}"), body).unwrap();
    println!("wrote {name}");
}

/// Record which faith-runner commit this data came from, so a regenerated file can be traced.
pub fn write_reference(dir: &str) {
    let clone = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../faith-runner");
    let git = |args: &[&str]| {
        std::process::Command::new("git").arg("-C").arg(clone).args(args).output()
            .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
            .unwrap_or_default()
    };
    let sha = git(&["rev-parse", "HEAD"]);
    assert!(!sha.is_empty(), "can't read faith-runner's commit from {clone}");
    let dirty = if git(&["status", "--porcelain"]).is_empty() { "" } else { " (dirty)" };
    std::fs::write(format!("{dir}/REFERENCE"), format!("faith-runner {sha}{dirty}\n")).unwrap();
    println!("wrote REFERENCE (faith-runner {sha}{dirty})");
}
