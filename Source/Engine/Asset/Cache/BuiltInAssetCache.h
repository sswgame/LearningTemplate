/**
 * @file BuiltInAssetCache.h
 * @brief 엔진 기능 폴더가 자기 에셋 캐시를 `AssetManager` 에 등록하는 창구입니다. 캐시 클래스의 .cpp 에 `SW_BUILT_IN_ASSET_CACHE` 한 줄을 둡니다.
 * @details `Resource` 는 레지스트리만 갖고 기능 캐시(Material · Mesh · Skeleton · Fracture …)의 타입을 모릅니다. 기능 폴더가 정적 초기화에서 이 표에
 *          만드는 함수를 올리고, `AssetManager` 는 생성자에서 표를 훑어 캐시를 만들고 소유합니다 — 의존은 기능 → Resource 한 방향입니다.
 *          언리얼 서브시스템(모듈이 자기 타입을 올리고 엔진이 인스턴스를 만든다) · 고도 `register_types` 의 `add_resource_format_loader` 와 같은 모양입니다.
 *
 *          - **소유 캐시**(`SW_BUILT_IN_ASSET_CACHE`): 매니저마다 하나씩 만들고 매니저가 지웁니다.
 *          - **공유 캐시**(`SW_BUILT_IN_SHARED_ASSET_CACHE`): Engine 이미지에 있고 엔진 수명 범위가 소유하는 객체(코드로 짓는 값 표)를 빌려 올립니다. 매니저보다 오래 삽니다.
 *
 * @note 표는 Engine 이미지 안에서만 채웁니다. 모듈(게임 · 키트)의 캐시는 `AssetManager::registerAssetCache` 로 올리고 내려가기 전에 그 구독을 놓습니다 —
 *       이 표의 항목은 내리지 않으므로, 모듈이 쓰면 언로드 뒤 죽은 함수를 가리킵니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/IAssetCache.h"

namespace sw
{
    /** @brief 소유 캐시를 하나 만듭니다. */
    using CreateAssetCacheFunction = unique_ptr<IAssetCache> ( * )();
    /** @brief 프로세스에 하나인 공유 캐시를 빌려 줍니다. */
    using GetSharedAssetCacheFunction = IAssetCache& (*)();

    /** @struct BuiltInAssetCacheRegistration @brief 등록 줄 하나 — 둘 중 하나만 채웁니다. 등록한 .cpp 의 정적 저장소에 있습니다. */
    struct BuiltInAssetCacheRegistration
    {
        CreateAssetCacheFunction    _pCreate;    ///< 소유 캐시면 만드는 함수, 아니면 nullptr
        GetSharedAssetCacheFunction _pGetShared; ///< 공유 캐시면 빌려 주는 함수, 아니면 nullptr
        AssetCacheReloadFunction    _pReload;    ///< 핫 리로드 함수(배포 · 다시 읽을 파일이 없는 캐시는 nullptr)
    };
} // namespace sw

namespace sw
{
    /**
     * @class BuiltInAssetCacheRegistry
     * @brief 엔진 내장 캐시의 등록표입니다. 정적 초기화에서 채우고 `AssetManager` 생성자가 읽습니다.
     * @details 등록 순서는 링크 순서라 정해져 있지 않습니다. 매니저는 만든 뒤 종류 이름 순으로 늘어놓아 비우기 순서를 고정합니다.
     */
    class SW_API BuiltInAssetCacheRegistry
    {
    public:
        /** @brief 등록 줄을 더합니다. 같은 줄을 두 번 올리면 무시합니다. */
        static void add( const BuiltInAssetCacheRegistration* pRegistration );
        /** @brief 지금까지 올라온 등록 줄입니다(올린 순서). */
        static const vector<const BuiltInAssetCacheRegistration*>& getAll();
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_BUILT_IN_ASSET_CACHE` 가 두는 정적 등록자입니다. */
    struct SW_API BuiltInAssetCacheRegistrar
    {
        explicit BuiltInAssetCacheRegistrar( const BuiltInAssetCacheRegistration* pRegistration );
        BuiltInAssetCacheRegistrar( const BuiltInAssetCacheRegistrar& )            = delete;
        BuiltInAssetCacheRegistrar& operator=( const BuiltInAssetCacheRegistrar& ) = delete;
    };

    /** @brief 소유 캐시를 만드는 함수입니다. 캐시 타입이 완전한 자리(그 캐시의 .cpp)에서 실체화합니다. */
    template <typename TCache>
    unique_ptr<IAssetCache> createBuiltInAssetCache()
    {
        // 기반 포인터로 지우므로 정렬 할당을 쓰는 타입이면 해제가 어긋난다.
        static_assert( kUsesAlignedAllocation<TCache> == false, "built-in asset cache must not be over-aligned" );
        return make_unique<TCache>();
    }
} // namespace sw

/**
 * @brief 소유 캐시 @p Type 을 내장 캐시로 등록합니다(캐시의 .cpp, 파일 범위, `namespace sw` 안). 매니저마다 하나씩 만듭니다.
 */
#define SW_BUILT_IN_ASSET_CACHE( Type ) SW_BUILT_IN_ASSET_CACHE_WITH_ID( Type, Type )

/**
 * @brief 이름이 식별자가 아닌 소유 캐시 @p Type(템플릿 인스턴스 `SharedAssetCache<Skeleton>` 등)을 등록합니다. @p id 는 정적 이름을 만드는 식별자입니다.
 */
#define SW_BUILT_IN_ASSET_CACHE_WITH_ID( id, Type )                                                                                \
    static const ::sw::BuiltInAssetCacheRegistration sw_builtInAssetCache_##id{ &::sw::createBuiltInAssetCache<Type>,              \
                                                                                nullptr, SW_ASSET_CACHE_RELOAD_FUNCTION( Type ) }; \
    static const ::sw::BuiltInAssetCacheRegistrar    sw_builtInAssetCacheRegistrar_##id                                            \
    {                                                                                                                              \
        &sw_builtInAssetCache_##id                                                                                                 \
    }

/**
 * @brief 공유 캐시를 내장 캐시로 등록합니다. @p id 는 정적 이름을 만드는 식별자, @p pGetShared 는 `IAssetCache&` 를 돌려주는 함수입니다.
 *        공유 캐시는 코드로 짓는 값 표라 다시 읽을 파일이 없다(리로드 함수 없음).
 */
#define SW_BUILT_IN_SHARED_ASSET_CACHE( id, pGetShared )                                                        \
    static const ::sw::BuiltInAssetCacheRegistration sw_builtInAssetCache_##id{ nullptr, pGetShared, nullptr }; \
    static const ::sw::BuiltInAssetCacheRegistrar    sw_builtInAssetCacheRegistrar_##id                         \
    {                                                                                                           \
        &sw_builtInAssetCache_##id                                                                              \
    }
