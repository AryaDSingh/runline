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
    MeshWorld::new(t, vec![])
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
