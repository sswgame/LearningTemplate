#include "pch.h"

#include "GameFramework/Framework/GameSound.h"

#include "Engine/Audio/IAudioSystem.h"

#include "GameFramework/Framework/GameService.h"

namespace sw
{
    bool GameSound::play( string_view path )
    {
        IAudioSystem* pAudio = game::getService<IAudioSystem>();
        return pAudio != nullptr && pAudio->play( path );
    }

    bool GameSound::playMusic( string_view path )
    {
        IAudioSystem* pAudio = game::getService<IAudioSystem>();
        return pAudio != nullptr && pAudio->playMusic( path );
    }
} // namespace sw
