#include "pch.h"

#include "Engine/Audio/IAudioSystem.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/NullAudioSystem.h"

// NullAudioSystem 은 Windows 에서 쓰이지 않지만 **항상 컴파일한다.** 헤더 전용이라 비용이 없고,
// `#else` 안에만 두었을 때는 Windows 에서 한 번도 컴파일되지 않아 조용히 썩는다(FileWatcher 의
// macOS 구현이 그랬다).

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Audio/Windows/XAudio2System.h"
#endif

namespace sw
{
    unique_ptr<IAudioSystem> IAudioSystem::create()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<XAudio2System>();
#else
        return make_unique<NullAudioSystem>();
#endif
    }

    void IAudioSystem::setMasterVolume( float32 volume )
    {
        _masterVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        applyVolume();
    }

    void IAudioSystem::setMusicVolume( float32 volume )
    {
        _musicVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        applyVolume();
    }

    void IAudioSystem::setSfxVolume( float32 volume )
    {
        _sfxVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        applyVolume();
    }

    void IAudioSystem::setMute( bool bMute )
    {
        _bMuted = bMute;
        applyVolume();
    }
} // namespace sw
