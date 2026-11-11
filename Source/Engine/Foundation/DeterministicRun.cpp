#include "pch.h"

#include "Engine/Foundation/DeterministicRun.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    DeterministicRunPolicy::DeterministicRunPolicy()
        : _bFinishAssetJobsEachFrame{ SW_FALSE }
        , _bWaitForAnimationClips{ SW_FALSE }
        , _bFreezeEnvironmentTime{ SW_FALSE }
        , _fixedFrameDelta{ 0.0f }
    {
    }

    DeterministicRun::DeterministicRun()
        : _policy{}
    {
    }
} // namespace sw

namespace sw
{
    // 결정적 실행 정책 — 자동화 · 개발 스위치가 채우고 런타임(렌더 · 에셋 · 애니메이션 · App 프레임 시간)이 읽는다.
    SW_ENGINE_SERVICE_SUBSYSTEM( DeterministicRun, GameVisible, Services, (), SW_ENGINE_DEFAULT_FACTORY( DeterministicRun ) );
} // namespace sw
