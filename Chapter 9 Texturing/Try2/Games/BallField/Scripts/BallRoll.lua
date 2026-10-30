return {
    parameters = { radius = 0.65 },
    OnInit = function(self) self.previous = self.entity:GetPosition(); self.roll = Vec3() end,
    OnUpdate = function(self, dt)
        local p = self.entity:GetPosition()
        local delta = p - self.previous
        self.previous = p
        -- Visual rolling follows physical translation. The current solver has
        -- linear impulses, not angular momentum; do not feed this into physics.
        if math.abs(delta.x) + math.abs(delta.z) < 3 then
            self.roll.x = self.roll.x + delta.z / self.params.radius
            self.roll.z = self.roll.z - delta.x / self.params.radius
        else self.roll = Vec3() end
        self.entity:SetRotation(self.roll)
        local spot = Entity.FindByName("BallSpot")
        if spot then
            spot:SetPosition(p + Vec3(math.sin(self.roll.z)*0.63, math.cos(self.roll.z)*math.cos(self.roll.x)*0.63,
                math.cos(self.roll.z)*math.sin(self.roll.x)*0.63))
        end
    end
}
