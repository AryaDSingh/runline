//! locomotion.rs: the speed and acceleration curves, and the ground acceleration and velocity
//! update run on random states, including dt = 0 and the tap-stop.
use crate::common::{f, Lcg};
use faith_move::locomotion::{Curve, Locomotion, RunInput, RunState, SPEED_CURVE};
use glam::Vec3;

/// kind 0 = an accel-curve point (x, y); 1 = speed curve eval; 2 = accel curve eval.
pub fn curves_csv() -> String {
    let loco = Locomotion::default();
    let speed = Curve(SPEED_CURVE.to_vec());
    let mut s = String::from("kind,x,y\n");
    for (x, y) in &loco.accel_curve.0 {
        s += &format!("0,{},{}\n", x, y);
    }
    for i in 0..=200 {
        let x = -1.0 + i as f32 * 0.05;
        s += &format!("1,{},{}\n", x, speed.eval(x));
        s += &format!("2,{},{}\n", x, loco.accel_curve.eval(x));
    }
    s
}

fn pick(r: &mut Lcg) -> f32 {
    let x = r.next();
    if x < 0.25 { 0.0 } else if x < 0.5 { 1.0 } else { r.range(-1.0, 1.0) }
}

fn rand_input(r: &mut Lcg) -> RunInput {
    let a = r.range(0.0, std::f32::consts::TAU);
    RunInput {
        forward: Vec3::new(-a.sin(), 0.0, -a.cos()),
        right: Vec3::new(a.cos(), 0.0, -a.sin()),
        a_forward: pick(r),
        a_strafe: pick(r),
        turn: if r.next() < 0.5 { 0.0 } else { r.range(-0.3, 0.3) },
    }
}

fn rand_state(r: &mut Lcg) -> RunState {
    let energy = if r.next() < 0.5 { 0.0 } else { r.range(0.0, 3.2) };
    let accel_time = match (r.next() * 3.0) as u32 { 0 => 0.0, 1 => r.range(0.0, 0.2), _ => r.range(0.0, 3.0) };
    let stopping = if r.next() < 0.8 { 0.0 } else { r.range(0.0, 0.25) };
    RunState { energy, accel_time, stopping }
}

fn rand_vel(r: &mut Lcg) -> Vec3 {
    if r.next() < 0.2 { Vec3::ZERO } else { Vec3::new(r.range(-8.0, 8.0), r.range(-3.0, 3.0), r.range(-8.0, 8.0)) }
}

fn rand_dt(r: &mut Lcg) -> f32 {
    [1.0 / 60.0, 1.0 / 120.0, 0.05, 0.0][(r.next() * 4.0) as usize % 4]
}

fn st3(st: &RunState) -> String { format!("{},{},{}", st.energy, st.accel_time, st.stopping) }

/// kind 0 = sprint_accel, 1 = walk_accel, 2 = controller_accel.
/// Columns: kind, state in (3), forward (3), right (3), a_forward, a_strafe, turn, vel (3), dt,
/// falling, then accel out (3), state out (3), vel out (3): 27 in all.
pub fn accel_csv() -> String {
    let loco = Locomotion::default();
    let mut r = Lcg(0x10C0);
    let mut s = String::from("kind,e,at,stop,fx,fy,fz,rx,ry,rz,af,as,turn,vx,vy,vz,dt,falling,ax,ay,az,e2,at2,stop2,vx2,vy2,vz2\n");
    for n in 0..3000 {
        let kind = n % 3;
        let (st0, i, vel0, dt) = (rand_state(&mut r), rand_input(&mut r), rand_vel(&mut r), rand_dt(&mut r));
        let falling = kind != 2 && r.next() < 0.3;
        let mut st = st0;
        let mut vel = vel0;
        let a = match kind {
            0 => loco.sprint_accel(&mut st, &i, vel, dt, falling),
            1 => loco.walk_accel(&mut st, &i, vel, dt, falling),
            _ => loco.controller_accel(&mut st, &i, &mut vel, dt),
        };
        s += &format!(
            "{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
            kind, st3(&st0), f(i.forward), f(i.right), i.a_forward, i.a_strafe, i.turn, f(vel0), dt, falling as u8,
            f(a), st3(&st), f(vel)
        );
    }
    s
}

/// Columns: vel (3), accel (3), dt, speed_mod, friction, then v out (3): 12 in all.
pub fn velocity_csv() -> String {
    let loco = Locomotion::default();
    let mut r = Lcg(0xCA1C);
    let mut s = String::from("vx,vy,vz,ax,ay,az,dt,mod,fric,ox,oy,oz\n");
    for _ in 0..2000 {
        let vel = rand_vel(&mut r);
        let accel = if r.next() < 0.4 { Vec3::ZERO } else { Vec3::new(r.range(-80.0, 80.0), r.range(-5.0, 5.0), r.range(-80.0, 80.0)) };
        let dt = rand_dt(&mut r);
        let speed_mod = [1.0, 0.2, 0.34][(r.next() * 3.0) as usize % 3];
        let friction = [loco.ground_friction, loco.ground_friction * 0.1, 0.0][(r.next() * 3.0) as usize % 3];
        let v = loco.calc_velocity(vel, accel, dt, speed_mod, friction);
        s += &format!("{},{},{},{},{},{}\n", f(vel), f(accel), dt, speed_mod, friction, f(v));
    }
    s
}
