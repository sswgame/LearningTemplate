#include "pch.h"

#include "Engine/Graphics/RHI/RHIRenderResource.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"

namespace sw
{
    SW_LOG_CALLER( "RHIRenderResource" );

    namespace
    {
        /**
         * @brief 등록부. **소유하지 않는다** — 통보할 대상의 주소만 안다.
         * @details 함수 지역 정적이라 첫 사용 시점에 만들어진다. 전역 객체 초기화 순서에 기대면, 정적 수명 객체가
         *          이 목록보다 먼저/나중에 파괴되며 목록이 없는 채로 등록 해제를 부르는 일이 생긴다.
         */
        unordered_set<RHIRenderResource*>& registryInternal()
        {
            static unordered_set<RHIRenderResource*> s_registry;
            return s_registry;
        }

        mutex& registryMutexInternal()
        {
            static mutex s_mutex;
            return s_mutex;
        }
    } // namespace

    RHIRenderResource::RHIRenderResource()
    {
        std::scoped_lock<mutex> lock{ registryMutexInternal() };
        registryInternal().insert( this );
    }

    RHIRenderResource::~RHIRenderResource()
    {
        std::scoped_lock<mutex> lock{ registryMutexInternal() };
        registryInternal().erase( this );
    }

    void RHIRenderResource::releaseAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;

        // 통보 중에 목록이 바뀔 수 있다(리소스를 놓다가 다른 리소스가 파괴되는 경우). 사본을 만들어 돌면
        // 잠금을 든 채로 남의 코드를 부르지 않아도 되고, 재진입 교착도 생기지 않는다.
        vector<RHIRenderResource*> listResource;
        {
            std::scoped_lock<mutex> lock{ registryMutexInternal() };
            listResource.reserve( registryInternal().size() );
            for ( RHIRenderResource* pResource : registryInternal() )
                listResource.push_back( pResource );
        }

        for ( RHIRenderResource* pResource : listResource )
        {
            if ( pResource != nullptr )
                pResource->releaseRhi( pDevice );
        }
    }

    void RHIRenderResource::forgetAll()
    {
        vector<RHIRenderResource*> listResource;
        {
            std::scoped_lock<mutex> lock{ registryMutexInternal() };
            listResource.reserve( registryInternal().size() );
            for ( RHIRenderResource* pResource : registryInternal() )
                listResource.push_back( pResource );
        }

        for ( RHIRenderResource* pResource : listResource )
        {
            if ( pResource != nullptr )
                pResource->forgetRhi();
        }
    }

    uint32 RHIRenderResource::getRegisteredCount()
    {
        std::scoped_lock<mutex> lock{ registryMutexInternal() };
        return static_cast<uint32>( registryInternal().size() );
    }
} // namespace sw
