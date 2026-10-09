//! Writes reference results from faith-runner's faith_move for RunlineCore's golden tests.
mod common;
mod locomotion;
mod mesh;
mod probes;
mod scalars;
mod slides;
mod sweeps;
mod tuning;
mod vault;

fn main() {
    let dir = common::golden_dir();
    std::fs::create_dir_all(&dir).unwrap();
    let boxes = sweeps::scene();
    common::write(&dir, "scene.csv", sweeps::scene_csv());
    common::write(&dir, "sweeps.csv", sweeps::sweeps_csv(&boxes));
    common::write(&dir, "scalars.csv", scalars::csv());
    let anchors = sweeps::anchors();
    common::write(&dir, "slides.csv", slides::slides_csv(&boxes, &anchors, 0x511D));
    common::write(&dir, "move_axis.csv", slides::move_axis_csv(&boxes, &anchors, 0xA815));
    common::write(&dir, "probe_wall.csv", probes::probe_wall_csv(&boxes, &anchors, 0x9B0E));
    common::write(&dir, "trace_wall.csv", probes::trace_wall_csv(&boxes, &anchors, 0x7A11));
    common::write(&dir, "trace_box.csv", probes::trace_box_csv(&boxes, &anchors, 0x7B0C));
    common::write(&dir, "tops_below.csv", probes::tops_below_csv(&boxes, &anchors));
    common::write(&dir, "find_ledge.csv", probes::find_ledge_csv(&boxes, &anchors, 0x1ED6));
    mesh::write_all(&dir);
    common::write(&dir, "curves.csv", locomotion::curves_csv());
    common::write(&dir, "loco_accel.csv", locomotion::accel_csv());
    common::write(&dir, "loco_velocity.csv", locomotion::velocity_csv());
    common::write(&dir, "tuning.txt", tuning::txt());
    common::write(&dir, "vault_types.csv", vault::types_csv());
    common::write_reference(&dir);
}
