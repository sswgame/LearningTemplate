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

    namespace
    {
        /**
         * @brief 등록부 전체에 통보를 밀어 넣습니다. 통보 도중 목록이 바뀌어도 안전합니다.
         * @details 사본을 떠서 도는 것만으로는 부족하다. 머티리얼이 자기 GPU 자원을 놓으면서 빌려 온 텍스처를
         *          돌려주고, 그 참조가 마지막이면 `Texture2D` 가 **그 자리에서 파괴된다** — 사본에 남은 주소는
         *          그 순간 댕글링이다. 그래서 부르기 직전에 "아직 등록부에 있나" 를 잠금 아래에서 다시 묻는다.
         *          파괴자가 등록부에서 자기를 지우므로(같은 잠금), 이 질문은 정확하다.
         */
        template <typename FnNotify>
        void broadcastInternal( FnNotify&& notify )
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
                if ( pResource == nullptr )
                    continue;
                {
                    std::scoped_lock<mutex> lock{ registryMutexInternal() };
                    // 앞선 통보를 처리하다 사라졌다 — 사본에 남은 주소는 이미 남의 것이거나 없는 것이다.
                    if ( registryInternal().count( pResource ) == 0 )
                        continue;
                }
                notify( pResource );
            }
        }
    } // namespace

    bool RHIRenderResource::initRhi( IRHIDevice* )
    {
        // 기본은 아무것도 하지 않는다 — 그릴 때 알아서 다시 올라가는 리소스는 여기 낄 이유가 없다.
        return true;
    }

    void RHIRenderResource::releaseAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            pResource->releaseRhi( pDevice );
        } );
    }

    void RHIRenderResource::forgetAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            pResource->forgetRhi( pDevice );
        } );
    }

    void RHIRenderResource::initAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        // 실패는 각자가 자기 자리에서 로그로 남긴다 — 여기서 세어 봐야 어느 리소스인지 모르는 한 줄만 는다.
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            (void)pResource->initRhi( pDevice );
        } );
    }

    uint32 RHIRenderResource::getRegisteredCount()
    {
        std::scoped_lock<mutex> lock{ registryMutexInternal() };
        return static_cast<uint32>( registryInternal().size() );
    }
} // namespace sw
