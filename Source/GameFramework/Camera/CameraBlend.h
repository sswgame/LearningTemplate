/**
 * @file CameraBlend.h
 * @brief 두 카메라 포즈 섞기(`blendPoses`)입니다. 곡선 · 길이 · 가중치는 엔진의 `BlendCurveSpec` · `evaluateBlendWeight`(`Engine/Animation/BlendCurve.h`)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Animation/BlendCurve.h"

#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 두 포즈를 @p weight(0 = @p from, 1 = @p to)로 섞습니다.
     * @details 자리 · 시야각 · 직교 높이 · 근/원평면은 직선, 회전은 짧은 쪽 구면 보간입니다. 직교 ↔ 원근은 섞지 않고 가중치 0.5 에서 바꿉니다
     *          (Cinemachine 도 투영을 섞지 않는다 — 두 투영 사이의 행렬은 어느 쪽 화면과도 닮지 않는다).
     */
    SW_GF_API CameraPose blendPoses( const CameraPose& from, const CameraPose& to, float32 weight );
} // namespace sw
