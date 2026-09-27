// Compiled into the config oracle library only, with `cameraunlock` and
// `DishonoredHeadTracking` renamed, so "config.h" here is the published build's
// (oracle/src/config.h).
#include "config.h"
#include "oracle_adapter.h"

namespace dishonored_oracle_view {

OracleResult RunOracle(const std::string& path) {
    DishonoredHeadTracking::Config c;
    const bool loaded = c.LoadOrCreate(path.c_str());
    OracleConfig o{};
    o.enabled_on_startup = c.enabled_on_startup;
    o.udp_port = c.udp_port;
    o.sens_yaw = c.sens_yaw;
    o.sens_pitch = c.sens_pitch;
    o.sens_roll = c.sens_roll;
    o.invert_yaw = c.invert_yaw;
    o.invert_pitch = c.invert_pitch;
    o.invert_roll = c.invert_roll;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.move_crosshair = c.move_crosshair;
    o.world_space_yaw = c.world_space_yaw;
    o.fov = c.fov;
    o.position_enabled = c.position_enabled;
    o.pos_sens_x = c.pos_sens_x;
    o.pos_sens_y = c.pos_sens_y;
    o.pos_sens_z = c.pos_sens_z;
    o.pos_limit_x = c.pos_limit_x;
    o.pos_limit_y = c.pos_limit_y;
    o.pos_limit_z = c.pos_limit_z;
    o.pos_limit_z_back = c.pos_limit_z_back;
    o.invert_pos_x = c.invert_pos_x;
    o.invert_pos_y = c.invert_pos_y;
    o.invert_pos_z = c.invert_pos_z;
    o.position_scale = c.position_scale;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.vk_toggle = c.vk_toggle;
    o.vk_cycle_mode = c.vk_cycle_mode;
    o.vk_yaw_mode = c.vk_yaw_mode;
    o.chord_toggle = c.chord_toggle;
    o.chord_cycle_mode = c.chord_cycle_mode;
    o.chord_yaw_mode = c.chord_yaw_mode;
    return {loaded, o};
}

}
