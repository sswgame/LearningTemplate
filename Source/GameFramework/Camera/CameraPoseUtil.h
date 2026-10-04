/**
 * @file CameraPoseUtil.h
 * @brief 카메라 포즈(`CameraPose`)와 `CameraComponent` 사이를 오가는 두 걸음 — 포즈를 카메라에 쓰기, 카메라를 포즈로 읽기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/Camera/CameraPose.h"
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
        /** @brief 카메라의 지금 월드 자리 · 회전과 렌즈를 포즈로 읽습니다(뷰 타깃 블렌드의 입력). */
        static CameraPose makePoseFromCamera( const CameraComponent& camera );
    };
} // namespace sw
