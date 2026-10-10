//! faith-runner's two courses (greybox and rooftops) as plain data for Unreal's ARunlineCourse,
//! written to Unreal/Runline/Data/Levels/<name>.csv. Core metres, Y up; one record per line:
//!
//! ```text
//! level,<name>
//! solid,<look>,minx,miny,minz,maxx,maxy,maxz      collides
//! decor,<look>,minx,miny,minz,maxx,maxy,maxz      drawn only
//! checkpoint,<name>,minx,miny,minz,maxx,maxy,maxz,spawnx,spawny,spawnz,yaw
//! finish,minx,miny,minz,maxx,maxy,maxz
//! skipped_fixtures,<count>                         ziplines, poles, beams: ported in M4
//! ```
use crate::common::{f, write};
use faith_move::greybox::Level;

fn csv(l: &Level) -> String {
    let mut s = format!("level,{}\n", l.name);
    for (b, look) in &l.solids {
        s += &format!("solid,{:?},{},{}\n", look, f(b.min), f(b.max));
    }
    for (b, look) in &l.decor {
        s += &format!("decor,{:?},{},{}\n", look, f(b.min), f(b.max));
    }
    for c in &l.checkpoints {
        s += &format!("checkpoint,{},{},{},{},{}\n", c.name, f(c.region.min), f(c.region.max), f(c.spawn), c.yaw);
    }
    if let Some(b) = l.finish {
        s += &format!("finish,{},{}\n", f(b.min), f(b.max));
    }
    s += &format!("skipped_fixtures,{}\n", l.fixtures.len());
    s
}

pub fn write_all() {
    let dir = concat!(env!("CARGO_MANIFEST_DIR"), "/../../Unreal/Runline/Data/Levels");
    std::fs::create_dir_all(dir).unwrap();
    write(dir, "greybox.csv", csv(&faith_move::greybox::greybox()));
    write(dir, "rooftops.csv", csv(&faith_move::rooftops::rooftops()));
}
