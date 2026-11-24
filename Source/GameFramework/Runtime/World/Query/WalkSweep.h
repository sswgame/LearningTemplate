/**
 * @file WalkSweep.h
 * @brief 바디 없이 자리만 옮기는 몸(점 이동 디렉터의 사람 · 농부)의 한 걸음을 씬의 막는 바디에 맞춥니다.
 * @details 몸 반지름의 구를 평면으로 쓸어 닿는 데까지 걷고 남은 걸음은 벽을 따라 미끄러진다(언리얼 `SweepSingle` + `SlideAlongSurface`).
 *          캐릭터 컨트롤러를 쓰는 몸은 그쪽이 같은 일을 하므로 쓰지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AbsolutePosition;

    class GameObjectManager;

    /** @struct WalkSweep @brief 파일 머리말 참고. 물리를 묻는 함수라 게임 스레드에서만 부릅니다. */
    struct SW_GF_API WalkSweep
    {
        static constexpr float32 kSkin = 0.02f; ///< 벽 · 땅에 닿은 채로 쓸지 않게 남기는 틈(m) — 몸의 구 가운데는 땅에서 반지름 + 이 값 위에 둔다

        /**
         * @brief 구 가운데 @p center(씬 로컬)에서 평면으로 @p delta(x · z) 만큼 걸어 닿는 구 가운데입니다.
         * @details 막는 바디가 없는 씬이면 그대로 걷는다. 벽에서 멀어지는 걸음은 겹친 채 시작해도 막지 않는다(빠져나온다).
         */
        static float3 slide( const GameObjectManager& manager, const float3& center, const float2& delta, float32 radius );
        /**
         * @brief 발이 절대 자리 @p foot 에 선 몸이 평면으로 @p delta 만큼 걸어 실제로 간 걸음입니다(`slide` 의 땅 위 몸).
         * @param pManager 씬이 없는 몸(테스트가 씬 없이 만든 디렉터)이면 nullptr 를 적는다 — 그때 막는 바디가 없다.
         */
        static float2 walkOnGround( const GameObjectManager* pManager, const AbsolutePosition& foot, const float2& delta, float32 radius );
    };
} // namespace sw
