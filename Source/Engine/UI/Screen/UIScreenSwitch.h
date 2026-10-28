/**
 * @file UIScreenSwitch.h
 * @brief 개발 스위치(전역 변수)를 따라 화면을 열고 닫는 등록입니다. 화면 .cpp 가 `SW_UI_SCREEN_SWITCH` 한 줄로 스스로 등록하고, `UISystem::update` 는 등록을 순서대로 돌기만 합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    class UISystem;

    /**
     * @brief 스위치 함수입니다. 이 스위치가 전에 연 화면 @p current(없으면 무효)를 받아 스위치 값에 맞게 열고 닫은 뒤, 지금 열려 있는 화면(없으면 무효)을 돌려줍니다.
     * @details 같으면 아무것도 하지 않고 @p current 를 돌려준다. 게임 스레드의 `UISystem::update` 가 프레임마다 부른다.
     */
    using UIScreenSwitchFunction = UIScreenHandle ( * )( UISystem& uiSystem, UIScreenHandle current );

    /** @struct UIScreenSwitchRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct UIScreenSwitchRegistration
    {
        const utf8*            _pName; ///< 스위치 이름(예: `UIDemo`). `UISystem` 이 연 화면을 이 이름으로 기억합니다.
        int32                  _order; ///< 도는 순서(작을수록 먼저). 같은 레이어의 화면은 먼저 연 것이 아래에 쌓입니다.
        UIScreenSwitchFunction _pFunction;
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIScreenSwitches
     * @brief 화면 스위치 레지스트리입니다. 같은 이름이 둘이면 뒤의 것을 거절하고 오류를 남깁니다. 핫 리로드가 올리는 교체 이미지의 같은 이름은 대체합니다(`RegistrationReloadScope`).
     * @details 목록은 (순서, 이름) 순이다 — 정적 초기화 순서가 번역 단위마다 달라도 도는 순서가 같다. 게임 스레드와 정적 초기화에서만 쓴다.
     */
    class SW_API UIScreenSwitches
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerSwitch( const UIScreenSwitchRegistration* pRegistration );
        static void               unregisterSwitch( const UIScreenSwitchRegistration* pRegistration );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const UIScreenSwitchRegistration* find( string_view name );
        /** @brief 등록 수입니다. */
        static uint32 getCount();
        /** @brief 도는 순서로 @p index 번째 등록입니다. */
        static const UIScreenSwitchRegistration* getAt( uint32 index );
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_UI_SCREEN_SWITCH` 가 두는 정적 등록자입니다. 등록이 거절됐으면 내릴 때 빼지 않습니다(앞 등록을 지우지 않게). */
    struct SW_API UIScreenSwitchRegistrar
    {
        explicit UIScreenSwitchRegistrar( const UIScreenSwitchRegistration* pRegistration );
        ~UIScreenSwitchRegistrar();
        UIScreenSwitchRegistrar( const UIScreenSwitchRegistrar& )            = delete;
        UIScreenSwitchRegistrar& operator=( const UIScreenSwitchRegistrar& ) = delete;

        const UIScreenSwitchRegistration* _pRegistration;
    };
} // namespace sw

/**
 * @brief 화면 스위치를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다.
 * @details Shipping 은 Engine 을 통째로 링크하므로(`sw_linkWholeArchive`) 등록자가 링크에서 빠지지 않는다.
 */
#define SW_UI_SCREEN_SWITCH( id, pName, order, pFunction )                                           \
    static const ::sw::UIScreenSwitchRegistration sw_uiScreenSwitch_##id{ pName, order, pFunction }; \
    static const ::sw::UIScreenSwitchRegistrar    sw_uiScreenSwitchRegistrar_##id                    \
    {                                                                                                \
        &sw_uiScreenSwitch_##id                                                                      \
    }
