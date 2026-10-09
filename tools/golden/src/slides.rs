//! move_axis and slide_move: collide-and-slide, the base of every ground move.
use crate::common::{f, Lcg};
use faith_move::world::{move_axis, slide_move, Body, World};
use glam::Vec3;

/// Standing and crouched (TdMove.ShrinkCollision) bodies.
pub const BODIES: [Body; 2] = [Body { half_width: 0.3, height: 1.8 }, Body { half_width: 0.3, height: 1.22 }];

/// Eight horizontal directions, 45 degrees apart.
pub fn dirs8() -> Vec<Vec3> {
    (0..8)
        .map(|i| {
            let a = i as f32 * std::f32::consts::FRAC_PI_4;
            Vec3::new(a.cos(), 0.0, a.sin())
        })
        .collect()
}

fn slide_row(w: &dyn World, body: Body, feet: Vec3, delta: Vec3) -> String {
    let mut out = feet;
    let ns = slide_move(w, body, &mut out, delta);
    let mut cols = vec![body.half_width.to_string(), body.height.to_string(), f(feet), f(delta), f(out), ns.len().to_string()];
    for i in 0..3 {
        cols.push(f(ns.get(i).copied().unwrap_or(Vec3::ZERO)));
    }
    cols.join(",") + "\n"
}

/// Feet on the ground at each anchor (touching, and sunk 5e-5), in every direction at three
/// lengths, plus a move with a vertical part; then random feet and moves.
pub fn slides_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,dx,dy,dz,ox,oy,oz,count,n1x,n1y,n1z,n2x,n2y,n2z,n3x,n3y,n3z\n");
    for body in BODIES {
        for &a in anchors {
            for d in dirs8() {
                for len in [0.1f32, 1.0, 3.0] {
                    for sink in [0.0f32, 5e-5] {
                        s.push_str(&slide_row(w, body, a - Vec3::Y * sink, d * len));
                    }
                }
            }
            s.push_str(&slide_row(w, body, a, Vec3::new(1.0, 5.0, 0.0))); // the y part is ignored
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..3000 {
        let body = BODIES[(r.next() * 2.0) as usize % 2];
        let y = if r.next() < 0.7 { 0.0 } else { r.range(0.0, 3.0) };
        let feet = Vec3::new(r.range(-6.0, 6.0), y, r.range(-6.0, 6.0));
        let delta = Vec3::new(r.axis(4.0), r.axis(1.0), r.axis(4.0));
        s.push_str(&slide_row(w, body, feet, delta));
    }
    s
}

fn axis_row(w: &dyn World, body: Body, feet: Vec3, axis: usize, d: f32) -> String {
    let mut out = feet;
    let h = move_axis(w, body, &mut out, axis, d);
    format!("{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), axis, d, f(out), h.blocked as u8, f(h.normal))
}

pub fn move_axis_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,axis,d,ox,oy,oz,blocked,nx,ny,nz\n");
    for body in BODIES {
        for &a in anchors {
            for axis in 0..3 {
                for d in [-1.0f32, -0.2, 0.0, 0.2, 1.0] {
                    s.push_str(&axis_row(w, body, a, axis, d));
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let body = BODIES[(r.next() * 2.0) as usize % 2];
        let feet = Vec3::new(r.range(-6.0, 6.0), r.range(0.0, 3.0), r.range(-6.0, 6.0));
        let axis = (r.next() * 3.0) as usize % 3;
        let d = r.range(-3.0, 3.0);
        s.push_str(&axis_row(w, body, feet, axis, d));
    }
    s
}
