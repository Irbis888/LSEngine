-- One confetti piece, spawned from Games/BallField/Prefabs/Confetti*.json by FootballMatch.lua.
-- Flies out of the goal, falls, bounces on the grass and removes itself. No rigidbody:
-- the pieces never push the ball or the players.
return {
    parameters = { lifetime = 1.5, speed = 6.0, gravity = 14.0 },
    OnInit = function(self)
        local angle = math.random() * 2 * math.pi
        local direction = Vec3(math.cos(angle), 0.7 + math.random() * 0.8, math.sin(angle))
        self.velocity = direction * (self.params.speed * (0.5 + math.random() * 0.5))
        self.spin = Vec3(math.random() * 12 - 6, math.random() * 12 - 6, math.random() * 12 - 6)
        self.age = 0
    end,
    OnUpdate = function(self, dt)
        self.age = self.age + dt
        if self.age >= self.params.lifetime then
            self.entity:Destroy()
            return
        end
        self.velocity.y = self.velocity.y - self.params.gravity * dt
        local p = self.entity:GetPosition() + self.velocity * dt
        if p.y < 0.03 then
            p.y = 0.03
            self.velocity = Vec3(self.velocity.x * 0.5, -self.velocity.y * 0.35, self.velocity.z * 0.5)
        end
        self.entity:SetPosition(p)
        self.entity:SetRotation(self.entity:GetRotation() + self.spin * dt)
    end
}
