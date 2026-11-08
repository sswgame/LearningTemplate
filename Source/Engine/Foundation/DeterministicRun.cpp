#include "pch.h"

#include "Engine/Foundation/DeterministicRun.h"

namespace sw
{
    namespace
    {
        /** @brief 프로세스의 정책입니다(함수 정적 — 정적 초기화 순서와 무관하다). */
        DeterministicRunPolicy& getPolicyStorage()
        {
            static DeterministicRunPolicy s_policy{};
            return s_policy;
        }
    } // namespace

    DeterministicRunPolicy::DeterministicRunPolicy()
        : _bFinishAssetWorkEachFrame{ SW_FALSE }
        , _bWaitForAnimationClips{ SW_FALSE }
        , _bFreezeEnvironmentTime{ SW_FALSE }
    {
    }

    const DeterministicRunPolicy& DeterministicRun::getPolicy()
    {
        return getPolicyStorage();
    }

    void DeterministicRun::setPolicy( const DeterministicRunPolicy& policy )
    {
        getPolicyStorage() = policy;
    }
} // namespace sw
