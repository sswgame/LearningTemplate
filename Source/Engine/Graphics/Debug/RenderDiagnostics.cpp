#include "pch.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"

#include "Core/String/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        /** @brief 붙은 채우는 함수입니다(이미지에 하나 — 둘째는 거절). 걸기 · 떼기는 정적 초기화 · 종료 때, 읽기는 어느 스레드든. Engine 이미지의 함수 정적이라 등록한 이미지(개발 도구)보다 오래 산다. */
        SW_DEFINE_REGISTRATION_SLOT( getProviderSlot, void( RenderDiagnosticsPolicy& ), "RenderDiagnosticsProvider" )
    } // namespace

    RenderDiagnosticsPolicy RenderDiagnostics::fetch()
    {
        RenderDiagnosticsPolicy         policy{};
        const RenderDiagnosticsProvider provider = getProviderSlot().get();
        if ( provider != nullptr )
            provider( policy );
        return policy;
    }

    bool RenderDiagnostics::registerProvider( RenderDiagnosticsProvider provider )
    {
        return getProviderSlot().setLogged( provider, "Render diagnostics provider", "RenderDiagnosticsProvider" ) == RegistrationResult::Added;
    }

    void RenderDiagnostics::unregisterProvider( RenderDiagnosticsProvider provider )
    {
        (void)getProviderSlot().reset( provider ); // 붙어 있지 않은 함수를 떼는 것은 할 일이 없다
    }
} // namespace sw
