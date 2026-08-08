local music = {}

local audio = nil

function music.Tick(self, entity)
    if not audio then
        audio = entity:GetComponent(ComponentType.AudioSource)
    end

    if audio and not audio:IsPlaying() then
        audio:PlayClip()
    end
end

return music
