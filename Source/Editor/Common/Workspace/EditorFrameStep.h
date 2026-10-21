/**
 * @file EditorFrameStep.h
 * @brief 에디터 UI 프레임 안에서 순서대로 도는 단계(`SW_EDITOR_FRAME_STEP`)의 등록 줄입니다.
 */
#pragma once
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    class EditorContext;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorFrameStepOrder
     * @brief 셸이 아는 단계의 순서 값입니다. 작을수록 먼저 돕니다. 확장 모듈은 사이 값(예: 250)으로 끼어듭니다.
     * @details 셸(`ImGuiEditor::updateUI`)은 프레임을 열고(NewFrame · 메뉴 · 툴바 · 도크스페이스) 등록된 단계를 이 순서로 부른 뒤 프레임을 닫는다.
     *          순서에 뜻이 있다. 아래 값을 바꾸면 그 뜻이 깨진다.
     */
    struct EditorFrameStepOrder
    {
        /// 1) 단축키 · 메뉴가 미룬 패널 열기 · 씬 세션. 패널보다 앞이라 이번 프레임에 연 패널과 단축키 결과가 그대로 그려진다.
        static constexpr int32 kCommands = 100;
        /// 2) 에셋 · 설정 파일 감시, 저장 직후 검증, 버전 관리 상태. 패널보다 앞이라 이번 프레임에 바뀐 머티리얼이 그대로 보인다.
        static constexpr int32 kAssetReload = 200;
        /// 3) 패널 · 팝업 · 알림 그리기.
        static constexpr int32 kPanels = 300;
        /// 4) 자체 테스트 한 단계. 패널 뒤라 테스트가 이번 프레임의 패널 상태를 본다.
        static constexpr int32 kSelfTest = 400;
        /// 5) 창 제목. 이번 프레임의 편집 · 플레이 상태가 반영된 뒤에 쓴다.
        static constexpr int32 kWindowTitle = 500;
        /// 6) 스크린샷 요청의 결과 알림.
        static constexpr int32 kScreenshot = 600;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorFrameStepRegistration
     * @brief UI 프레임 단계 하나입니다. 셸은 단계를 모르고 순서대로 부르기만 합니다(언리얼 에디터의 Tick 델리게이트에 해당).
     * @details 단계는 ImGui 프레임 안(NewFrame 과 Render 사이)에서 UI 스레드가 부른다. 그래서 ImGui 를 그려도 된다. 순서는 `_order` 가 정하고,
     *          셸이 아는 단계의 값은 `EditorFrameStepOrder` 에 있다. 같은 id 의 둘째 등록은 거절되고, 핫 리로드 중에는 새 이미지의 등록이 옛 것을 대체한다.
     */
    struct EditorFrameStepRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "frameStep";

        void ( *_pfnRun )( EditorContext& context );
    };
} // namespace sw::editor

/** @code SW_EDITOR_FRAME_STEP( Panels, "panels", ::sw::editor::EditorFrameStepOrder::kPanels, &drawPanels ); @endcode */
#define SW_EDITOR_FRAME_STEP( name, pID, order, pfnRun ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorFrameStepRegistration, FrameStep_##name, { pID, order }, pfnRun )
