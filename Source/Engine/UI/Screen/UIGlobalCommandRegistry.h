/**
 * @file UIGlobalCommandRegistry.h
 * @brief 화면이 처리하지 않은 UI 명령(`_command`)을 받는 전역 명령 표입니다. 게임 흐름의 일(타이틀로 가기, 끝내기)을 엔진 화면이 모른 채 이어 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    class Widget;
} // namespace sw

namespace sw
{
    /**
     * @class UIGlobalCommandRegistry
     * @brief 이름 → 처리 함수 표입니다(`UISystem::getGlobalCommands`). 화면이 `onCommand` 로 처리하지 않은 명령이 마지막으로 여기에 닿습니다.
     * @details 언리얼 CommonUI 의 입력 액션 바인딩이 위젯 밖의 핸들러를 부르는 것, 유니티 UI Toolkit 의 명령 이벤트가 패널 밖으로 올라가는 것에 해당한다.
     *          엔진 일시정지 메뉴(`PauseMenuScreen`)의 `ReturnToTitle` · `QuitGame` 버튼은 이 표에 그 이름이 등록됐을 때만 보인다 —
     *          게임 흐름(`GameInstanceBase`)이나 호스트(`EngineLoop`)가 자기 처리 함수를 등록한다. 함수가 모듈 이미지 안에 있으면 그 모듈이 언로드될 때 `UISystem` 이 제거한다(`removeCodeWithin`).
     *          게임 스레드에서만 쓴다.
     */
    class SW_API UIGlobalCommandRegistry
    {
    public:
        static constexpr utf8 kReturnToTitle[] = "ReturnToTitle"; ///< 일시정지 메뉴의 "타이틀로" 명령입니다.
        static constexpr utf8 kQuitGame[]      = "QuitGame";      ///< 일시정지 메뉴의 "끝내기" 명령입니다.

        UIGlobalCommandRegistry();

        /** @brief 명령 @p command 의 처리 함수를 등록합니다. 같은 이름이 있으면 교체합니다. 호출한 쪽이 끝날 때 `unregisterCommand` 로 거둡니다. */
        void registerCommand( const hashed_string& command, const UICommandDelegate& handler );
        void unregisterCommand( const hashed_string& command );
        /** @brief 그 이름의 처리 함수가 있으면 true 입니다(일시정지 메뉴가 버튼을 보일지 정할 때 묻습니다). */
        bool hasCommand( const hashed_string& command ) const;
        /** @brief 처리 함수가 있으면 @p source 를 넘겨 부르고 true 입니다. 없으면 false 입니다. */
        [[nodiscard]] bool tryExecute( const hashed_string& command, Widget& source ) const;
        /** @brief 함수가 [@p pBegin, @p pEnd) 안에 있는 항목을 제거합니다(모듈 핫 리로드). 제거한 개수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

    private:
        NameRegistry<UICommandDelegate> _registry;
    };
} // namespace sw
