//! The M0 box scene and BoxWorld::sweep cases (structured, TOUCH band, then random).
use crate::common::{f, Lcg};
use faith_move::world::{Aabb, BoxWorld, World};
use glam::Vec3;
use std::fmt::Write as _;

/// The golden scene. The floor is two flush boxes, so seams get tested too.
pub const SCENE: [([f32; 3], [f32; 3]); 6] = [
    ([-10.0, -1.0, -10.0], [0.0, 0.0, 10.0]),  // floor, west half
    ([0.0, -1.0, -10.0], [10.0, 0.0, 10.0]),   // floor, east half (seam at x = 0)
    ([2.0, 0.0, -1.0], [3.0, 3.0, 1.0]),       // wall
    ([-4.0, 0.0, 2.0], [-2.0, 1.2, 4.0]),      // waist-high block
    ([-1.0, 2.5, -6.0], [1.0, 2.7, -3.0]),     // overhead pipe
    ([4.0, 0.0, 4.0], [4.5, 6.0, 4.5]),        // pillar
];

pub fn row(w: &dyn World, half: Vec3, start: Vec3, delta: Vec3) -> String {
    match w.sweep(half, start, delta) {
        Some(h) => format!("{},{},{},1,{},{},{}\n", f(half), f(start), f(delta), h.t, f(h.normal), f(h.point)),
        None => format!("{},{},{},0,0,0,0,0,0,0,0\n", f(half), f(start), f(delta)),
    }
}

/// Starts exactly on (and 1e-5 either side of) every face of every box, and diagonal corner
/// approaches: the boundary cases random floats never reach.
fn structured(w: &dyn World, out: &mut String) {
    let halves = [Vec3::new(0.3, 0.9, 0.3), Vec3::new(0.5, 0.9, 0.5)];
    let offs = [0.0f32, 1e-5, -1e-5, 0.25];
    for (lo, hi) in SCENE {
        let (lo, hi) = (Vec3::from(lo), Vec3::from(hi));
        let mid = (lo + hi) * 0.5;
        for half in halves {
            for a in 0..3 {
                for s in [-1.0f32, 1.0] {
                    for off in offs {
                        // Outside the face on side `s` of axis `a`, `off` away from touching.
                        let mut start = mid;
                        start[a] = if s < 0.0 { lo[a] - half[a] - off } else { hi[a] + half[a] + off };
                        let mut into = Vec3::ZERO;
                        into[a] = -s * 0.5;
                        let mut deltas = vec![Vec3::ZERO, into, -into]; // still, into the face, away
                        for b in 0..3 {
                            if b == a { continue; }
                            for t in [-0.5f32, 0.5] {
                                let mut side = Vec3::ZERO;
                                side[b] = t;
                                deltas.push(side);        // along the face
                                deltas.push(side + into); // diagonally into it
                            }
                        }
                        for d in deltas { out.push_str(&row(w, half, start, d)); }
                    }
                }
            }
            // Corner approaches: from outside the (min x, min z) vertical edge, and over the top edge.
            for off in offs {
                let c = Vec3::new(lo.x - half.x - off, mid.y, lo.z - half.z - off);
                for d in [Vec3::new(1.0, 0.0, 1.0), Vec3::new(1.0, 0.0, 0.5), Vec3::new(0.5, 0.0, 1.0)] {
                    out.push_str(&row(w, half, c, d));
                }
                let top = Vec3::new(lo.x - half.x - off, hi.y + half.y + off, mid.z);
                out.push_str(&row(w, half, top, Vec3::new(1.0, -1.0, 0.0)));
            }
        }
    }
}

/// The TOUCH band (1e-4) decides hit or no hit for moves that end, or slide, a hair inside a
/// face. Random and 0.5 m structured moves never land there; these do.
fn touch_band(w: &dyn World, out: &mut String) {
    let halves = [Vec3::new(0.3, 0.9, 0.3), Vec3::new(0.5, 0.9, 0.5)];
    for half in halves {
        // Moves that end exactly on a face, and 1e-5 / 5e-5 past it.
        for (lo, hi) in SCENE {
            let (lo, hi) = (Vec3::from(lo), Vec3::from(hi));
            let mid = (lo + hi) * 0.5;
            for a in 0..3 {
                for s in [-1.0f32, 1.0] {
                    for e in [0.0f32, 1e-5, 5e-5] {
                        let mut start = mid;
                        start[a] = if s < 0.0 { lo[a] - half[a] - 0.25 } else { hi[a] + half[a] + 0.25 };
                        let mut d = Vec3::ZERO;
                        d[a] = -s * (0.25 + e);
                        out.push_str(&row(w, half, start, d));
                    }
                }
            }
        }
        // Sunk into (or just above) the floor by less than TOUCH, sliding across the seam at x = 0.
        for sink in [0.0f32, 5e-5, -5e-5] {
            for d in [Vec3::new(2.0, 0.0, 0.0), Vec3::new(2.0, 0.0, 0.5), Vec3::new(-2.0, 0.0, 0.0)] {
                let start = Vec3::new(-d.x.signum(), half.y - sink, 0.0);
                out.push_str(&row(w, half, start, d));
            }
        }
        // A delta component below the 1e-9 zero threshold.
        out.push_str(&row(w, half, Vec3::new(-1.0, half.y, 0.0), Vec3::new(2.0, 1e-10, 0.0)));
    }
}

pub fn scene() -> BoxWorld {
    let mut w = BoxWorld::default();
    for (a, b) in SCENE {
        w.add(Aabb::new(Vec3::from(a), Vec3::from(b)));
    }
    w
}

pub fn scene_csv() -> String {
    let mut s = String::from("minx,miny,minz,maxx,maxy,maxz\n");
    for (a, b) in SCENE {
        writeln!(s, "{},{}", f(Vec3::from(a)), f(Vec3::from(b))).unwrap();
    }
    s
}

pub fn sweeps_csv(w: &BoxWorld) -> String {
    let mut out = String::from("hx,hy,hz,sx,sy,sz,dx,dy,dz,hit,t,nx,ny,nz,px,py,pz\n");
    structured(w, &mut out);
    touch_band(w, &mut out);
    let mut r = Lcg(0x5EED);
    for _ in 0..4000 {
        let half = Vec3::new(r.range(0.1, 0.5), r.range(0.3, 1.0), r.range(0.1, 0.5));
        let start = Vec3::new(r.range(-6.0, 6.0), r.range(0.0, 4.0), r.range(-6.0, 6.0));
        let delta = Vec3::new(r.axis(6.0), r.axis(4.0), r.axis(6.0));
        out.push_str(&row(w, half, start, delta));
    }
    out
}

/// Spots on the floor (and the block's top) next to each box: where collisions happen.
pub fn anchors() -> Vec<Vec3> {
    vec![
        Vec3::new(1.7, 0.0, 0.0),   // touching the wall
        Vec3::new(1.7, 0.0, 1.3),   // at the wall's end
        Vec3::new(-1.7, 0.0, 3.0),  // touching the block
        Vec3::new(-3.0, 1.2, 3.0),  // on the block
        Vec3::new(3.7, 0.0, 3.7),   // at the pillar's corner
        Vec3::new(-0.5, 0.0, -5.0), // by the floor seam
        Vec3::new(0.0, 0.0, -4.5),  // under the pipe
    ]
}
