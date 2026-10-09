//! The wall, ledge and ground probes every move builds on.
use crate::common::{f, Lcg};
use crate::slides::{dirs8, BODIES};
use faith_move::world::{find_ledge, probe_wall, tops_below, trace_box, trace_wall, Body, World};
use glam::Vec3;

fn rand_feet(r: &mut Lcg) -> Vec3 {
    let y = if r.next() < 0.7 { 0.0 } else { r.range(0.0, 3.0) };
    Vec3::new(r.range(-6.0, 6.0), y, r.range(-6.0, 6.0))
}
fn rand_dir(r: &mut Lcg) -> Vec3 {
    let a = r.range(0.0, std::f32::consts::TAU);
    Vec3::new(a.cos(), 0.0, a.sin())
}
fn rand_body(r: &mut Lcg) -> Body { BODIES[(r.next() * 2.0) as usize % 2] }

fn probe_wall_row(w: &dyn World, body: Body, feet: Vec3, dir: Vec3, reach: f32, lo: f32, hi: f32) -> String {
    let (found, n, gap) = match probe_wall(w, body, feet, dir, reach, lo, hi) {
        Some((n, gap)) => (1, n, gap),
        None => (0, Vec3::ZERO, 0.0),
    };
    format!("{},{},{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), f(dir), reach, lo, hi, found, f(n), gap)
}

pub fn probe_wall_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,dx,dy,dz,reach,ylo,yhi,found,nx,ny,nz,gap\n");
    let mut ds = dirs8();
    ds.push(Vec3::new(1.0, 0.5, 0.0));
    ds.push(Vec3::ZERO);
    for body in BODIES {
        for &a in anchors {
            for &d in &ds {
                for reach in [0.5f32, 2.0] {
                    for (lo, hi) in [(0.0f32, 1.8f32), (1.0, 1.6), (0.3, 0.6)] {
                        s.push_str(&probe_wall_row(w, body, a, d, reach, lo, hi));
                    }
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let (body, feet, d) = (rand_body(&mut r), rand_feet(&mut r), rand_dir(&mut r));
        let reach = r.range(0.1, 3.0);
        let lo = r.range(0.0, 1.0);
        let hi = lo + r.range(0.1, 1.0);
        s.push_str(&probe_wall_row(w, body, feet, d, reach, lo, hi));
    }
    s
}

fn trace_wall_row(w: &dyn World, body: Body, feet: Vec3, dir: Vec3, reach: f32, y: f32) -> String {
    let (found, n, at, gap) = match trace_wall(w, body, feet, dir, reach, y) {
        Some(h) => (1, h.n, h.at, h.gap),
        None => (0, Vec3::ZERO, Vec3::ZERO, 0.0),
    };
    format!("{},{},{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), f(dir), reach, y, found, f(n), f(at), gap)
}

pub fn trace_wall_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,dx,dy,dz,reach,y,found,nx,ny,nz,ax,ay,az,gap\n");
    let mut ds = dirs8();
    ds.push(Vec3::ZERO);
    for body in BODIES {
        for &a in anchors {
            for &d in &ds {
                for reach in [0.0f32, 0.5, 2.0] {
                    for y in [0.3f32, 1.0, 1.7] {
                        s.push_str(&trace_wall_row(w, body, a, d, reach, y));
                    }
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let (body, feet, d) = (rand_body(&mut r), rand_feet(&mut r), rand_dir(&mut r));
        let reach = r.range(0.1, 3.0);
        let y = r.range(0.0, 1.8);
        s.push_str(&trace_wall_row(w, body, feet, d, reach, y));
    }
    s
}

fn trace_box_row(w: &dyn World, start: Vec3, dir: Vec3, reach: f32, half: Vec3) -> String {
    let (found, at, n) = match trace_box(w, start, dir, reach, half) {
        Some((at, n)) => (1, at, n),
        None => (0, Vec3::ZERO, Vec3::ZERO),
    };
    format!("{},{},{},{},{},{},{}\n", f(start), f(dir), reach, f(half), found, f(at), f(n))
}

pub fn trace_box_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("sx,sy,sz,dx,dy,dz,reach,hx,hy,hz,found,px,py,pz,nx,ny,nz\n");
    let mut ds = dirs8();
    ds.extend([Vec3::new(0.0, -1.0, 0.0), Vec3::new(0.0, 1.0, 0.0), Vec3::ZERO]);
    for &a in anchors {
        for &d in &ds {
            for reach in [0.0f32, 1.0, 4.0] {
                for half in [Vec3::splat(0.05), Vec3::new(0.3, 0.02, 0.3)] {
                    s.push_str(&trace_box_row(w, a + Vec3::Y, d, reach, half));
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1000 {
        let start = rand_feet(&mut r) + Vec3::Y * r.range(0.0, 2.0);
        let dir = Vec3::new(r.range(-1.0, 1.0), r.range(-1.0, 1.0), r.range(-1.0, 1.0));
        let reach = r.range(0.0, 5.0);
        let half = Vec3::splat(r.range(0.01, 0.3));
        s.push_str(&trace_box_row(w, start, dir, reach, half));
    }
    s
}

fn tops_row(w: &dyn World, p: Vec3, hxz: f32, y_hi: f32, y_lo: f32) -> String {
    let ts = tops_below(w, p, hxz, y_hi, y_lo);
    let mut s = format!("{},{},{},{},{}", f(p), hxz, y_hi, y_lo, ts.len());
    for i in 0..8 {
        s.push_str(&format!(",{}", ts.get(i).copied().unwrap_or(0.0)));
    }
    s + "\n"
}

/// Every anchor's column and a 1 m grid over the scene, two widths, three height ranges
/// (the last one empty: y_lo above y_hi).
pub fn tops_below_csv(w: &dyn World, anchors: &[Vec3]) -> String {
    let mut s = String::from("px,py,pz,hxz,yhi,ylo,count,t1,t2,t3,t4,t5,t6,t7,t8\n");
    let mut pts: Vec<Vec3> = anchors.iter().map(|a| Vec3::new(a.x, 0.0, a.z)).collect();
    for i in -6..=6 {
        for j in -6..=6 {
            pts.push(Vec3::new(i as f32, 0.0, j as f32));
        }
    }
    for p in pts {
        for hxz in [0.01f32, 0.3] {
            for (hi, lo) in [(7.0f32, -1.0f32), (2.0, 0.5), (0.5, 2.0)] {
                s.push_str(&tops_row(w, p, hxz, hi, lo));
            }
        }
    }
    s
}

fn ledge_row(w: &dyn World, body: Body, feet: Vec3, n: Vec3, lo: f32, hi: f32, crouch: f32) -> String {
    let (found, top) = match find_ledge(w, body, feet, n, lo, hi, crouch) {
        Some(t) => (1, t),
        None => (0, 0.0),
    };
    format!("{},{},{},{},{},{},{},{},{}\n", body.half_width, body.height, f(feet), f(n), lo, hi, crouch, found, top)
}

pub fn find_ledge_csv(w: &dyn World, anchors: &[Vec3], seed: u64) -> String {
    let mut s = String::from("hw,h,fx,fy,fz,nx,ny,nz,lo,hi,crouch,found,top\n");
    let normals = [Vec3::X, -Vec3::X, Vec3::Z, -Vec3::Z, Vec3::new(1.0, 0.0, 1.0), Vec3::new(-1.0, 0.0, -1.0), Vec3::ZERO];
    for body in BODIES {
        for &a in anchors {
            for n in normals {
                for (lo, hi) in [(0.5f32, 2.5f32), (1.0, 1.6), (0.0, 6.5)] {
                    s.push_str(&ledge_row(w, body, a, n, lo, hi, 1.22));
                }
            }
        }
    }
    let mut r = Lcg(seed);
    for _ in 0..1500 {
        let (body, feet, n) = (rand_body(&mut r), rand_feet(&mut r), rand_dir(&mut r));
        let lo = r.range(0.0, 2.0);
        let hi = lo + r.range(0.2, 4.0);
        s.push_str(&ledge_row(w, body, feet, n, lo, hi, 1.22));
    }
    s
}
