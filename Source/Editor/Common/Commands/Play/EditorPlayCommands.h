/**
 * @file EditorPlayCommands.h
 * @brief 플레이 옵션입니다 — 플레이 때 게임 뷰 최대화, 플레이 중 씬 뷰로 빠져나오기(Eject), 플레이 중 바꾼 값 남기기, 새 창(별도 프로세스)에서 플레이.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"

namespace sw
{
    class CommandLineManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorPlayCommands
     * @brief 플레이 툴바 밖의 플레이 옵션을 모읍니다(메뉴 Play · 게임 뷰 툴바 · 단축키).
     * @details 플레이 중 인스펙터 편집은 Stop 이 스냅샷으로 되돌린다(유니티 플레이 모드 편집). 남기려는 오브젝트는 Keep Simulation Changes 로
     *          적어 두면 Stop 이 되돌린 뒤 그 상태를 편집 씬에 입히고 되돌리기 한 번으로 기록한다(언리얼 "Keep Simulation Changes").
     */
    class EditorPlayCommands
    {
    public:
        /**
         * @brief UI 프레임마다 부릅니다(UI 쪽, 프레임을 열기 전) — 세션 전이를 보고 배치를 맞춥니다: 시작한 첫 프레임에 "Maximize On Play" 면 게임 뷰를 최대화하고,
         *        Eject 하면 최대화를 되돌리고 씬 뷰를, Possess 하면 게임 뷰를 앞으로 두고, 멈추면 배치를 되돌립니다.
         * @details 시작 · 정지 · Eject 는 플레이 툴바 · 단축키 · 콘솔 · 시나리오 어디서든 와서(게임 스레드가 적용한다) 전이를 여기서 한 번에 본다. 배치 상태는 UI 가 가진다
         *          (`EditorPlayLayoutState`) — 게임 스레드는 도킹 배치를 고치지 않는다(언리얼 PIE 의 레벨 에디터 탭 배치도 Slate 쪽이다).
         */
        static void update();
        /**
         * @brief F8: Eject/Possess 를 UI 명령으로 보냅니다(`EditorUICommandKind::ToggleEject`). 이 UI 프레임 뒤 게임 틱 시작에 적용됩니다. 플레이어 조종 중이면 Simulate 로 바꾸고
         *        씬 뷰를 앞으로(Eject), Eject 한 세션이면 다시 조종하고 게임 뷰를 앞으로 둡니다(Possess).
         * @details UI(메뉴 · 단축키 · 게임 뷰 툴바)는 세션을 바로 바꾸지 않는다 — UI 출력 계약(`EditorUICommandQueue`).
         */
        static void toggleEject();
        /** @brief 고른 오브젝트의 지금 상태를 Stop 뒤에도 남기게 적으라고 UI 명령으로 보냅니다(`EditorUICommandKind::KeepSelectedChanges` — 적으면 알림이 뜬다). */
        static void keepSelectedChanges();
        /** @brief 저장된 활성 씬을 새 창(엔진을 따로 띄운 프로세스)에서 엽니다(언리얼 Standalone Game). 띄웠으면 true 입니다. */
        [[nodiscard]] static bool launchStandalone();
        /**
         * @brief 새 창 플레이의 명령입니다 — 실행 파일, `-gv_firstScene=<씬>`, 환경설정의 덧붙일 인자(`StringUtil::splitQuotedTokens` 으로 나눈다) 순서입니다.
         *        엔진 자식이라 @p parent 가 자동화 실행이면 `-unattended` 가 붙는다(`EngineChildProcess`).
         */
        static ProcessCommand makeStandaloneCommand( const CommandLineManager& parent, const AbsolutePath& executablePath, string_view scenePath, string_view extraArguments );
    };
} // namespace sw::editor
