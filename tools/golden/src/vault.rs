//! vault.rs's VAULT_TYPES table, every number in it, so the C++ copy is checked bit for bit.
use faith_move::vault::VAULT_TYPES;

pub fn types_csv() -> String {
    let mut s = String::from("onto,min_h,max_h,min_vz,max_vz,max_mom,max_dist_t,clamp_min,clamp_max,add,t_up,t_over,t_down,hand_along,hand_up,ledge_along,ledge_up\n");
    for t in VAULT_TYPES.iter() {
        s += &format!(
            "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
            t.onto as u8, t.min_height, t.max_height, t.min_speed_z, t.max_speed_z, t.max_momentum, t.max_distance_time,
            t.clamp_speed_min, t.clamp_speed_max, t.speed_addition, t.time_up, t.time_over, t.time_down,
            t.handplant.0, t.handplant.1, t.ledge.0, t.ledge.1
        );
    }
    s
}
