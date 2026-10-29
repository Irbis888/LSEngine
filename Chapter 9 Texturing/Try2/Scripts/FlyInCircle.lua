local function updatePosition(self)
    -- A horizontal circle in the XZ plane; center.y is the flight altitude.
    self.entity:SetPosition(self.params.center + Vec3(
        self.params.radius * math.cos(self.angle),
        0,
        self.params.radius * math.sin(self.angle)
    ))
end

return {
    parameters = {
        center = Vec3(0, 4, 2),
        radius = 3.5,
        angularSpeed = 0.9 -- radians per second
    },
    OnInit = function(self)
        assert(self.params.radius >= 0, "Orbit radius must be nonnegative")
        self.angle = 0
        updatePosition(self)
    end,
    OnUpdate = function(self, dt)
        self.angle = (self.angle + self.params.angularSpeed * dt) % (2 * math.pi)
        updatePosition(self)
    end
}
