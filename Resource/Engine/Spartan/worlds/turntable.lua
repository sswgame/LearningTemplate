local turntable = {}

-- y axis spin rate in radians per second
local rotation_speed_rad = 0.35

function turntable.Tick(self, entity)
    local dt         = Timer.GetDeltaTimeSec()
    local half_angle = rotation_speed_rad * dt * 0.5

    -- lua quaternion binding only exposes x y z w so we build a y axis rotation by hand
    local q = Quaternion()
    q.x = 0.0
    q.y = math.sin(half_angle)
    q.z = 0.0
    q.w = math.cos(half_angle)

    entity:Rotate(q)
end

return turntable
