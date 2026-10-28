/**
 * @file UIGlobalCommandRegistry.h
 * @brief 화면이 처리하지 않은 UI 명령(`_command`)을 받는 전역 명령 표입니다. 게임 흐름의 일(타이틀로 가기, 끝내기)을 엔진 화면이 모른 채 이어 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
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
     *          게임 흐름(`GameInstance`)이나 호스트(`Engine`)가 자기 처리 함수를 등록한다. 함수가 모듈 라이브러리 안에 있으면 그 모듈이 언로드될 때 `UISystem` 이 제거한다(`removeCodeWithin`).
     *          게임 스레드에서만 쓴다.
     */
    class SW_API UIGlobalCommandRegistry
    {
    public:
        static constexpr utf8 kReturnToTitle[] = "ReturnToTitle"; ///< 일시정지 메뉴의 "타이틀로" 명령입니다.
        static constexpr utf8 kQuitGame[]      = "QuitGame";      ///< 일시정지 메뉴의 "끝내기" 명령입니다.

        UIGlobalCommandRegistry();

        /**
         * @brief 명령 @p command 의 처리 함수를 등록하고 그 구독을 돌려줍니다. 같은 이름이 있으면 교체합니다. 구독이 사라지면 거둡니다.
         * @details 교체된 옛 구독은 놓아도 지금 등록을 지우지 않는다 — 핫 리로드는 새 게임 인스턴스를 먼저 세우고 옛 인스턴스를 내린다.
         */
        [[nodiscard]] Subscription registerCommand( const hashed_string& command, const UICommandDelegate& handler );
        /** @brief 그 이름의 처리 함수가 있으면 true 입니다(일시정지 메뉴가 버튼을 보일지 정할 때 묻습니다). */
        bool hasCommand( const hashed_string& command ) const;
        /** @brief 처리 함수가 있으면 @p source 를 넘겨 부르고 true 입니다. 없으면 false 입니다. */
        [[nodiscard]] bool tryExecute( const hashed_string& command, Widget& source ) const;
        /** @brief 함수가 [@p pBegin, @p pEnd) 안에 있는 항목을 제거합니다(모듈 핫 리로드). 제거한 개수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

    private:
        /** @brief 명령 하나의 지금 등록 구독 번호입니다. 교체되면 새 번호로 바뀐다. */
        struct CommandOwner
        {
            hashed_string _command{};
            uint64        _id{ 0 };
        };

        /** @brief 등록 구독 @p id 가 풀렸습니다. 그 명령의 지금 등록이 이 구독이면 거둡니다. */
        void releaseSubscription( uint64 id );

        NameRegistry<UICommandDelegate> _registry;
        vector<CommandOwner>            _listOwner;   ///< 명령마다 지금 등록의 구독 번호(명령은 몇 개뿐이다)
        uint64                          _nextOwnerID; ///< 다음 등록 구독 번호(0 은 쓰지 않는다)

    private:
        /** @brief 이 객체가 내준 구독입니다(믹스인 상속 대신 합성). 풀리면 `releaseSubscription` 이 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&UIGlobalCommandRegistry::releaseSubscription>( this ) };
    };
} // namespace sw
