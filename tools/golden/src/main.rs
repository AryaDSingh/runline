//! Writes reference results from faith-runner's faith_move for RunlineCore's golden tests.
mod common;
mod scalars;
mod slides;
mod sweeps;

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
    common::write_reference(&dir);
}
