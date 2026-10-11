/**
 * @file PauseMenuScreen.h
 * @brief 일시정지 메뉴입니다. 엔진 기본 문서 `engine/ui/pause.ui.xml`(계속, 옵션)을 쓰며, 화면이 없을 때 `UI.Pause`(Esc, 패드 Start)가 엽니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /**
     * @class PauseMenuScreen
     * @brief 명령 `Resume`(닫기)과 `OpenOptions`(옵션 메뉴를 위에 연다)를 받는 일시정지 화면입니다(Lyra 의 Escape 메뉴에 해당).
     * @details 여는 경로는 `UISystem` 하나뿐이다. 게임 프리셋이 `_bUIPauseMenu` 를 켜면 화면이 없을 때 UI 맵의 `UI.Pause` 가 연다(그 입력은 UI 가 소비하므로 게임이
     *          보지 못한다). 게임 정지는 문서의 `_bPausesGame` 으로 정한다. 타이틀로 가기와 끝내기는 게임 흐름의 일이라 여기에 두지 않는다.
     */
    class SW_API PauseMenuScreen : public UIScreen
    {
    public:
        PauseMenuScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~PauseMenuScreen() override;

        bool onCommand( const hashed_string& command, Widget& source ) override;
    };
} // namespace sw
