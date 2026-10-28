return {
    parameters = { speed = 3.0, jumpImpulse = 5.0 },
    OnInit = function(self) self.jumpRequested = false end,
    OnUpdate = function(self, dt)
        -- Fixed steps precede Update in this engine. Intent applies on the next
        -- actual fixed step and is consumed once, even with several substeps.
        if Input.IsKeyPressed(Key.Space) then self.jumpRequested = true end
    end,
    OnFixedUpdate = function(self, dt)
        if not self.entity:HasRigidbody() then return end
        local velocity = self.entity:GetVelocity()
        velocity.x = 0
        if Input.IsKeyDown(Key.J) then velocity.x = -self.params.speed end
        if Input.IsKeyDown(Key.L) then velocity.x = self.params.speed end
        self.entity:SetVelocity(velocity)
        if self.jumpRequested then
            self.entity:ApplyImpulse(Vec3(0, self.params.jumpImpulse, 0))
            self.jumpRequested = false
        end
    end
}
