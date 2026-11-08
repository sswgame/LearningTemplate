#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct GameInstanceClientHookInternal
        {
            /** @brief 걸린 창구입니다(이미지에 하나 — 둘째는 거절). 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, IGameInstanceClientHook, "GameInstanceClientHook" )
        };
    } // namespace

    IGameInstanceClientHook* IGameInstanceClientHook::find()
    {
        return GameInstanceClientHookInternal::getSlot().get();
    }

    bool IGameInstanceClientHook::attach( IGameInstanceClientHook* pHook )
    {
        return GameInstanceClientHookInternal::getSlot().setLogged( pHook, "Game instance client hook", "GameInstanceClientHook" ) == RegistrationResult::Added;
    }

    void IGameInstanceClientHook::detach( IGameInstanceClientHook* pHook )
    {
        (void)GameInstanceClientHookInternal::getSlot().reset( pHook );
    }
} // namespace sw
