/**
 * @file GameDataCache.h
 * @brief 경로마다 한 번 읽어 여러 컴포넌트가 나눠 쓰는 게임 데이터 표(상호작용 표 · 원소 규칙 표)의 캐시와, 그 캐시들을 에셋 캐시 등록부에 올리는 목록입니다.
 * @details 캐시는 GameFramework 모듈의 정적 객체입니다. 게임 서비스가 묶이면(`game::bindGameService` — 서비스에 `AssetManager` 가 있으면) 등록부에 오르고,
 *          풀리면(`unbindGameService`) 내려갑니다. 그래서 에디터 핫 리로드가 파일을 고친 표를 종류 이름으로 찾아 다시 읽고(`reload`), 종료의 비우기 · 진단도
 *          엔진 캐시와 같은 길을 탑니다. 모듈이 내리지 않고 사라지면 `AssetManager::onModuleUnloading` 이 이미지 안의 캐시를 걷습니다.
 *
 *          다시 읽은 표는 **새 객체**로 칸을 바꾸고, 옛 객체는 캐시가 사라질 때까지 살려 둡니다 — 표 안의 정의를 날 포인터로 든 컴포넌트가 그 프레임에 옛 것을 읽어도
 *          죽은 메모리를 밟지 않습니다. 쓰는 쪽은 다시 읽은 횟수(`getReloadCount`)가 바뀌면 자기 정의를 다시 찾습니다(UE `UAssetManager` 의 주 에셋 다시 읽기 자리).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/IAssetCache.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AssetManager;

    /**
     * @struct GameDataCacheRegistry
     * @brief GameFramework 의 데이터 캐시 목록과, 그것을 올린 에셋 매니저입니다. 캐시가 생기거나 서비스가 묶이고 풀릴 때 등록부를 맞춥니다.
     */
    struct SW_GF_API GameDataCacheRegistry
    {
        /** @brief 캐시를 목록에 넣고, 묶인 매니저가 있으면 그 등록부에 올립니다. 캐시 생성자가 부릅니다. */
        static void addCache( IAssetCache* pCache );
        /** @brief 캐시를 목록에서 뺍니다. 캐시 소멸자가 부릅니다(매니저는 건드리지 않는다 — 프로세스 끝에는 이미 사라졌을 수 있다). */
        static void removeCache( const IAssetCache* pCache );
        /** @brief 목록의 캐시를 @p pAssetManager 등록부에 올립니다. 앞서 묶인 매니저가 있으면 거기서 먼저 내립니다. nullptr 이면 내리기만 합니다. */
        static void attach( AssetManager* pAssetManager );
        /** @brief 묶인 매니저의 등록부에서 목록의 캐시를 내립니다. */
        static void detach() { attach( nullptr ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameDataCache
     * @brief 경로 → 표 하나입니다. 표 타입은 `bool loadFromResource( string_view )` 를 가집니다.
     * @tparam TableType 기본 생성되는 데이터 표 타입입니다.
     * @details 읽지 못한 경로도 칸을 남깁니다(빈 칸) — 쓰는 쪽마다 같은 오류를 다시 내지 않고, 파일을 고치면 `reload` 가 그 칸을 채웁니다.
     */
    template <typename TableType>
    class GameDataCache final : public IAssetCache
    {
    public:
        /** @param pKindName 등록부의 종류 이름입니다(정적 문자열 — 에디터 핫 리로드 경로 표가 이 이름으로 찾는다). */
        explicit GameDataCache( const utf8* pKindName )
            : _pKindName{ pKindName }
            , _mutex{}
            , _mapTable{}
            , _listRetired{}
            , _reloadCount{ 0 }
        {
            GameDataCacheRegistry::addCache( this );
        }

        ~GameDataCache() override { GameDataCacheRegistry::removeCache( this ); }

        GameDataCache( const GameDataCache& )            = delete;
        GameDataCache& operator=( const GameDataCache& ) = delete;

        /**
         * @brief 경로의 표입니다. 처음이면 읽습니다. 읽지 못했으면 nullptr 입니다. 여러 스레드에서 불려도 됩니다.
         * @details 돌려준 표는 캐시가 사라질 때(모듈이 내릴 때)까지 삽니다(다시 읽거나 비워도 옛 표는 남는다).
         */
        const TableType* find( string_view path )
        {
            const string            key = makeKey( path );
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              iter = _mapTable.find( key );
            if ( iter != _mapTable.end() )
                return iter->second.get();
            unique_ptr<TableType> table = make_unique<TableType>();
            if ( table->loadFromResource( path ) == false )
                table.reset();
            const TableType* pTable = table.get();
            _mapTable[key]          = std::move( table );
            return pTable;
        }

        /** @brief 이 캐시가 표를 다시 읽은 횟수입니다. 쓰는 쪽은 값이 바뀌면 정의를 다시 찾습니다. */
        uint32 getReloadCount() const { return _reloadCount.load( std::memory_order_acquire ); }

        const utf8* getAssetKindName() const override { return _pKindName; }

        bool isCached( string_view relativePath ) const override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _mapTable.find( makeKey( relativePath ) ) != _mapTable.end();
        }

        /** @brief 읽은 적 있는 경로면 새 표로 다시 읽어 칸을 바꿉니다. 읽지 못하면 옛 표 그대로이고 오류를 남깁니다. 틱 밖(게임 스레드)에서 부릅니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override
        {
            (void)pDevice;
            const string key = makeKey( relativePath );
            {
                std::scoped_lock<mutex> lock{ _mutex };
                if ( _mapTable.find( key ) == _mapTable.end() )
                    return; // 아무도 읽지 않았다 — 다음 `find` 가 새 내용을 읽는다
            }
            unique_ptr<TableType> fresh = make_unique<TableType>();
            if ( fresh->loadFromResource( relativePath ) == false )
            {
                SW_LOG_ERROR( "[GameDataCache] %# could not be reloaded - keeping the previous table", key );
                return;
            }
            std::scoped_lock<mutex> lock{ _mutex };
            unique_ptr<TableType>&  slot = _mapTable[key];
            if ( slot != nullptr )
                _listRetired.push_back( std::move( slot ) );
            slot = std::move( fresh );
            _reloadCount.fetch_add( 1, std::memory_order_release );
        }

        /** @brief 읽어 둔 표의 수입니다(읽지 못한 빈 칸은 세지 않는다). */
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
         * @brief 표를 비웁니다(종료 · 재초기화) — 다음 `find` 는 파일을 새로 읽습니다.
         * @details 앞서 돌려준 표는 버리지 않고 캐시가 사라질 때(모듈이 내릴 때)까지 둡니다. 종료 순서상 비우기 뒤에도 정의를 날 포인터로 든 컴포넌트가 남아 있을 수 있다.
         */
        void clear() override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto& [key, table] : _mapTable )
            {
                if ( table != nullptr )
                    _listRetired.push_back( std::move( table ) );
            }
            _mapTable.clear();
        }

    private:
        /** @brief 표의 키입니다 — 핫 리로드가 넘기는 경로와 같은 칸을 찾도록 구분자를 맞춥니다. */
        static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }

        const utf8*                                  _pKindName;
        mutable mutex                                _mutex;
        unordered_map<string, unique_ptr<TableType>> _mapTable;
        vector<unique_ptr<TableType>>                _listRetired; ///< 다시 읽거나 비우기 전의 표 — 날 포인터로 든 쪽이 있을 수 있어 캐시와 함께 산다
        atomic<uint32>                               _reloadCount;
    };
} // namespace sw
