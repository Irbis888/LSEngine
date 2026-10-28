return {
    parameters = { message = "Deferred deletion requested" },
    OnUpdate = function(self, dt)
        if Input.IsKeyPressed(Key.X) then
            Log(self.params.message)
            self.entity:Destroy()
        end
    end,
    OnDestroy = function(self) Log(self.entity:GetName(), "OnDestroy while entity is still valid:", self.entity:IsValid()) end
}
