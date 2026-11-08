#include "pch.h"

#include "DevTools/DevToolsClientPart.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct DevToolsClientPartInternal
        {
            /** @brief 걸린 창구입니다(이미지에 하나 — 둘째는 거절). 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, IDevToolsClientPart, "DevToolsClientPart" )
        };
    } // namespace

    IDevToolsClientPart* IDevToolsClientPart::find()
    {
        return DevToolsClientPartInternal::getSlot().get();
    }

    bool IDevToolsClientPart::attach( IDevToolsClientPart* pPart )
    {
        return DevToolsClientPartInternal::getSlot().setLogged( pPart, "Dev tools client part", "DevToolsClientPart" ) == RegistrationResult::Added;
    }

    void IDevToolsClientPart::detach( IDevToolsClientPart* pPart )
    {
        (void)DevToolsClientPartInternal::getSlot().reset( pPart );
    }
} // namespace sw
