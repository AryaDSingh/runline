//! A triangle scene: a wall and a block turned off the grid, a walkable ramp and a steep one.
use crate::common::{f, Lcg};
use crate::{probes, slides, sweeps};
use faith_move::world::MeshWorld;
use glam::Vec3;
use std::fmt::Write as _;

pub fn scene() -> MeshWorld {
    let v = Vec3::new;
    let mut t: Vec<[Vec3; 3]> = vec![];
    t.extend(MeshWorld::quad(v(-12.0, 0.0, -12.0), v(-12.0, 0.0, 12.0), v(12.0, 0.0, 12.0), v(12.0, 0.0, -12.0)));
    t.extend(MeshWorld::oriented_box(v(3.0, 1.5, 0.0), v(0.5, 1.5, 2.0), 0.4712)); // wall turned 27 degrees
    t.extend(MeshWorld::oriented_box(v(-3.0, 0.6, 3.0), v(1.0, 0.6, 1.0), -0.3)); // waist-high block
    t.extend(MeshWorld::quad(v(4.0, 0.0, 4.0), v(8.0, 2.0, 4.0), v(8.0, 2.0, 7.0), v(4.0, 0.0, 7.0))); // walkable ramp
    t.extend(MeshWorld::quad(v(-8.0, 0.0, -2.0), v(-7.0, 1.732, -2.0), v(-7.0, 1.732, 2.0), v(-8.0, 0.0, 2.0))); // 60-degree ramp
    // A panel tilted about two axes, with no edge along any axis: the only geometry where the
    // separating-axis test's face-normal axis decides anything.
    t.extend(MeshWorld::quad(v(-6.0, 0.2, -7.0), v(-4.2, 1.4, -7.6), v(-3.4, 1.9, -5.8), v(-5.2, 0.7, -5.2)));
    MeshWorld::new(t, vec![])
}

/// The vertical edges of the turned wall and block, each with the outward diagonal there.
fn box_edges() -> Vec<(Vec3, Vec3)> {
    let mut out = vec![];
    for (c, half, yaw) in [(Vec3::new(3.0, 1.5, 0.0), Vec3::new(0.5, 1.5, 2.0), 0.4712f32), (Vec3::new(-3.0, 0.6, 3.0), Vec3::new(1.0, 0.6, 1.0), -0.3)] {
        let r = glam::Quat::from_rotation_y(yaw);
        for (sx, sz) in [(-1.0f32, -1.0f32), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)] {
            let corner = c + r * Vec3::new(sx * half.x, 0.0, sz * half.z);
            let out_dir = (r * Vec3::new(sx, 0.0, sz)).normalize();
            out.push((Vec3::new(corner.x, 0.0, corner.z), out_dir));
        }
    }
    out
}

/// Moves aimed at places where two surfaces meet within a hair of each other: straight at the
/// turned boxes' vertical edges (and just either side of them), and along the line where the
/// ramp leaves the floor. These decide which surface wins at an edge.
fn edge_cases(w: &MeshWorld, s: &mut String) {
    for half in [Vec3::new(0.3, 0.9, 0.3), Vec3::splat(0.05)] {
        for (edge, out_dir) in box_edges() {
            let side = Vec3::new(-out_dir.z, 0.0, out_dir.x);
            for skew in [0.0f32, 0.02, -0.02, 0.2, -0.2] {
                let d = -(out_dir + side * skew).normalize();
                let start = edge + out_dir * (half.x * 2.0 + 0.3) + Vec3::Y * (half.y + 0.01);
                s.push_str(&sweeps::row(w, half, start, d * 1.0));
            }
        }
        // Inside corners: dropping slightly while moving into a face of the turned wall, so the
        // floor and the wall are met within a fraction of a millimetre of each other (either
        // order). The box reaches the wall at t = 0.3 and the floor at t = 0.3 * (1 + eps).
        let r = glam::Quat::from_rotation_y(0.4712);
        let (wc, wh) = (Vec3::new(3.0, 0.0, 0.0), Vec3::new(0.5, 1.5, 2.0));
        for local in [Vec3::X, -Vec3::X, Vec3::Z, -Vec3::Z] {
            let n = r * local;
            let face = wc + n * (local.x.abs() * wh.x + local.z.abs() * wh.z);
            let support = half.x * n.x.abs() + half.z * n.z.abs();
            for i in 0..31 {
                let eps = (i as f32 - 15.0) * 2e-4;
                let drop = 0.15 * (1.0 + eps); // floor contact at t = drop / 0.5
                let start = Vec3::new(face.x, half.y + drop, face.z) + n * (support + 0.3);
                s.push_str(&sweeps::row(w, half, start, -n + Vec3::new(0.0, -0.5, 0.0)));
            }
        }
        for z in [4.5f32, 5.5, 6.5] {
            for d in [Vec3::new(1.0, 0.0, 0.0), Vec3::new(1.0, -0.05, 0.0), Vec3::new(1.0, 0.0, 0.3)] {
                s.push_str(&sweeps::row(w, half, Vec3::new(3.5 - half.x, half.y + 1e-4, z), d));
            }
        }
    }
}

pub fn scene_csv(w: &MeshWorld) -> String {
    let mut s = String::from("ax,ay,az,bx,by,bz,cx,cy,cz\n");
    for t in &w.tris {
        writeln!(s, "{},{},{}", f(t[0]), f(t[1]), f(t[2])).unwrap();
    }
    s
}

pub fn anchors() -> Vec<Vec3> {
    vec![
        Vec3::new(2.0, 0.0, 0.0),   // in front of the turned wall
        Vec3::new(3.5, 0.0, -2.2),  // by its far corner
        Vec3::new(-3.0, 0.0, 1.5),  // in front of the turned block
        Vec3::new(-3.0, 1.2, 3.0),  // on the block
        Vec3::new(3.6, 0.0, 5.5),   // at the foot of the ramp
        Vec3::new(6.0, 1.0, 5.5),   // on the ramp
        Vec3::new(-6.6, 0.0, 0.0),  // at the foot of the steep ramp
        Vec3::new(-4.8, 0.0, -4.6), // in front of the tilted panel
    ]
}

pub fn sweeps_csv(w: &MeshWorld) -> String {
    let mut s = String::from("hx,hy,hz,sx,sy,sz,dx,dy,dz,hit,t,nx,ny,nz,px,py,pz\n");
    let halves = [Vec3::new(0.3, 0.9, 0.3), Vec3::new(0.3, 0.61, 0.3), Vec3::splat(0.05)];
    let mut ds = slides::dirs8();
    ds.extend([Vec3::new(0.0, -1.0, 0.0), Vec3::new(1.0, -0.5, 0.0)]);
    for half in halves {
        for a in anchors() {
            let start = a + Vec3::Y * half.y; // standing on the ground
            for &d in &ds {
                for len in [0.5f32, 2.0] {
                    s.push_str(&sweeps::row(w, half, start, d * len));
                    s.push_str(&sweeps::row(w, half, start - Vec3::Y * 0.01, d * len)); // sunk 1 cm, inside SKIN
                }
            }
        }
    }
    edge_cases(w, &mut s);
    let mut r = Lcg(0x3E5A);
    for _ in 0..1500 {
        let half = Vec3::new(r.range(0.05, 0.4), r.range(0.05, 1.0), r.range(0.05, 0.4));
        let start = Vec3::new(r.range(-8.0, 8.0), half.y + r.range(0.0, 3.0), r.range(-8.0, 8.0));
        let delta = Vec3::new(r.axis(2.0), r.axis(2.0), r.axis(2.0));
        s.push_str(&sweeps::row(w, half, start, delta));
    }
    s
}

pub fn write_all(dir: &str) {
    let w = scene();
    let a = anchors();
    crate::common::write(dir, "mesh_scene.csv", scene_csv(&w));
    crate::common::write(dir, "mesh_sweeps.csv", sweeps_csv(&w));
    crate::common::write(dir, "mesh_slides.csv", slides::slides_csv(&w, &a, 0x3511));
    crate::common::write(dir, "mesh_trace_wall.csv", probes::trace_wall_csv(&w, &a, 0x3711));
    crate::common::write(dir, "mesh_find_ledge.csv", probes::find_ledge_csv(&w, &a, 0x3ED6));
}
