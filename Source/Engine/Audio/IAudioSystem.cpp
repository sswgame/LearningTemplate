#include "pch.h"
#include "Engine/Audio/IAudioSystem.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Audio/XAudio2System.h"
#else
	#include "Engine/Audio/NullAudioSystem.h"
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
} // namespace sw
