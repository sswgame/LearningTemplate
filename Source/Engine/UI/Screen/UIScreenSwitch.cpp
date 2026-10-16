#include "pch.h"

#include "Engine/UI/Screen/UIScreenSwitch.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_LOG_CALLER( "UIScreenSwitch" );

    namespace
    {
        /** @brief 등록된 스위치((순서, 이름) 순 · 이름은 대소문자를 가린다). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다. */
        RegistrationList<const UIScreenSwitchRegistration>& getScreenSwitchRegistrations()
        {
            static RegistrationList<const UIScreenSwitchRegistration> s_registration{ "UIScreenSwitchRegistry", RegistrationOrder::ByOrderThenName, true };
            return s_registration;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool UIScreenSwitches::registerSwitch( const UIScreenSwitchRegistration* pRegistration )
    {
        const bool bInvalidRegistration = pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pFunction == nullptr;
        if ( bInvalidRegistration )
            return false;
        if ( getScreenSwitchRegistrations().add( pRegistration, pRegistration->_pName, pRegistration->_order ) != RegistrationResult::Added )
        {
            SW_LOG_ERROR( "UI screen switch '%#' is registered twice - the second one is ignored", pRegistration->_pName );
            return false;
        }
        return true;
    }

    void UIScreenSwitches::unregisterSwitch( const UIScreenSwitchRegistration* pRegistration )
    {
        (void)getScreenSwitchRegistrations().remove( pRegistration ); // 등록되어 있지 않은 스위치를 빼는 것은 할 일이 없다
    }

    const UIScreenSwitchRegistration* UIScreenSwitches::find( string_view name )
    {
        return getScreenSwitchRegistrations().findByName( name );
    }

    uint32 UIScreenSwitches::getCount()
    {
        return getScreenSwitchRegistrations().getCount();
    }

    const UIScreenSwitchRegistration* UIScreenSwitches::getAt( uint32 index )
    {
        return getScreenSwitchRegistrations().getAt( index );
    }

    UIScreenSwitchRegistrar::UIScreenSwitchRegistrar( const UIScreenSwitchRegistration* pRegistration )
        : _pRegistration{ UIScreenSwitches::registerSwitch( pRegistration ) ? pRegistration : nullptr }
    {
    }

    UIScreenSwitchRegistrar::~UIScreenSwitchRegistrar()
    {
        if ( _pRegistration != nullptr )
            UIScreenSwitches::unregisterSwitch( _pRegistration );
    }
} // namespace sw
