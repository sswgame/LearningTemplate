/**
 * @file AnimationDebugState.h
 * @brief 애니메이션 진단 상태 — 그래프 상태 · 상태 시각 · 이번 프레임 알림 · 커브 · 루트 모션 · 스프라이트 프레임입니다(되감기 기록 · 패널이 읽는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @struct AnimationDebugState
     * @brief 단계 일(`IAnimationPhaseTask::collectDebugState`) · 스프라이트 애니메이터가 채웁니다. 채우지 않은 칸은 기본값입니다.
     */
    struct AnimationDebugState
    {
        hashed_string         _stateName;         ///< 그래프 상태(없으면 재생 중인 클립)
        float32               _stateTime{ 0.0f }; ///< 상태 안 재생 시각(초)
        vector<hashed_string> _listNotify;        ///< 이번 프레임에 지나간 알림
        vector<hashed_string> _listCurveName;     ///< 이번 프레임 커브 이름
        vector<float32>       _listCurveValue;    ///< `_listCurveName` 과 나란한 값
        float3                _rootMotionTranslation{};
        quaternion            _rootMotionRotation{};
        int32                 _spriteFrame{ -1 }; ///< 스프라이트 프레임 번호(스켈레탈은 -1)

        /** @brief 비웁니다(목록의 용량은 둔다 — 고리 버퍼가 프레임마다 다시 쓴다). */
        void reset()
        {
            _stateName = hashed_string{};
            _stateTime = 0.0f;
            _listNotify.clear();
            _listCurveName.clear();
            _listCurveValue.clear();
            _rootMotionTranslation = float3{};
            _rootMotionRotation    = quaternion::Identity;
            _spriteFrame           = -1;
        }
    };
} // namespace sw
