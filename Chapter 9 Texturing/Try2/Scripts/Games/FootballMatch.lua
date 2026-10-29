-- Goals, score and kickoff for BallField. Blue ("Player") defends the west goal (-X),
-- red ("Player2") defends the east goal (+X). The scoreboard is two seven-segment digits:
-- each segment is a scene entity, shown at its authored position or hidden under the field.
local SEGMENTS = { "A", "B", "C", "D", "E", "F", "G" }
local DIGITS = {
    [0] = "ABCDEF", [1] = "BC", [2] = "ABDEG", [3] = "ABCDG", [4] = "BCFG",
    [5] = "ACDFG", [6] = "ACDEFG", [7] = "ABC", [8] = "ABCDEFG", [9] = "ABCDFG"
}

local function ShowDigit(segments, value)
    local lit = DIGITS[math.min(value, 9)]
    for name, segment in pairs(segments) do
        local p = segment.home
        if not string.find(lit, name, 1, true) then p = Vec3(p.x, -5, p.z) end
        segment.entity:SetPosition(p)
    end
end

return {
    parameters = {
        goalLine = 12.0,      -- inner face of the end borders
        goalHalfWidth = 2.5,  -- goal mouth spans z in [-goalHalfWidth, goalHalfWidth]
        ballRadius = 0.65,    -- the ball counts once it is fully over the line
        winScore = 5,
        goalPause = 1.5,      -- seconds the ball stays in the net before kickoff
        winPause = 4.0
    },
    OnInit = function(self)
        self.ball = assert(Entity.FindByName("Ball"))
        self.players = { assert(Entity.FindByName("Player")), assert(Entity.FindByName("Player2")) }
        self.score = { blue = 0, red = 0 }
        self.segments = { blue = {}, red = {} }
        for team, prefix in pairs({ blue = "ScoreBlue", red = "ScoreRed" }) do
            for _, name in ipairs(SEGMENTS) do
                local entity = assert(Entity.FindByName(prefix .. name))
                self.segments[team][name] = { entity = entity, home = entity:GetPosition() }
            end
        end
        self.winScore = math.max(1, math.min(9, math.floor(self.params.winScore)))
        self.pause = 0
        self:UpdateBoard()
        Log("Match: first to " .. self.winScore .. " goals. Blue: WASD + Shift, Red: arrows + Enter")
    end,
    UpdateBoard = function(self)
        ShowDigit(self.segments.blue, self.score.blue)
        ShowDigit(self.segments.red, self.score.red)
    end,
    Kickoff = function(self)
        self.ball:SetPosition(Vec3(0, 0.66, 0))
        self.ball:SetVelocity(Vec3())
        for _, player in ipairs(self.players) do player:SendEvent("Kickoff") end
    end,
    Goal = function(self, team)
        self.score[team] = self.score[team] + 1
        self:UpdateBoard()
        for _, player in ipairs(self.players) do player:SendEvent("Freeze") end
        Log(string.format("GOAL for %s! Blue %d : %d Red", team, self.score.blue, self.score.red))
        if self.score[team] >= self.winScore then
            self.winner = team
            self.pause = self.params.winPause
            Log(team .. " wins the match!")
        else
            self.pause = self.params.goalPause
        end
    end,
    OnUpdate = function(self, dt)
        if self.pause > 0 then
            self.pause = self.pause - dt
            if self.pause <= 0 then
                if self.winner then
                    self.winner = nil
                    self.score = { blue = 0, red = 0 }
                    self:UpdateBoard()
                    Log("New match")
                end
                self:Kickoff()
            end
            return
        end
        if Input.IsKeyPressed(Key.R) then self:Kickoff() return end
        local p = self.ball:GetPosition()
        if math.abs(p.z) < self.params.goalHalfWidth then
            local line = self.params.goalLine + self.params.ballRadius
            if p.x > line then self:Goal("blue")
            elseif p.x < -line then self:Goal("red") end
        end
    end
}
