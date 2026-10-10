//! look.rs: the per-move look limits, the LookLimiter's easing, and the swan neck.
//! One CSV, `kind,code,param,frame,a,b,c,d`:
//!   kind 0 (limit):   a = pitch min, b = pitch max, c = has yaw limit, d = yaw half-range
//!   kind 1 (limiter): one 90-frame run (param 1: look far down-left; 2: far up-right; 3: the
//!                     state changes at frame 30); a = pitch, b = yaw after the frame, c = the
//!                     state's own param
//!   kind 2 (swan):    a = forward, b = down after the frame, c = the pitch it saw
//! `code` names the state (see `state`). Coiled-in-air isn't here: `coiled` is private.
use faith_move::look::{look_limit, LookLimiter, SwanNeck};
use faith_move::{Controller, State, Traverse, TraverseKind, Tuning};
use glam::Vec3;

const DT: f32 = 1.0 / 60.0;

/// A controller in the state `code` (with `param` where the state has a time).
fn state(code: u32, param: f32) -> Controller {
    let mut c = Controller::new(Tuning::default(), Vec3::ZERO, 0.0);
    let tr = |kind| State::Traverse(Traverse { kind, from: Vec3::ZERO, to: Vec3::Y, t: param, dur: 0.5, arc: 0.0, exit_vel: Vec3::ZERO });
    c.state = match code {
        0 => State::Ground,
        1 => {
            c.crouched = true;
            State::Ground
        }
        2 => State::Air,
        3 => {
            c.turned_in_air = true;
            State::Air
        }
        4 => State::Slide { t: param },
        5 => State::Roll { t: param },
        6 => State::Stunned { t: param },
        7 => State::WallRun { normal: Vec3::X, t: param },
        8 => State::WallClimb { normal: Vec3::Z, t: param },
        9 => State::WallClimbTurned { normal: Vec3::Z, t: param },
        10 => State::LedgeHang { normal: Vec3::Z, ledge_y: 1.8, turned: false },
        11 => State::LedgeHang { normal: Vec3::Z, ledge_y: 1.8, turned: true },
        12 => State::GrabTransfer { t: param, from: Vec3::ZERO, to: Vec3::Y, dur: 0.5, normal: Vec3::Z, ledge_y: 1.8 },
        13 => tr(TraverseKind::Vault),
        14 => tr(TraverseKind::Mantle),
        15 => tr(TraverseKind::PullUp),
        16 => tr(TraverseKind::SpringBoard),
        17 => State::LayOnGround { t: param, getting_up: None, back_roll: false },
        _ => unreachable!(),
    };
    c
}

const CODES: [(u32, f32); 18] = [
    (0, 0.0), (1, 0.0), (2, 0.0), (3, 0.0), (4, 0.1), (5, 0.1), (6, 0.1), (7, 0.1), (8, 0.1), (9, 0.1),
    (10, 0.0), (11, 0.0), (12, 0.1), (13, 0.1), (14, 0.1), (15, 0.3), (16, 0.1), (17, 0.1),
];

fn row(s: &mut String, kind: u32, code: u32, param: f32, frame: usize, v: [f32; 4]) {
    *s += &format!("{kind},{code},{param},{frame},{},{},{},{}\n", v[0], v[1], v[2], v[3]);
}

pub fn csv() -> String {
    let mut s = String::from("kind,code,param,frame,a,b,c,d\n");
    for (code, param) in CODES {
        let c = state(code, param);
        let l = look_limit(&c);
        row(&mut s, 0, code, param, 0, [l.pitch.0, l.pitch.1, l.yaw.is_some() as u32 as f32, l.yaw.unwrap_or(0.0)]);
    }
    for (code, param) in CODES {
        for run in [1u32, 2, 3] {
            let mut c = state(code, param);
            c.yaw = 0.3;
            c.slide_yaw = -0.4;
            let mut l = LookLimiter::default();
            l.apply(&mut c, DT);
            for frame in 1..=90usize {
                if frame == 1 {
                    let (dy, p) = if run == 2 { (-2.5, 1.4) } else { (2.5, -1.4) };
                    c.yaw += dy;
                    c.pitch = p;
                }
                if run == 3 && frame == 30 {
                    // A new move: the limiter re-anchors its yaw and eases in again.
                    c.state = State::LedgeHang { normal: Vec3::Z, ledge_y: 1.8, turned: false };
                    c.pitch = -1.3;
                }
                l.apply(&mut c, DT);
                row(&mut s, 1, code, run as f32, frame, [c.pitch, c.yaw, param, 0.0]);
            }
        }
    }
    for (code, param) in [(0, 0.0), (10, 0.0), (5, 0.1), (5, 0.3), (3, 0.0), (15, 0.3), (17, 0.1), (7, 0.1)] {
        let mut c = state(code, param);
        let mut swan = SwanNeck::default();
        let mut frame = 0;
        for pitch in [-0.05f32, -0.3, -0.8, -1.2, -1.45, 0.4, -1.5] {
            c.pitch = pitch;
            for _ in 0..30 {
                let (f, d) = swan.update(&c, DT);
                row(&mut s, 2, code, param, frame, [f, d, pitch, 0.0]);
                frame += 1;
            }
        }
    }
    s
}
