/**
 * @file MainMenuScreen.h
 * @brief 메인 메뉴입니다. 엔진 기본 문서 `engine/ui/mainmenu.ui.xml`(시작, 옵션, 끝내기)을 쓰며, 타이틀 씬에 둔 `TitleMenuComponent` 가 엽니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Menu/ConfirmDialogScreen.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /**
     * @class MainMenuScreen
     * @brief 타이틀 화면의 첫 메뉴입니다(Lyra 의 Frontend 메인 메뉴, 언리얼 CommonUI 의 루트 활성화 위젯).
     * @details 게임 흐름의 일(시작 · 끝내기)은 이 화면이 하지 않고 전역 명령 표(`UIGlobalCommandRegistry::kStartGame` · `kQuitGame`)로 넘긴다 — 게임 흐름(`GameSessionCommands`)과
     *          호스트(`UIPlatformBinding`)가 처리 함수를 건다. 끝내기는 되돌릴 수 없으므로 공통 확인 대화 상자(`ConfirmDialogScreen`)를 먼저 띄운다.
     *          `QuitGame` 단추는 표에 그 명령이 있을 때만 보인다(에디터 안에서는 없다 — 일시정지 메뉴와 같은 규칙). 뒤로 가기는 아무것도 하지 않는다(첫 화면이다).
     */
    class SW_API MainMenuScreen : public UIScreen
    {
    public:
        static constexpr const utf8* kOpenOptionsCommand = "OpenOptions";

        MainMenuScreen( const UIScreenDef& desc, unique_ptr<Widget> root );
        ~MainMenuScreen() override;

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 첫 화면이라 닫지 않습니다(처리한 것으로 답한다). */
        bool onBack() override { return true; }
        /** @brief 전역 명령 표에 맞춰 `QuitGame` 단추를 보이거나 접습니다. */
        void onTick( float32 deltaSeconds ) override;

    private:
        /** @brief 끝내기 확인의 답입니다. 확인이고 메뉴 @p menu 가 아직 떠 있으면 전역 명령 `QuitGame` 을 부릅니다. */
        static void onQuitConfirmed( UISystem& ui, UIScreenHandle menu, ConfirmDialogResult result );
    };
} // namespace sw
