/**
 * @file EditorFrameStep.h
 * @brief 에디터 UI 프레임 안에서 순서대로 도는 단계(`SW_EDITOR_FRAME_STEP`)의 등록 줄입니다.
 */
#pragma once
#include "Editor/Common/EditorExtensionRegistry.h"

namespace sw::editor
{
    class EditorContext;
    class EditorPlaySessionWriteKey;
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
        /// 2) 버전 관리 상태 · 저장 직후 검증. 에셋 감시 · 리로드는 게임 단계다(`EditorGameStepOrder::kAssetReload`).
        static constexpr int32 kAssetReload = 200;
        /// 활성 씬의 자동 저장 사본 쓰기와 복원 마무리(`EditorAutosaveCommands::tick`). 씬 편집 명령 뒤라 이번 프레임 편집이 사본에 든다.
        static constexpr int32 kAutosave = 300;
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
        SW_EDITOR_EXTENSION_KIND( "frameStep" );

        void ( *_pfnRun )( EditorContext& context );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorGameStepRegistration
     * @brief 게임 스레드에서 프레임마다 도는 에디터 단계 하나입니다(`SW_EDITOR_GAME_STEP`) — 씬 · 엔진을 바꾸는 여러 프레임짜리 일(베이크)의 자리.
     * @details 셸이 UI 명령을 적용한 뒤 부른다(`ImGuiEditor::applyUICommands` — 호스트의 UI 프레임 뒤 · 씬 틱 앞). ImGui 를 그리지 않는다. UI 프레임 단계(`EditorFrameStepRegistration`)와 달리
     *          게임 스레드 입구라 플레이 세션 쓰기 열쇠(`EditorPlaySessionWriteKey`)를 받는다. UI 는 이 단계의 일을 시작하려면 요청(값)만 남긴다(언리얼 에디터의 게임 스레드 Tick 자리).
     */
    struct EditorGameStepRegistration : EditorExtensionRegistration
    {
        SW_EDITOR_EXTENSION_KIND( "gameStep" );

        void ( *_pfnRun )( EditorContext& context, EditorPlaySessionWriteKey key );
    };

    /** @struct EditorGameStepOrder @brief 셸이 아는 게임 단계의 순서 값입니다. 작을수록 먼저 돕니다. */
    struct EditorGameStepOrder
    {
        /// UI 가 남긴 씬 열기 요청(`EditorWorkspace::requestLoadScene`)을 연다 — 저장하지 않은 변경이 있으면 확인 창 동작을 남긴다(UI 가 다음 프레임에 띄운다).
        static constexpr int32 kSceneSession = 40;
        /// UI 가 요청한 씬 편집 명령(`EditorCommandRegistry::request` — 거래를 여는 명령)을 요청한 순서대로 실행한다. 베이크보다 앞이라 같은 프레임의 베이크 시작이 편집 뒤의 씬을 읽는다.
        static constexpr int32 kSceneCommands = 50;
        /// 여러 프레임에 나눠 도는 베이크(라이트 프로브 · 반사 프로브).
        static constexpr int32 kBake = 100;
        /// 에셋 · 설정 파일 감시와 임포트 결과 반영(캐시 리로드 · 씬 알림 — `AssetHotReload::update`). 다음 UI 프레임이 바뀐 에셋을 그린다.
        static constexpr int32 kAssetReload = 200;
        /// 활성 씬의 자동 저장 사본 쓰기와 복원 마무리(`EditorAutosaveCommands::tick`). 씬 편집 명령 뒤라 이번 프레임 편집이 사본에 든다.
        static constexpr int32 kAutosave = 300;
    };
} // namespace sw::editor

/** @code SW_EDITOR_FRAME_STEP( Panels, "panels", ::sw::editor::EditorFrameStepOrder::kPanels, &drawPanels ); @endcode */
#define SW_EDITOR_FRAME_STEP( name, pID, order, pfnRun ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorFrameStepRegistration, FrameStep_##name, { pID, order }, pfnRun )

/** @code SW_EDITOR_GAME_STEP( LightProbeBake, "lightProbeBake", ::sw::editor::EditorGameStepOrder::kBake, &runBake ); @endcode */
#define SW_EDITOR_GAME_STEP( name, pID, order, pfnRun ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorGameStepRegistration, GameStep_##name, { pID, order }, pfnRun )
