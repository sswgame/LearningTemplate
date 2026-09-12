#include "pch.h"

#include "Engine/Module/ModuleHandleProvider.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 등록된 제공자. 지연 로드 훅은 **로더 락 안에서** 불리므로 잠금을 쓰지 않는다.
         * @details 훅은 DLL 로드 중(로더 락 보유)에 실행된다. 여기서 뮤텍스를 잡으면 다른 스레드가 로더 락을 기다리는
         *          동안 교착이 난다. 포인터 하나를 원자적으로 읽고 쓰는 것으로 충분하다 — 등록·해제는 App 이 프레임
         *          바깥에서 한 번씩만 한다.
         */
        atomic<IModuleHandleProvider*> _s_pProvider{ nullptr };
    } // namespace

    namespace engine
    {
        void setModuleHandleProvider( IModuleHandleProvider* pProvider )
        {
            _s_pProvider.store( pProvider, std::memory_order_release );
        }

        IModuleHandleProvider* getModuleHandleProvider()
        {
            return _s_pProvider.load( std::memory_order_acquire );
        }
    } // namespace engine
} // namespace sw
