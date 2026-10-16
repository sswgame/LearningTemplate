/**
 * @file CameraUserSettings.h
 * @brief 카메라가 읽는 사용자 설정(옵션 메뉴의 시야각 · 흔들림 배율 · 머리 흔들림)을 정의합니다.
 * @details 값의 정본은 엔진 전역 변수 `gv_cameraFieldOfView` · `gv_cameraShakeScale` · `gv_cameraHeadBob`(`Engine/UserSettings/UserSettingsVariables.h`)이다.
 *          카메라 컴포넌트(디렉터 · 1인칭 카메라 · 카메라 매니저)가 포즈를 풀 때마다 `makeFromGlobalVariables` 로 읽어 계산에 넘긴다.
 *          모드 계산(`CameraMode`)은 이 값을 모른다 — 순수 계산으로 남아 테스트가 값을 직접 넣는다.
 *          시야각은 게임 시점(1인칭 · 3인칭 모드)에만 건다. 언리얼 · 유니티 게임이 옵션 시야각을 플레이어 카메라에만 거는 것과 같다(시네마틱 · 고정 카메라는 저작한 렌즈 그대로).
 *          사용자 시야각을 그대로 덮지 않고 변수의 기본값 대비 비율(반각의 tan 비율)로 프리셋 시야각을 넓히고 좁힌다. 기본값이면 저작한 화면 그대로이고, 조준 프리셋처럼
 *          좁게 저작한 시야각도 같은 비율로 따라간다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 카메라가 읽는 사용자 설정 값입니다. 기본값은 사용자가 아무것도 바꾸지 않은 상태(저작한 화면 그대로)입니다. */
    struct SW_GF_API CameraUserSettings
    {
        float32 _fieldOfViewScale{ 1.0f }; ///< tan(사용자 시야각 / 2) ÷ tan(기본 시야각 / 2). 1인칭 · 3인칭 시야각 반각의 tan 에 곱한다
        float32 _shakeScale{ 1.0f };       ///< 흔들림(손떨림 잡음 · 충격) 오프셋에 곱한다. 0 이면 끔
        bool    _bHeadBob{ true };         ///< 걷기 머리 흔들림(`<HeadBob>`)을 켠다

        /**
         * @brief 엔진 전역 변수에서 읽습니다. 시야각 비율의 기준은 `gv_cameraFieldOfView` 의 기본값입니다.
         * @details 엔진 서비스가 묶이지 않은 실행(엔진 없이 도는 테스트 하네스)에는 사용자 설정이 없으므로 기본값(바꾸지 않은 상태)을 돌려준다.
         *          변수 관리자가 있는데 변수가 없으면 등록이 빠진 결함이라 `SW_ASSERT` 로 멈춘다.
         */
        static CameraUserSettings makeFromGlobalVariables();

        /** @brief @p mode 가 1인칭 · 3인칭이고 원근이면 포즈의 시야각을 `_fieldOfViewScale` 로 넓히거나 좁힌 포즈를, 아니면 그대로 돌려줍니다. */
        CameraPose applyFieldOfView( CameraPresetMode mode, const CameraPose& pose ) const;
    };
} // namespace sw
