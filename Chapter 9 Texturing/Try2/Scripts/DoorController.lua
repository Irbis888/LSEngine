return {
    parameters = { targetName = "Door", speed = 2.0 },
    OnUpdate = function(self, dt)
        if Input.IsKeyPressed(Key.O) then
            local target = Entity.FindByName(self.params.targetName)
            if target and target:IsValid() then
                target:SendEvent("Open", { speed = self.params.speed })
                Log("Open event queued for", self.params.targetName)
            end
        end
    end
}
