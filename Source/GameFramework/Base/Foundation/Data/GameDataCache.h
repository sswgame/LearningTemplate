/**
 * @file GameDataCache.h
 * @brief 게임 데이터 테이블(상호작용 테이블, 원소 규칙 테이블)의 캐시와, 그 캐시들을 에셋 캐시 레지스트리에 등록하는 목록을 정의합니다.
 * @details 표는 경로마다 한 번 읽어 여러 컴포넌트가 나눠 쓴다.
 *          캐시는 GameFramework 모듈의 객체이고 엔진 수명 범위(`ScopeOwned`)가 소유한다 — 엔진 종료 때나 모듈이 내려갈 때 사라진다.
 *          게임 서비스가 묶이면(`game::bindGameService`, 서비스에 `AssetManager` 가 있을 때) 레지스트리에 오르고, 풀리면(`unbindGameService`) 내려간다.
 *          그래서 에디터 핫 리로드가 파일을 고친 표를 종류 이름으로 찾아 다시 읽을 수 있고(`reload`), 종료할 때의 비우기와 진단도 엔진 캐시와 같은 길을 탄다.
 *          모듈이 해제하지 않고 사라지면 `AssetManager::onModuleUnloading` 이 이미지 안의 캐시를 정리한다.
 *
 *          다시 읽은 테이블은 **새 객체**로 항목을 바꾸고, 옛 객체는 캐시가 사라질 때까지 살려 둔다.
 *          표 안의 정의를 날 포인터로 든 컴포넌트가 그 프레임에 옛 것을 읽어도 죽은 메모리를 밟지 않게 하려는 것이다.
 *          쓰는 쪽은 다시 읽은 횟수(`getReloadCount`)가 바뀌면 자기 정의를 다시 찾는다. UE 의 `UAssetManager` 가 주 에셋을 다시 읽는 방식과 같다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AssetManager;

    /**
     * @struct GameDataCacheRegistry
     * @brief GameFramework 의 데이터 캐시 목록과, 그것을 등록한 에셋 매니저입니다. 캐시가 생기거나 서비스가 바인딩되고 해제될 때 레지스트리를 맞춥니다.
     */
    struct SW_GF_API GameDataCacheRegistry
    {
        /** @brief 캐시를 목록에 넣고, 묶인 매니저가 있으면 그 레지스트리에 올립니다. 캐시 생성자가 부릅니다. */
        static void registerCache( IAssetCache* pCache );
        /** @brief 캐시를 목록에서 뺍니다. 캐시 소멸자가 부릅니다. 프로세스 끝에는 매니저가 이미 사라졌을 수 있으므로 매니저는 건드리지 않습니다. */
        static void unregisterCache( const IAssetCache* pCache );
        /** @brief 목록의 캐시를 @p pAssetManager 레지스트리에 등록합니다. 앞서 바인딩된 매니저가 있으면 거기서 먼저 해제합니다. nullptr 이면 해제만 합니다. */
        static void attach( AssetManager* pAssetManager );
        /** @brief 묶인 매니저의 레지스트리에서 목록의 캐시를 내립니다. */
        static void detach() { attach( nullptr ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameDataCache
     * @brief 경로마다 테이블 하나를 보관하는 캐시입니다. 테이블 타입은 `bool loadFromResource( string_view )` 를 가져야 합니다.
     * @tparam TableType 기본 생성되는 데이터 표 타입입니다.
     * @details 읽지 못한 경로도 빈 항목으로 남긴다. 쓰는 쪽마다 같은 오류를 다시 내지 않게 하려는 것이며, 파일을 고치면 `reload` 가 그 항목을 채운다.
     */
    template <typename TableType>
    class GameDataCache final : public IAssetCache
    {
    public:
        /** @param pKindName 레지스트리의 종류 이름입니다. 정적 문자열이며, 에디터 핫 리로드 경로 표가 이 이름으로 찾습니다. */
        explicit GameDataCache( const utf8* pKindName )
            : _pKindName{ pKindName }
            , _mutex{}
            , _mapTable{}
            , _listRetired{}
            , _reloadCount{ 0 }
            , _stats{ CacheStats::getOrCreate( pKindName ) } // 캐시는 엔진 수명 범위가 소유해 다시 만들어질 수 있다 — 같은 이름의 칸을 이어 쓴다
        {
            GameDataCacheRegistry::registerCache( this );
        }

        ~GameDataCache() override { GameDataCacheRegistry::unregisterCache( this ); }

        GameDataCache( const GameDataCache& )            = delete;
        GameDataCache& operator=( const GameDataCache& ) = delete;

        /**
         * @brief 경로의 표를 반환합니다. 처음이면 읽으며, 읽지 못했으면 nullptr 입니다. 여러 스레드에서 불려도 됩니다.
         * @details 다시 읽거나 비워도 옛 표는 `kRetainGenerations` 번 더 다시 읽을 때까지(또는 캐시가 사라질 때까지) 남는다. 그보다 오래 쥐려면 `getReloadCount` 가 바뀔 때 다시 찾아야 한다.
         */
        const TableType* find( string_view path )
        {
            const string            key = makeKey( path );
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              iter = _mapTable.find( key );
            if ( iter != _mapTable.end() )
            {
                _stats.recordHit(); // 읽지 못한 경로를 기억한 빈 항목도 적중이다(같은 오류를 다시 읽지 않는다)
                return iter->second.get();
            }
            _stats.recordMiss();
            unique_ptr<TableType> table = make_unique<TableType>();
            if ( table->loadFromResource( path ) == false )
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
            std::scoped_lock<mutex> lock{ _mutex };
            return _mapTable.find( makeKey( relativePath ) ) != _mapTable.end();
        }

        /** @brief 읽은 적 있는 경로면 새 테이블로 다시 읽어 항목을 바꿉니다. 읽지 못하면 옛 테이블을 그대로 두고 오류를 남깁니다. 틱 밖(게임 스레드)에서 불러야 합니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override
        {
            (void)pDevice;
            const string key = makeKey( relativePath );
            {
                std::scoped_lock<mutex> lock{ _mutex };
                if ( _mapTable.find( key ) == _mapTable.end() )
                    return; // 아무도 읽지 않았으므로 다음 `find` 가 새 내용을 읽는다.
            }
            unique_ptr<TableType> fresh = make_unique<TableType>();
            if ( fresh->loadFromResource( relativePath ) == false )
            {
                SW_LOG_ERROR( "[GameDataCache] %# could not be reloaded - keeping the previous table", key );
                return;
            }
            std::scoped_lock<mutex> lock{ _mutex };
            unique_ptr<TableType>&  slot       = _mapTable[key];
            const uint32            generation = _reloadCount.load( std::memory_order_relaxed ) + 1; // 이 리로드가 만드는 세대
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
            std::scoped_lock<mutex> lock{ _mutex };
            size_t                  count{ 0 };
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
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto& [key, table] : _mapTable )
            {
                if ( table != nullptr )
                    _listRetired.push_back( RetiredTable{ std::move( table ), _reloadCount.load( std::memory_order_relaxed ) } );
            }
            _mapTable.clear();
            _stats.recordClear();
        }

    private:
        /** @brief 테이블의 키를 만듭니다. 핫 리로드가 넘기는 경로와 같은 항목을 찾도록 구분자를 맞춥니다. */
        static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }

        const utf8*                                  _pKindName;
        mutable mutex                                _mutex;
        unordered_map<string, unique_ptr<TableType>> _mapTable;
        /// 다시 읽거나 비우기 전의 테이블. 원시 포인터로 참조하는 쪽이 있을 수 있어 `kRetainGenerations` 번 더 다시 읽을 때까지(또는 캐시가 사라질 때까지) 산다.
        struct RetiredTable
        {
            unique_ptr<TableType> _pTable;
            uint32                _generation; ///< 내려놓을 때의 `_reloadCount`(리로드면 그 리로드의 세대)
        };
        static constexpr uint32 kRetainGenerations = 4; ///< 옛 표를 이 횟수만큼의 리로드 동안 둔다. 리로드는 사람 속도라 쓰는 쪽이 그 사이 다시 찾는다

        vector<RetiredTable> _listRetired;
        atomic<uint32>       _reloadCount;
        CacheStatsHandle     _stats; ///< 이 종류의 계측 칸(`CacheStats`)
    };
} // namespace sw
