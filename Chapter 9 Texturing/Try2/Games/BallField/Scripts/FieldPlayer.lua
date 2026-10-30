-- Limb entities are named after the body: "Player" -> "PlayerHead", "Player2" -> "Player2Head".
local CONTROLS = {
    wasd = { up = "W", down = "S", left = "A", right = "D" },
    arrows = { up = "Up", down = "Down", left = "Left", right = "Right" }
}

return {
    parameters = { speed = 5.0, runSpeed = 8.0, controls = "wasd", runKey = "Shift" },
    OnInit = function(self)
        self.time = 0
        self.moving = false
        self.keys = assert(CONTROLS[self.params.controls], "controls must be 'wasd' or 'arrows'")
        assert(Key[self.params.runKey], "unknown runKey " .. self.params.runKey)
        self.start = self.entity:GetPosition()
        self.startYaw = self.entity:GetRotation().y
        self.parts = {}
        local prefix = self.entity:GetName()
        for _, name in ipairs({"Head", "LeftArm", "RightArm", "LeftLeg", "RightLeg", "LeftEye", "RightEye"}) do
            self.parts[name] = assert(Entity.FindByName(prefix .. name))
        end
    end,
    OnFixedUpdate = function(self, dt)
        if self.frozen then
            self.entity:SetVelocity(Vec3(0, self.entity:GetVelocity().y, 0))
            return
        end
        local keys = self.keys
        local x, z = 0, 0
        if Input.IsKeyDown(Key[keys.up]) then z = z + 1 end
        if Input.IsKeyDown(Key[keys.down]) then z = z - 1 end
        if Input.IsKeyDown(Key[keys.left]) then x = x - 1 end
        if Input.IsKeyDown(Key[keys.right]) then x = x + 1 end
        local length = math.sqrt(x*x + z*z)
        self.moving = length > 0
        if self.moving then
            x, z = x/length, z/length
            self.entity:SetRotation(Vec3(0, math.atan(x, z), 0))
        end
        local speed = Input.IsKeyDown(Key[self.params.runKey]) and self.params.runSpeed or self.params.speed
        local velocity = self.entity:GetVelocity()
        self.entity:SetVelocity(Vec3(x * speed, velocity.y, z * speed))
    end,
    -- Sent by FootballMatch.lua: "Kickoff" returns to the start spot, "Freeze" stops input after a goal.
    OnEvent = function(self, name, payload)
        if name == "Kickoff" then
            self.frozen = false
            self.moving = false
            self.entity:SetPosition(self.start)
            self.entity:SetRotation(Vec3(0, self.startYaw, 0))
            self.entity:SetVelocity(Vec3())
        elseif name == "Freeze" then
            self.frozen = true
            self.moving = false
        end
    end,
    OnUpdate = function(self, dt)
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
