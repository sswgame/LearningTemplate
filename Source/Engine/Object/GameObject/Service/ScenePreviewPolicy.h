/**
 * @file Engine/Object/GameObject/Service/ScenePreviewPolicy.h
 * @brief 미리보기 씬의 틱 정책입니다(`SceneManager::createPreviewScene`). 씬 서비스(`ISceneService::applyPreviewPolicy`)가 자기 몫(오디오 · 물리)을 읽습니다.
 * @details 씬 서비스 계약 옆에 둔다 — 기능 폴더(Physics · Audio)가 씬 매니저 헤더를 include 하지 않고 정책을 읽게.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 미리보기 씬을 누가 틱하는지입니다(`ScenePreviewPolicy::_tick`). */
    enum class ScenePreviewTick : uint8
    {
        /**
         * 씬 매니저가 틱하되 호스트가 이번 프레임에 그 씬의 뷰를 요청한(그려질) 프레임에만 한다(`updatePreviewScenes` 의 프레임 입력).
         * 숨은 창(뒤 탭 · 접힌 창)의 씬은 시간이 멈춘다 — 다시 보이면 그 프레임의 델타로 이어 가므로 숨었던 시간만큼 튀지 않는다.
         */
        WhenDrawn = 0,
        External, ///< 씬 매니저는 틱하지 않는다 — 씬의 주인(하니스)이 직접 틱한다.
    };

    /**
     * @brief 미리보기 씬의 틱 정책입니다(`SceneManager::createPreviewScene`). 프리팹 창은 틱 · 물리 없음, 애니메이션 창은 틱만, 래그돌 창은 물리까지입니다.
     */
    struct ScenePreviewPolicy
    {
        ScenePreviewTick _tick{ ScenePreviewTick::WhenDrawn }; ///< 누가 언제 틱하는지
        float32          _timeScale{ 1.0f };                   ///< 이 씬의 시간 배율(틱 델타에 곱한다). 0 이면 틱은 돌되 시간은 멈춘다
        bool             _bPhysics{ false };                   ///< 물리 단계를 돌릴지
        bool             _bAudio{ false };                     ///< 오디오 엔진에 리스너 · 에미터를 넣을지(보통 끈다)
        bool             _bPlay{ false };                      ///< 만들 때 `beginPlay`, 내릴 때 `endPlay` 를 부를지(컴포넌트의 `onBeginPlay` 에 기대는 창)
        /**
         * @brief 이 씬 물리의 고정 스텝(초)입니다. 0 이면 엔진 설정의 고정 스텝(`EngineConfig::_fixedDeltaTime`)을 쓴다 — 정책의 기본값이지 빠진 값의 대체가 아니다.
         * @details 래그돌 · 물리 에셋 창이 관절 한계를 촘촘한 스텝으로 보려고 쓴다(`ScenePhysics::setFixedStepOverride`). 음수는 오류다.
         */
        float32 _fixedStepOverride{ 0.0f };
    };
} // namespace sw
