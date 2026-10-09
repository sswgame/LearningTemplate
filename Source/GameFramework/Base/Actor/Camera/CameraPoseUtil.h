/**
 * @file CameraPoseUtil.h
 * @brief 카메라 포즈(`CameraPose`)와 `CameraComponent` 사이를 오가는 두 걸음 — 포즈를 카메라에 쓰기, 카메라를 포즈로 읽기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CameraComponent;

    /**
     * @struct CameraPoseUtil
     * @brief 디렉터 · 카메라 매니저 · 직교 리그 · 1인칭 카메라가 같은 규칙으로 카메라를 둡니다.
     */
    struct SW_GF_API CameraPoseUtil
    {
        /**
         * @brief 포즈를 카메라에 씁니다 — 렌즈와 월드 자리 · 회전(스케일은 지금 것을 지킨다).
         * @details 포즈는 월드 값이라 부모 아래에 둔 카메라도 같은 자리를 봅니다(`setWorldTransform`).
         */
        static void applyToCamera( CameraComponent& camera, const CameraPose& pose );
        /**
         * @brief 포즈를 카메라의 **부모 공간**에 씁니다 — 렌즈와 로컬 자리 · 회전. 대상이 카메라 주인의 로컬 값(1인칭의 눈 자리)일 때 씁니다.
         * @details 부모(몸 · 탈것)가 움직이고 돌면 카메라도 계층을 따라간다. 월드로 쓰면 부모를 버리고 원점 근처에 남는다.
         */
        static void applyToCameraLocal( CameraComponent& camera, const CameraPose& pose );
        /** @brief 렌즈(원근 · 직교, 시야각, 높이, 가까운 · 먼 면)만 씁니다. */
        static void applyLensToCamera( CameraComponent& camera, const CameraPose& pose );
        /** @brief 카메라의 지금 월드 자리 · 회전과 렌즈를 포즈로 읽습니다(뷰 타깃 블렌드의 입력). */
        static CameraPose makePoseFromCamera( const CameraComponent& camera );
    };
} // namespace sw
