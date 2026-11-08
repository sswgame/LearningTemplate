#include "pch.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        /** @brief 붙은 채우는 함수입니다. 정적 초기화 · 종료 때만 쓰고 어느 스레드든 읽는다 — 값 하나라 원자 변수로 둔다. Engine 이미지의 함수 정적이라 등록한 이미지(개발 도구)보다 오래 산다. */
        std::atomic<RenderDiagnosticsProvider>& renderDiagnosticsProvider()
        {
            static std::atomic<RenderDiagnosticsProvider> s_provider{ nullptr };
            return s_provider;
        }
    } // namespace

    RenderDiagnosticsPolicy RenderDiagnostics::fetch()
    {
        RenderDiagnosticsPolicy         policy{};
        const RenderDiagnosticsProvider provider = renderDiagnosticsProvider().load( std::memory_order_acquire );
        if ( provider != nullptr )
            provider( policy );
        return policy;
    }

    bool RenderDiagnostics::registerProvider( RenderDiagnosticsProvider provider )
    {
        if ( provider == nullptr )
            return false;
        RenderDiagnosticsProvider expected = nullptr;
        if ( renderDiagnosticsProvider().compare_exchange_strong( expected, provider, std::memory_order_acq_rel ) )
            return true;
        SW_LOG_ERROR_ID( "RenderDiagnostics.ProviderAlreadySet", "A render diagnostics provider is already registered - the second one is ignored (one image fills the policy)" );
        return false;
    }

    void RenderDiagnostics::unregisterProvider( RenderDiagnosticsProvider provider )
    {
        RenderDiagnosticsProvider expected = provider;
        (void)renderDiagnosticsProvider().compare_exchange_strong( expected, nullptr, std::memory_order_acq_rel ); // 붙어 있지 않은 함수를 떼는 것은 할 일이 없다
    }

    RenderDiagnosticsRegistrar::RenderDiagnosticsRegistrar( RenderDiagnosticsProvider provider )
        : _provider{ RenderDiagnostics::registerProvider( provider ) ? provider : nullptr }
    {
    }

    RenderDiagnosticsRegistrar::~RenderDiagnosticsRegistrar()
    {
        if ( _provider != nullptr )
            RenderDiagnostics::unregisterProvider( _provider );
    }
} // namespace sw
