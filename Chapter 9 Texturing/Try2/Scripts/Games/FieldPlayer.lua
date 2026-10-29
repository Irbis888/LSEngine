return {
    parameters = { speed = 5.0, runSpeed = 8.0 },
    OnInit = function(self)
        self.time = 0
        self.moving = false
        self.parts = {}
        for _, name in ipairs({"Head", "LeftArm", "RightArm", "LeftLeg", "RightLeg", "LeftEye", "RightEye"}) do
            self.parts[name] = assert(Entity.FindByName("Player" .. name))
        end
    end,
    OnFixedUpdate = function(self, dt)
        local x, z = 0, 0
        if Input.IsKeyDown(Key.W) or Input.IsKeyDown(Key.Up) then z = z + 1 end
        if Input.IsKeyDown(Key.S) or Input.IsKeyDown(Key.Down) then z = z - 1 end
        if Input.IsKeyDown(Key.A) or Input.IsKeyDown(Key.Left) then x = x - 1 end
        if Input.IsKeyDown(Key.D) or Input.IsKeyDown(Key.Right) then x = x + 1 end
        local length = math.sqrt(x*x + z*z)
        self.moving = length > 0
        if self.moving then
            x, z = x/length, z/length
            self.entity:SetRotation(Vec3(0, math.atan(x, z), 0))
        end
        local speed = Input.IsKeyDown(Key.Shift) and self.params.runSpeed or self.params.speed
        local velocity = self.entity:GetVelocity()
        self.entity:SetVelocity(Vec3(x * speed, velocity.y, z * speed))
    end,
    OnUpdate = function(self, dt)
        if Input.IsKeyPressed(Key.R) then
            self.entity:SetPosition(Vec3(-4, 0.91, 0))
            self.entity:SetVelocity(Vec3())
            local ball = Entity.FindByName("Ball")
            if ball then ball:SetPosition(Vec3(0, 0.66, 0)); ball:SetVelocity(Vec3()) end
        end
        self.time = self.time + dt
        local p = self.entity:GetPosition()
        local yaw = self.entity:GetRotation().y
        local c, s = math.cos(yaw), math.sin(yaw)
        local swing = self.moving and math.sin(self.time * 12) * 0.35 or 0
        local function part(name, x, y, z, tilt)
            local entity = self.parts[name]
            entity:SetPosition(p + Vec3(x*c + z*s, y, -x*s + z*c))
            entity:SetRotation(Vec3(tilt or 0, yaw, 0))
        end
        part("Head", 0, 0.75, 0, 0)
        part("LeftEye", -0.10, 0.78, 0.257, 0)
        part("RightEye", 0.10, 0.78, 0.257, 0)
        part("LeftArm", -0.55, -0.02, 0, swing)
        part("RightArm", 0.55, -0.02, 0, -swing)
        part("LeftLeg", -0.22, -0.6, 0, -swing)
        part("RightLeg", 0.22, -0.6, 0, swing)
    end
}
