#include "pch.h"

#include "Engine/Graphics/RHI/RHIRenderResource.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"

#include "Engine/Foundation/EngineParallel.h"

namespace sw
{
    SW_LOG_CALLER( "RHIRenderResource" );

    namespace
    {
        /**
         * @brief 레지스트리입니다. **소유하지 않습니다.** 통보할 대상의 주소만 압니다.
         * @details 첫 사용 시점에 정적 저장소에 한 번 세우고 **소멸자를 부르지 않습니다.** 함수 지역 static 객체로 두면, 처음 쓴 시점이
         *          엔진 하네스보다 늦을 때(퍼저의 정적 `TestHostRuntime` 이 기동한 뒤 첫 머티리얼) `exit()` 의 정적 소멸이 목록을 먼저 지우고,
         *          뒤이어 도는 엔진 정리나 늦게 소멸하는 리소스가 지워진 목록을 씁니다. 버킷 메모리는 마지막 리소스가 소멸할 때
         *          (`~RHIRenderResource` 가 빈 표를 대입) 돌아갑니다.
         * @note 리눅스는 공유 라이브러리와 실행 파일의 정적 소멸이 한 목록에서 섞여 돌아 이 순서가 드러나고, Windows 는 DLL 마다 따로 돌아 드러나지 않습니다.
         */
        struct RenderResourceRegistry
        {
            /** @brief 목록을 지킵니다. 목록과 같은 까닭으로 소멸자를 부르지 않습니다. */
            mutex                             _mutex;
            unordered_set<RHIRenderResource*> _uniqueSetResource;
        };

        /**
         * @brief 레지스트리 하나(잠금과 목록을 한 객체로)를 돌려줍니다.
         * @details 서비스 범위나 엔진 수명 범위(`ScopeOwned`)의 소유로 옮기지 않는 까닭: 소유자는 리소스 객체들 자신이다. 리소스는 생성자에서 오르고 소멸자에서 빠지며, 엔진을 다시 세워도
         *          정적 객체로 살아남는 리소스(기본 에셋)와 엔진 정리 뒤에 늦게 소멸하는 리소스가 있어, 이 표는 어느 범위보다 오래 살아야 한다(범위가 먼저 사라지면 살아남은 리소스가
         *          장치 해제 통보를 영영 받지 못한다). 그래서 잠금과 목록을 한 객체로 모아 함수가 둘이던 숨은 상태를 하나로 줄이고, 그 객체를 소멸하지 않는 저장소에 둔다.
         */
        RenderResourceRegistry& getRegistryInternal()
        {
            alignas( RenderResourceRegistry ) static uint8 s_arrStorage[sizeof( RenderResourceRegistry )];
            static RenderResourceRegistry&                 s_registry = *sw_placement_new( s_arrStorage ) RenderResourceRegistry();
            return s_registry;
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
                ScopedLock<mutex> lock{ getRegistryInternal()._mutex };
                listResource.reserve( getRegistryInternal()._uniqueSetResource.size() );
                for ( RHIRenderResource* pResource : getRegistryInternal()._uniqueSetResource )
                {
                    listResource.push_back( pResource );
                }
            }

            for ( RHIRenderResource* pResource : listResource )
            {
                if ( pResource == nullptr )
                    continue;
                {
                    ScopedLock<mutex> lock{ getRegistryInternal()._mutex };
                    // 앞선 통보를 처리하다 사라졌다. 사본에 남은 주소는 이미 남의 것이거나 없는 것이다.
                    if ( getRegistryInternal()._uniqueSetResource.count( pResource ) == 0 )
                        continue;
                }
                notify( pResource );
            }
        }
    } // namespace

    RHIRenderResource::RHIRenderResource()
    {
        ScopedLock<mutex> lock{ getRegistryInternal()._mutex };
        getRegistryInternal()._uniqueSetResource.insert( this );
    }

    RHIRenderResource::~RHIRenderResource()
    {
        ScopedLock<mutex>                  lock{ getRegistryInternal()._mutex };
        unordered_set<RHIRenderResource*>& registry = getRegistryInternal()._uniqueSetResource;
        registry.erase( this );
        // 마지막 리소스가 사라지면 버킷도 놓는다 — 기동 뒤 자란 표가 종료 누수 보고에 남지 않는다. 엔진 기본 에셋이 사는 동안은 비지 않으므로 실행 중에는 일어나지 않는다.
        if ( registry.empty() )
            registry = unordered_set<RHIRenderResource*>{};
    }

    bool RHIRenderResource::initRHI( IRHIDevice* )
    {
        // 기본은 아무것도 하지 않는다. 그릴 때 알아서 다시 초기화되는 리소스는 여기서 할 일이 없다.
        return true;
    }

    uint32 RHIRenderResource::getRegisteredCount()
    {
        ScopedLock<mutex> lock{ getRegistryInternal()._mutex };
        return static_cast<uint32>( getRegistryInternal()._uniqueSetResource.size() );
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
        // 1) 디바이스 없는 준비(텍스처 파일 읽기 · 해석)를 워커에 나눈다 - 예전에는 아래 통보 안에서 텍스처마다 디스크를 읽어 백엔드 교체가 텍스처 수만큼 직렬이었다.
        //    잠금을 잡고 도므로 그 사이 리소스가 생기거나 사라지지 않는다(준비는 리소스를 만들지 않는다).
        {
            ScopedLock<mutex>          lock{ getRegistryInternal()._mutex };
            vector<RHIRenderResource*> listResource( getRegistryInternal()._uniqueSetResource.begin(), getRegistryInternal()._uniqueSetResource.end() );
            struct PrepareJob
            {
                RHIRenderResource* const* _pResource{ nullptr };
                void                      runRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                        _pResource[index]->prepareRHI();
                }
            };
            PrepareJob job{ listResource.data() };
            engine::runParallelByCost( static_cast<uint32>( listResource.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &PrepareJob::runRange, &job ) );
        }
        // 2) GPU 생성은 디바이스를 쓰는 이 스레드에서 차례로 한다. 실패는 각 리소스가 자기 코드에서 로그로 남긴다. 여기서 세어 봐야 어느 리소스인지 모르는 한 줄만 늘어난다.
        broadcastInternal( [pDevice]( RHIRenderResource* pResource )
        {
            (void)pResource->initRHI( pDevice ); // 실패는 리소스가 자기 코드에서 로그로 남긴다(위 설명)
        } );
    }
} // namespace sw
