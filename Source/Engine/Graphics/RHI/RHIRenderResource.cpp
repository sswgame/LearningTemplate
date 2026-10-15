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
         * @brief 레지스트리입니다. **소유하지 않습니다.** 통보할 대상의 주소만 압니다.
         * @details 첫 사용 시점에 정적 저장소에 한 번 세우고 **소멸자를 부르지 않습니다.** 함수 지역 static 객체로 두면, 처음 쓴 시점이
         *          엔진 하네스보다 늦을 때(퍼저의 정적 `TestHostRuntime` 이 기동한 뒤 첫 머티리얼) `exit()` 의 정적 소멸이 목록을 먼저 지우고,
         *          뒤이어 도는 엔진 정리(`releaseRegistryStorage`)나 늦게 소멸하는 리소스가 지워진 목록을 씁니다. 버킷 메모리는
         *          `releaseRegistryStorage`(엔진 종료)가 돌려줍니다.
         * @note 리눅스는 공유 라이브러리와 실행 파일의 정적 소멸이 한 목록에서 섞여 돌아 이 순서가 드러나고, Windows 는 DLL 마다 따로 돌아 드러나지 않습니다.
         */
        unordered_set<RHIRenderResource*>& registryInternal()
        {
            using Registry = unordered_set<RHIRenderResource*>;
            alignas( Registry ) static uint8 s_arrStorage[sizeof( Registry )];
            static Registry&                 s_registry = *sw_placement_new( s_arrStorage ) Registry();
            return s_registry;
        }

        /** @brief 레지스트리 잠금입니다. 목록과 같은 까닭으로 소멸자를 부르지 않습니다. */
        mutex& registryMutexInternal()
        {
            alignas( mutex ) static uint8 s_arrStorage[sizeof( mutex )];
            static mutex&                 s_mutex = *sw_placement_new( s_arrStorage ) mutex();
            return s_mutex;
        }

        /**
         * @brief 레지스트리 전체에 통보를 밀어 넣습니다. 통보 도중 목록이 바뀌어도 안전합니다.
         * @details 사본을 떠서 도는 것만으로는 부족합니다. 머티리얼이 자기 GPU 자원을 놓으면서 빌려 온 텍스처를
         *          돌려주고, 그 참조가 마지막이면 `Texture2D` 가 **그 자리에서 파괴됩니다.** 사본에 남은 주소는
         *          그 순간 댕글링입니다. 그래서 부르기 직전에 "아직 레지스트리에 있나" 를 잠금 아래에서 다시 묻습니다.
         *          파괴자가 레지스트리에서 자기를 지우므로(같은 잠금), 이 질문은 정확합니다.
         */
        template <typename FnNotify>
        void broadcastInternal( FnNotify&& notify )
        {
            vector<RHIRenderResource*> listResource;
            {
                std::scoped_lock<mutex> lock{ registryMutexInternal() };
                listResource.reserve( registryInternal().size() );
                for ( RHIRenderResource* pResource : registryInternal() )
                {
                    listResource.push_back( pResource );
                }
            }

            for ( RHIRenderResource* pResource : listResource )
            {
                if ( pResource == nullptr )
                    continue;
                {
                    std::scoped_lock<mutex> lock{ registryMutexInternal() };
                    // 앞선 통보를 처리하다 사라졌다. 사본에 남은 주소는 이미 남의 것이거나 없는 것이다.
                    if ( registryInternal().count( pResource ) == 0 )
                        continue;
                }
                notify( pResource );
            }
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

    bool RHIRenderResource::initRHI( IRHIDevice* )
    {
        // 기본은 아무것도 하지 않는다. 그릴 때 알아서 다시 올라가는 리소스는 여기 낄 이유가 없다.
        return true;
    }

    bool RHIRenderResource::releaseRegistryStorage()
    {
        std::scoped_lock<mutex> lock{ registryMutexInternal() };
        if ( registryInternal().empty() == false )
            return false;
        registryInternal() = unordered_set<RHIRenderResource*>{};
        return true;
    }

    void RHIRenderResource::releaseAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            pResource->releaseRHI( pDevice );
        } );
    }

    void RHIRenderResource::forgetAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            pResource->forgetRHI( pDevice );
        } );
    }

    void RHIRenderResource::initAllFor( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        // 실패는 각자가 자기 자리에서 로그로 남긴다. 여기서 세어 봐야 어느 리소스인지 모르는 한 줄만 는다.
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            (void)pResource->initRHI( pDevice );
        } );
    }
} // namespace sw
