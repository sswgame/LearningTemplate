/**
 * @file EditorPlayCommands.h
 * @brief 플레이 옵션입니다 — 플레이 때 게임 뷰 최대화, 플레이 중 씬 뷰로 빠져나오기(Eject), 플레이 중 바꾼 값 남기기, 새 창(별도 프로세스)에서 플레이.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

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
        /** @brief Shift+Space 최대화와 플레이 때 최대화가 함께 쓰는 임시 레이아웃 이름입니다(에디터 상태 폴더의 `Temp`). */
        static constexpr const utf8* kMaximizeLayoutName = "maximize-restore";

        /** @brief 지금 배치를 임시 레이아웃으로 저장하고 @p keepPanelID 밖의 패널을 닫은 뒤 그 패널을 앞으로 둡니다. 저장하지 못했으면 false 입니다. */
        [[nodiscard]] static bool maximizePanel( string_view keepPanelID );
        /** @brief 최대화 전 배치(임시 레이아웃)를 다음 프레임에 되읽습니다. 그 파일이 없으면 false 입니다. */
        [[nodiscard]] static bool restoreMaximizedLayout();

        /**
         * @brief 매 에디터 프레임에 부릅니다 — 세션이 시작된 첫 프레임에 "Maximize On Play" 면 게임 뷰를 최대화하고, 멈추면 되돌립니다.
         * @details 시작과 정지는 플레이 툴바 · 단축키 · 시나리오 어디서든 올 수 있어 상태 전이를 여기서 한 번에 본다.
         */
        static void tick();
        /** @brief F8 — 플레이어 조종 중이면 Simulate 로 바꾸고 씬 뷰를 앞으로(Eject), Eject 한 세션이면 다시 조종하고 게임 뷰를 앞으로 둡니다(Possess). */
        static void toggleEject();
        /** @brief 고른 오브젝트의 지금 상태를 Stop 뒤에도 남기게 적습니다. 적은 오브젝트 수입니다(플레이 중이 아니면 0). */
        static uint32 keepSelectedChanges();
        /** @brief 저장된 활성 씬을 새 창(엔진을 따로 띄운 프로세스)에서 엽니다(언리얼 Standalone Game). 띄웠으면 true 입니다. */
        [[nodiscard]] static bool launchStandalone();
        /** @brief 새 창 플레이의 명령 줄입니다 — 실행 파일, `-gv_firstScene=<씬>`, 환경설정의 덧붙일 인자 순서입니다. */
        static string makeStandaloneCommand( string_view executablePath, string_view scenePath, string_view extraArguments );
    };
} // namespace sw::editor
