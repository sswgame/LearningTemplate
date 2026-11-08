#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettingsOverride.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct GameSettingsOverrideInternal
        {
            /** @brief 걸린 창구입니다(이미지에 하나 — 둘째는 거절). 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, IGameSettingsOverride, "GameSettingsOverride" )
        };
    } // namespace

    IGameSettingsOverride* IGameSettingsOverride::find()
    {
        return GameSettingsOverrideInternal::getSlot().get();
    }

    bool IGameSettingsOverride::attach( IGameSettingsOverride* pSettingsOverride )
    {
        return GameSettingsOverrideInternal::getSlot().setLogged( pSettingsOverride, "Game settings override", "GameSettingsOverride" ) == RegistrationResult::Added;
    }

    void IGameSettingsOverride::detach( IGameSettingsOverride* pSettingsOverride )
    {
        (void)GameSettingsOverrideInternal::getSlot().reset( pSettingsOverride );
    }
} // namespace sw
