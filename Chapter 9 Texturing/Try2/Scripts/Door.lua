return {
    parameters = { height = 3.0, defaultSpeed = 2.0 },
    OnInit = function(self)
        self.startPosition = self.entity:GetPosition()
        self.offset = 0
        self.open = false
        self.speed = self.params.defaultSpeed
    end,
    OnEvent = function(self, name, payload, sender)
        if name == "Open" then
            self.open = true
            self.speed = payload.speed or self.params.defaultSpeed
            Log("Door received Open; sender valid:", sender:IsValid())
        end
    end,
    OnUpdate = function(self, dt)
        if self.open then
            self.offset = math.min(self.offset + self.speed * dt, self.params.height)
            self.entity:SetPosition(self.startPosition + Vec3(0, self.offset, 0))
        end
    end
}
