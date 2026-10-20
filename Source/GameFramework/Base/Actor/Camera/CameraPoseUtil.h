/**
 * @file CameraPoseUtil.h
 * @brief 카메라 포즈(`CameraPose`)와 `CameraComponent` 사이를 오갑니다. 포즈를 카메라에 쓰고, 카메라를 포즈로 읽습니다.
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
     * @brief 디렉터, 카메라 매니저, 1인칭 카메라가 같은 규칙으로 카메라를 놓게 합니다.
     */
    struct SW_GF_API CameraPoseUtil
    {
        /**
         * @brief 포즈를 카메라에 씁니다. 렌즈와 월드 위치, 회전을 쓰고 스케일은 지금 것을 지킵니다.
         * @details 포즈는 월드 값이라 부모 아래에 둔 카메라도 같은 위치를 봅니다(`setWorldTransform`).
         */
        static void applyToCamera( CameraComponent& camera, const CameraPose& pose );
        /**
         * @brief 포즈를 카메라의 **부모 공간**에 씁니다. 렌즈와 로컬 위치, 회전을 씁니다. 대상이 카메라 주인의 로컬 값(1인칭의 눈 위치)일 때 씁니다.
         * @details 부모(몸 · 탈것)가 움직이고 돌면 카메라도 계층을 따라간다. 월드로 쓰면 부모를 버리고 원점 근처에 남는다.
         */
        static void applyToCameraLocal( CameraComponent& camera, const CameraPose& pose );
        /** @brief 렌즈(원근이나 직교, 시야각, 높이, 근평면과 원평면)와 후처리 값만 씁니다. 위치와 회전은 쓰지 않습니다. */
        static void applyLensToCamera( CameraComponent& camera, const CameraPose& pose );
        /** @brief 카메라의 지금 월드 위치, 회전, 렌즈를 포즈로 읽습니다. 뷰 타깃 블렌드의 입력입니다. */
        static CameraPose makePoseFromCamera( const CameraComponent& camera );
        /**
         * @brief 카메라가 그리는 화면의 가로/세로 비율입니다.
         * @details 렌더 텍스처 출력이면 그 텍스처 크기, 화면 출력이면 활성 창 크기에 화면 사각형을 곱한 크기로 센다.
         *          창이 없으면(테스트, 서버) `CameraTarget::kDefaultAspectRatio` 이다.
         */
        static float32 computeAspectRatio( const CameraComponent& camera );
    };
} // namespace sw
