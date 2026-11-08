#include "pch.h"

#include "Engine/Window/WindowPlatform.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct WindowPlatformInternal
        {
            /** @brief 이 빌드의 창 플랫폼 공장입니다(이미지에 하나 — 플랫폼 소스의 정적 등록자가 건다). 정적 등록 공통 틀이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, const WindowPlatformFactory, "WindowPlatform" )
        };
    } // namespace

    const WindowPlatformFactory* WindowPlatform::find()
    {
        return WindowPlatformInternal::getSlot().get();
    }

    WindowPlatform::Registrar::Registrar( const WindowPlatformFactory& factory )
    {
        [[maybe_unused]] const RegistrationResult result = WindowPlatformInternal::getSlot().setLogged( &factory, "Window platform", "WindowPlatform" );
        SW_ASSERT( result == RegistrationResult::Added );
    }
} // namespace sw
