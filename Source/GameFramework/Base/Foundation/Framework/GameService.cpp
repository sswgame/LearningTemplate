#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Core/Container/map.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Foundation/Data/GameDataCache.h"
#include "GameFramework/GameFrameworkExports.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <intrin.h>
    #define SW_GAMESERVICE_CALLER_ADDRESS() _ReturnAddress()
#else
    #define SW_GAMESERVICE_CALLER_ADDRESS() __builtin_return_address( 0 )
#endif

SW_LOG_CALLER( "GameService" );
namespace sw
{
    namespace
    {
        /**
         * @struct LocalServiceEntry
         * @brief 게임 로컬 서비스 한 칸입니다. 서비스 포인터와 그것을 건 모듈 이미지(소유자 태그)를 함께 둡니다.
         */
        struct LocalServiceEntry
        {
            void*       _pService{ nullptr };    ///< 건 인스턴스(모듈이 만든 것)
            const void* _pOwnerImage{ nullptr }; ///< 건 쪽 이미지의 시작 주소. 찾지 못하면 호출 지점 주소 자체
        };

        /**
         * @class LocalServiceTable
         * @brief 게임 로컬 서비스 표입니다. 모듈 이미지가 내려가기 전에 그 이미지가 건 칸을 일괄 회수합니다(`IModuleUnloadListener`).
         * @details 옛 인스턴스가 `onShutdown` 에서 `unbindLocalService` 를 빠뜨리면 칸이 내려간 인스턴스를 가리킨 채 남고, 새 인스턴스의 bind 가
         *          겹침 규칙에 걸려 거절된다. 이 표는 GameFramework.dll 의 정적이라 vtable 이 키트 · 게임보다 오래 사는 이미지에 있다.
         */
        class LocalServiceTable final : public IModuleUnloadListener
        {
        public:
            map<uint64, LocalServiceEntry> _mapEntry{};

            const utf8* getModuleUnloadListenerName() const override { return "game local services"; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                uint32 releasedCount{ 0 };
                for ( auto it = _mapEntry.begin(); it != _mapEntry.end(); )
                {
                    if ( it->second._pOwnerImage >= pBegin && it->second._pOwnerImage < pEnd )
                    {
                        it = _mapEntry.erase( it );
                        ++releasedCount;
                    }
                    else
                    {
                        ++it;
                    }
                }
                return releasedCount;
            }
        };

        ModuleService     s_gameService{};
        LocalServiceTable s_localServiceTable{};

        /** @brief 호출 지점 @p pCallerAddress 를 품은 이미지의 시작 주소를 돌려줍니다. 찾지 못하면 그 주소 자체입니다(범위 비교는 그대로 맞다). */
        const void* findOwnerImage( const void* pCallerAddress )
        {
            const void* pBegin = nullptr;
            const void* pEnd   = nullptr;
            if ( ModuleImageUtil::findLoadedImageRange( pCallerAddress, pBegin, pEnd ) && pBegin != nullptr )
                return pBegin;
            return pCallerAddress;
        }
    } // namespace

    namespace game
    {
        SW_GAMESERVICE_API void bindGameService( const ModuleService& service )
        {
            s_gameService = service;
            // 데이터 표 캐시(상호작용 · 원소 규칙)를 호스트의 에셋 캐시 레지스트리에 올린다 — 핫 리로드 · 비우기 · 진단이 그 레지스트리를 훑는다.
            GameDataCacheRegistry::attach( getService<AssetManager>() );
        }

        SW_GAMESERVICE_API void unbindGameService()
        {
            GameDataCacheRegistry::detach();
            s_gameService = {};
            s_localServiceTable._mapEntry.clear();
        }

        SW_GAMESERVICE_API bool isSceneManagerServiceBound()
        {
            return s_gameService.arrServices[sw::internal::toRawServiceID( sw::internal::ModuleServiceID::SceneManager )] != nullptr;
        }

        namespace internal
        {
            SW_GAMESERVICE_API void bindRawLocalService( uint64 typeHash, void* pService )
            {
                map<uint64, LocalServiceEntry>& mapEntry = s_localServiceTable._mapEntry;
                if ( pService == nullptr )
                {
                    mapEntry.erase( typeHash );
                    return;
                }
                const auto it = mapEntry.find( typeHash );
                if ( it != mapEntry.end() && it->second._pService != pService )
                {
                    // 같은 타입을 둘이 걸면 나중 것이 앞 것을 소리 없이 덮는다 — 키트 둘이 같은 카탈로그 타입을 서비스로 쓰는 조립이다.
                    // 옛 이미지가 풀지 않고 내려간 경우는 여기 오지 않는다(언로드 훑기가 그 칸을 이미 회수했다).
                    SW_LOG_ERROR( "Game local service %# is already bound to another instance - unbind it first (two kits share one service type)", typeHash );
                    return;
                }
                // 호출자는 헤더의 인라인 템플릿(`bindLocalService`)이라 반환 주소가 건 모듈의 이미지 안에 있다 — 그 이미지가 소유자다.
                mapEntry[typeHash] = LocalServiceEntry{ pService, findOwnerImage( SW_GAMESERVICE_CALLER_ADDRESS() ) };
            }

            SW_GAMESERVICE_API void* getRawLocalService( uint64 typeHash )
            {
                const map<uint64, LocalServiceEntry>& mapEntry = s_localServiceTable._mapEntry;
                const auto                            it       = mapEntry.find( typeHash );
                return it != mapEntry.end() ? it->second._pService : nullptr;
            }

            SW_GAMESERVICE_API void* getRawService( sw::internal::ModuleServiceID id )
            {
                const uint32 rawID = sw::internal::toRawServiceID( id );
                if ( rawID >= sw::internal::kModuleServiceCount )
                    return nullptr;
                return const_cast<void*>( s_gameService.arrServices[rawID] );
            }
        } // namespace internal
    } // namespace game
} // namespace sw
