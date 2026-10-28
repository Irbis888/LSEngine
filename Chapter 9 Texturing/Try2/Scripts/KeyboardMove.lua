return {
    parameters = { speed = 3.0, enabledMovement = true },
    OnUpdate = function(self, dt)
        if not self.params.enabledMovement then return end
        local movement = Vec3()
        if Input.IsKeyDown(Key.Left) then movement.x = movement.x - 1 end
        if Input.IsKeyDown(Key.Right) then movement.x = movement.x + 1 end
        if Input.IsKeyDown(Key.Up) then movement.z = movement.z + 1 end
        if Input.IsKeyDown(Key.Down) then movement.z = movement.z - 1 end
        self.entity:SetPosition(self.entity:GetPosition() + movement * (self.params.speed * dt))
    end
}
