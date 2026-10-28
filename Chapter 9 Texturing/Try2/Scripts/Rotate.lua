-- Every entity executes this file in its own environment and receives a fresh table.
local fileLocalTicks = 0
return {
    parameters = { speed = 1.0, axis = Vec3(0, 1, 0), label = "spinner" },
    OnInit = function(self)
        self.elapsed = 0
        Log(self.entity:GetName(), "initialized", self.params.label)
    end,
    OnUpdate = function(self, dt)
        fileLocalTicks = fileLocalTicks + 1
        self.elapsed = self.elapsed + dt
        self.entity:SetRotation(self.entity:GetRotation() + self.params.axis * (self.params.speed * dt))
    end,
    OnDestroy = function(self)
        Log(self.entity:GetName(), "destroyed, private ticks", fileLocalTicks)
    end
}
