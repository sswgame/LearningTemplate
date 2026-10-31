/**
 * @file GameDataCache.h
 * @brief 게임 데이터 테이블(상호작용 테이블, 원소 규칙 테이블)의 캐시와, 캐시 하나를 가진 모듈 서브시스템(`GameDataCacheSubsystem`)을 정의합니다.
 * @details 표는 경로마다 한 번 읽어 여러 컴포넌트가 나눠 쓴다.
 *          캐시는 GameFramework 의 모듈 서브시스템이 소유한다(`SW_MODULE_SUBSYSTEM( Engine, … )` — 엔진 범위 아래 GameFramework 의 자식 범위). 서브시스템이 서면
 *          엔진의 에셋 캐시 레지스트리에 오르고(`AssetManager::registerAssetCache`) 종료가 내린다 — 엔진 기동 단계가 내려가거나 모듈이 내려갈 때 함께 사라진다.
 *          그래서 에디터 핫 리로드가 파일을 고친 표를 종류 이름으로 찾아 다시 읽을 수 있고(`reload`), 종료할 때의 비우기와 진단도 엔진 캐시와 같은 길을 탄다.
 *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이 캐시 · 목록을 들고 게임 서비스 묶기가 레지스트리를 맞췄다(언리얼 `UEngineSubsystem` 이 데이터 레지스트리를 갖는 자리).
 *
 *          다시 읽은 테이블은 **새 객체**로 항목을 바꾸고, 옛 객체는 캐시가 사라질 때까지 살려 둔다.
 *          표 안의 정의를 날 포인터로 든 컴포넌트가 그 프레임에 옛 것을 읽어도 죽은 메모리를 밟지 않게 하려는 것이다.
 *          쓰는 쪽은 다시 읽은 횟수(`getReloadCount`)가 바뀌면 자기 정의를 다시 찾는다. UE 의 `UAssetManager` 가 주 에셋을 다시 읽는 방식과 같다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Asset/Cache/IAssetCache.h"
#include "Engine/FileSystem/AssetID.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct GameDataCacheRegistration
     * @brief 데이터 캐시를 엔진의 에셋 캐시 레지스트리에 올리는 한 곳입니다(`GameDataCacheSubsystem` 의 기동이 부른다 — 템플릿이 에셋 매니저 헤더를 끌지 않게 .cpp 에 둔다).
     */
    struct SW_GF_API GameDataCacheRegistration
    {
        /**
         * @brief @p cache 를 에셋 매니저의 레지스트리에 올리고 그 등록을 @p outRegistration 에 담습니다(@p pReload 는 핫 리로드 함수 — 배포는 nullptr).
         * @return 에셋 매니저가 없으면 오류를 남기고 false 입니다(올릴 곳 없이 서지 않는다).
         */
        [[nodiscard]] static bool registerWithAssetManager( IAssetCache& cache, AssetCacheReloadFunction pReload, Subscription& outRegistration );
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameDataCache
     * @brief 경로마다 테이블 하나를 보관하는 캐시입니다. 테이블 타입은 `bool loadFromAsset( string_view )` 를 가져야 합니다.
     * @tparam TableType 기본 생성되는 데이터 표 타입입니다.
     * @details 읽지 못한 경로도 빈 항목으로 남긴다. 쓰는 쪽마다 같은 오류를 다시 내지 않게 하려는 것이며, 파일을 고치면 `reload` 가 그 항목을 채운다.
     */
    template <typename TableType>
    class GameDataCache final : public IAssetCache
    {
    public:
        /** @param pKindName 레지스트리의 종류 이름입니다. 정적 문자열이며, 에디터 핫 리로드 경로 표가 이 이름으로 찾습니다. */
        explicit GameDataCache( const utf8* pKindName )
            : IAssetCache{ CacheTracking::counted( pKindName ) } // 캐시는 모듈 서브시스템이 소유해 엔진을 다시 세우면 다시 만들어진다 — 같은 이름의 칸을 이어 쓴다
            , _pKindName{ pKindName }
            , _mutex{}
            , _mapTable{}
            , _listRetired{}
            , _reloadCount{ 0 }
        {
        }

        ~GameDataCache() override = default;

        GameDataCache( const GameDataCache& )            = delete;
        GameDataCache& operator=( const GameDataCache& ) = delete;

        /**
         * @brief 경로의 표를 반환합니다. 처음이면 읽으며, 읽지 못했으면 nullptr 입니다. 여러 스레드에서 불려도 됩니다.
         * @details 다시 읽거나 비워도 옛 표는 `kRetainGenerations` 번 더 다시 읽을 때까지(또는 캐시가 사라질 때까지) 남는다. 그보다 오래 쥐려면 `getReloadCount` 가 바뀔 때 다시 찾아야 한다.
         */
        const TableType* find( string_view path )
        {
            const string      key = makeKey( path );
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapTable.find( key );
            if ( iter != _mapTable.end() )
            {
                getCacheStats().recordHit(); // 읽지 못한 경로를 기억한 빈 항목도 적중이다(같은 오류를 다시 읽지 않는다)
                return iter->second.get();
            }
            getCacheStats().recordMiss();
            unique_ptr<TableType> table = make_unique<TableType>();
            if ( table->loadFromAsset( toAssetID( path ) ) == false )
                table.reset();
            const TableType* pTable = table.get();
            _mapTable[key]          = std::move( table );
            return pTable;
        }

        /** @brief 이 캐시가 표를 다시 읽은 횟수입니다. 쓰는 쪽은 값이 바뀌면 정의를 다시 찾아야 합니다. */
        uint32 getReloadCount() const { return _reloadCount.load( std::memory_order_acquire ); }

        const utf8* getAssetKindName() const override { return _pKindName; }

        bool isCached( string_view relativePath ) const override
        {
            ScopedLock<mutex> lock{ _mutex };
            return _mapTable.find( makeKey( relativePath ) ) != _mapTable.end();
        }

        /** @brief 읽은 적 있는 경로면 새 테이블로 다시 읽어 항목을 바꿉니다. 읽지 못하면 옛 테이블을 그대로 두고 오류를 남깁니다. 틱 밖(게임 스레드)에서 불러야 합니다. */
        void reload( const AssetID& assetID, IRHIDevice* pDevice )
        {
            const string_view relativePath = assetID.view();
            (void)pDevice;
            const string key = makeKey( relativePath );
            {
                ScopedLock<mutex> lock{ _mutex };
                if ( _mapTable.find( key ) == _mapTable.end() )
                    return; // 아무도 읽지 않았으므로 다음 `find` 가 새 내용을 읽는다.
            }
            unique_ptr<TableType> fresh = make_unique<TableType>();
            if ( fresh->loadFromAsset( assetID ) == false )
            {
                SW_LOG_ERROR( "[GameDataCache] %# could not be reloaded - keeping the previous table", key );
                return;
            }
            ScopedLock<mutex>      lock{ _mutex };
            unique_ptr<TableType>& slot       = _mapTable[key];
            const uint32           generation = _reloadCount.load( std::memory_order_relaxed ) + 1; // 이 리로드가 만드는 세대
            if ( slot != nullptr )
                _listRetired.push_back( RetiredTable{ std::move( slot ), generation } );
            slot = std::move( fresh );
            _reloadCount.store( generation, std::memory_order_release );
            // 세대가 충분히 지난 옛 표만 놓는다 — 쓰는 쪽은 세대가 바뀌면 정의를 다시 찾으므로 그 사이 한 프레임이면 옛 포인터를 버린다.
            VectorUtil::removeIf( _listRetired, [generation]( const RetiredTable& retired )
            { return generation - retired._generation >= kRetainGenerations; } );
        }

        /** @brief 읽어 둔 테이블의 수입니다. 읽지 못한 빈 항목은 세지 않습니다. */
        size_t getCachedCount() const override
        {
            ScopedLock<mutex> lock{ _mutex };
            size_t            count{ 0 };
            for ( const auto& [key, table] : _mapTable )
            {
                if ( table != nullptr )
                    ++count;
            }
            return count;
        }

        /**
         * @brief 표를 비웁니다. 종료하거나 재초기화할 때 부르며, 다음 `find` 는 파일을 새로 읽습니다.
         * @details 앞서 반환한 표는 버리지 않고 캐시가 사라질 때(모듈이 내릴 때)까지 둔다. 종료 순서상 비우기 뒤에도 정의를 날 포인터로 든 컴포넌트가 남아 있을 수 있기 때문이다.
         */
        void clear() override
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( auto& [key, table] : _mapTable )
            {
                if ( table != nullptr )
                    _listRetired.push_back( RetiredTable{ std::move( table ), _reloadCount.load( std::memory_order_relaxed ) } );
            }
            _mapTable.clear();
            getCacheStats().recordClear();
        }

    private:
        /** @brief 테이블의 키를 만듭니다. 핫 리로드가 넘기는 경로와 같은 항목을 찾도록 구분자를 맞춥니다. */
        static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }
        /** @brief 캐시 키(찾기 · 리로드가 넘긴 경로)를 로더에 넘길 리소스 id 로 바꾸는 한 곳입니다. 이 캐시의 키는 리소스 id 만 담는다. */
        static AssetID toAssetID( string_view path ) { return AssetID{ path }; }

        const utf8*                                            _pKindName;
        mutable mutex                                          _mutex;
        unordered_map<string, unique_ptr<TableType>> _mapTable SW_GUARDED_BY( _mutex );
        /// 다시 읽거나 비우기 전의 테이블. 원시 포인터로 참조하는 쪽이 있을 수 있어 `kRetainGenerations` 번 더 다시 읽을 때까지(또는 캐시가 사라질 때까지) 산다.
        struct RetiredTable
        {
            unique_ptr<TableType> _pTable;
            uint32                _generation; ///< 내려놓을 때의 `_reloadCount`(리로드면 그 리로드의 세대)
        };
        static constexpr uint32 kRetainGenerations = 4; ///< 옛 표를 이 횟수만큼의 리로드 동안 둔다. 리로드는 사람 속도라 쓰는 쪽이 그 사이 다시 찾는다

        vector<RetiredTable> _listRetired SW_GUARDED_BY( _mutex );
        atomic<uint32>                    _reloadCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameDataCacheSubsystem
     * @brief 데이터 캐시 하나를 가진 모듈 서브시스템의 바탕입니다. 기동이 캐시를 에셋 캐시 레지스트리에 올리고 종료가 내립니다.
     * @tparam TableType 캐시가 보관하는 데이터 표 타입입니다.
     * @details 표마다 `.cpp` 에서 상속해 이름(`kSubsystemName` · `getSubsystemName`)을 적고 `SW_MODULE_SUBSYSTEM( Engine, 그 타입, 그 타입 )` 으로 등록한다.
     *          쓰는 쪽(컴포넌트)은 `game::getSubsystem<그 타입>().getCache()` 로 찾는다.
     */
    template <typename TableType>
    class GameDataCacheSubsystem : public ISubsystem
    {
    public:
        /** @param pKindName 레지스트리의 종류 이름입니다(정적 문자열). */
        explicit GameDataCacheSubsystem( const utf8* pKindName )
            : _cache{ pKindName }
            , _registration{}
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            const bool bRegistered = GameDataCacheRegistration::registerWithAssetManager( _cache, SW_ASSET_CACHE_RELOAD_FUNCTION( GameDataCache<TableType> ), _registration );
            return bRegistered ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
        }
        /** @brief 레지스트리에서 내립니다. 표는 서브시스템이 사라질 때 함께 사라진다(정의를 날 포인터로 든 컴포넌트는 그 전에 내려간다). */
        void shutdownSubsystem() override { _registration.reset(); }

        /** @brief 경로마다 한 번 읽어서 공유하는 캐시입니다. */
        GameDataCache<TableType>& getCache() { return _cache; }

    private:
        GameDataCache<TableType> _cache;
        Subscription             _registration; ///< 에셋 캐시 레지스트리 등록(캐시보다 먼저 풀린다 — 나중에 선언)
    };
} // namespace sw
