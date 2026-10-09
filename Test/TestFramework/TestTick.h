/**
 * @file TestTick.h
 * @brief 시험이 오브젝트 관리자를 고정 프레임 시간으로 여러 번 틱하는 도우미입니다(EngineTest 의 물리 · 파괴 · 기믹 · 내비 시험이 같이 쓴다).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

namespace test
{
    /** @brief 시험의 기본 프레임 시간(60 Hz)입니다. */
    inline constexpr float32 kTestFrameSeconds = 1.0f / 60.0f;

    /** @brief @p manager 를 @p frameCount 번 @p deltaTime 으로 틱합니다. */
    inline void tickFrames( sw::GameObjectManager& manager, uint32 frameCount, float32 deltaTime = kTestFrameSeconds )
    {
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            manager.tick( deltaTime );
        }
    }
} // namespace test
