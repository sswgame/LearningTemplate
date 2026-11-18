#include "pch.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Registry/RegistrationSlot.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"

#include <type_traits>

namespace sw
{
    namespace
    {
        /** @brief 붙은 채우는 함수입니다(이미지에 하나 — 둘째는 거절). 걸기 · 떼기는 정적 초기화 · 종료 때, 부르기는 게임 스레드(`publish`)만. Engine 이미지의 함수 정적이라 등록한 이미지(개발 도구)보다 오래 산다. */
        SW_DEFINE_REGISTRATION_SLOT( getProviderSlot, void( RenderDiagnosticsPolicy& ), "RenderDiagnosticsProvider" )

        // 칸이 늘어 8 바이트를 넘으면 잠금 없는 원자 사본이 깨진다 — 여기서 멈추고 사본 모양(두 벌 버퍼 등)을 다시 정한다.
        static_assert( sizeof( RenderDiagnosticsPolicy ) <= sizeof( uint64 ), "RenderDiagnosticsPolicy outgrew the lock-free published copy" );
        static_assert( std::is_trivially_copyable<RenderDiagnosticsPolicy>::value, "RenderDiagnosticsPolicy must stay trivially copyable for the atomic copy" );
        static_assert( std::atomic<RenderDiagnosticsPolicy>::is_always_lock_free, "RenderDiagnosticsPolicy copy must be lock-free" );

        /** @brief 게임 스레드가 내보낸 정책 사본입니다. 다른 스레드(렌더 · 기록 워커 · 셰이더 컴파일)는 이것만 읽는다. 함수 정적이라 정적 초기화 중 등록에서도 선 뒤에 쓴다. */
        atomic<RenderDiagnosticsPolicy>& getPublishedPolicy()
        {
            static atomic<RenderDiagnosticsPolicy> s_published{ RenderDiagnosticsPolicy{} };
            return s_published;
        }
    } // namespace

    RenderDiagnosticsPolicy RenderDiagnostics::loadPublished()
    {
        return getPublishedPolicy().load( std::memory_order_acquire );
    }

    void RenderDiagnostics::publish()
    {
        // 엔진 서비스가 묶이기 전(정적 등록 · 명령줄 직후의 기동)은 게임 스레드 하나뿐이라 확인할 주인이 아직 없다.
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<TaskManager>().ensureMainThread();
        RenderDiagnosticsPolicy         policy{};
        const RenderDiagnosticsProvider provider = getProviderSlot().get();
        if ( provider != nullptr )
            provider( policy );
        getPublishedPolicy().store( policy, std::memory_order_release );
    }

    bool RenderDiagnostics::registerProvider( RenderDiagnosticsProvider provider )
    {
        const bool bAdded = getProviderSlot().setLogged( provider, "Render diagnostics provider", "RenderDiagnosticsProvider" ) == RegistrationResult::Added;
        if ( bAdded )
            publish(); // 기동 전 읽기가 채우는 쪽의 값을 보게 한다(명령줄 값은 엔진이 기동에서 다시 내보낸다)
        return bAdded;
    }

    void RenderDiagnostics::unregisterProvider( RenderDiagnosticsProvider provider )
    {
        if ( getProviderSlot().reset( provider ) )
            getPublishedPolicy().store( RenderDiagnosticsPolicy{}, std::memory_order_release ); // 떼인 쪽의 값을 남기지 않는다 — 기본값으로
    }
} // namespace sw
