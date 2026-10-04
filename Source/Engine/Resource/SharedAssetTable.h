/**
 * @file SharedAssetTable.h
 * @brief 경로 → 약한 참조 에셋 표 하나입니다. 경로로 나눠 주는 캐시(스켈레톤 · 클립 · 물리 에셋 · 캐릭터 데이터 표)가 같은 구현을 씁니다.
 * @details 같은 경로는 같은 객체를 받고, 마지막 사용자가 놓으면 사라집니다. 읽기(파일 IO)는 잠금 밖에서 하고, 둘이 같은 경로를 동시에 읽으면 먼저
 *          넣은 쪽이 남습니다. `reloadShared` 는 쥔 객체를 제자리에서 바꾸고 다시 읽은 횟수(`getReloadCount`)를 올립니다 — 쓰는 쪽은 그 값이
 *          달라졌을 때 자기가 지은 것(래그돌 · 알림 표 풀이)을 다시 짓습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class SharedAssetTable
     * @brief 에셋 종류 하나의 공유 표입니다. 종류마다 프로세스에 하나(Engine.dll 안의 함수 정적 변수)를 둡니다.
     * @tparam AssetType 기본 생성 · 이동 대입이 되는 에셋 타입입니다.
     */
    template <typename AssetType>
    class SharedAssetTable
    {
    public:
        /** @brief 경로의 파일을 @p outAsset 에 읽습니다. 읽지 못하면 false 입니다. */
        using LoadFunction = bool ( * )( string_view path, AssetType& outAsset );

        SharedAssetTable()
            : _mutex{}
            , _mapAsset{}
            , _reloadCount{ 0 }
        {
        }

        SharedAssetTable( const SharedAssetTable& )            = delete;
        SharedAssetTable& operator=( const SharedAssetTable& ) = delete;

        /** @brief 경로의 에셋을 나눠 받습니다. 처음이면 @p load 로 읽고, 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다(잠급니다). */
        shared_ptr<const AssetType> acquire( string_view path, LoadFunction load )
        {
            if ( path.empty() || load == nullptr )
                return nullptr;
            shared_ptr<const AssetType> live = findLive( path );
            if ( live != nullptr )
                return live;

            shared_ptr<AssetType> loaded = make_shared<AssetType>();
            if ( load( path, *loaded ) == false )
                return nullptr;

            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto iter = _mapAsset.begin(); iter != _mapAsset.end(); )
            {
                if ( iter->second.expired() )
                    iter = _mapAsset.erase( iter );
                else
                    ++iter;
            }
            weak_ptr<AssetType>&        slot   = _mapAsset[makeKey( path )];
            shared_ptr<const AssetType> winner = slot.lock();
            if ( winner != nullptr )
                return winner;
            slot = loaded;
            return loaded;
        }

        /** @brief 그 경로를 지금 쥔 에셋입니다. 없으면 nullptr 입니다. */
        shared_ptr<AssetType> findLive( string_view path )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              it = _mapAsset.find( makeKey( path ) );
            return ( it != _mapAsset.end() ) ? it->second.lock() : nullptr;
        }

        /** @brief 사용 중이면 제자리로 다시 읽고 다시 읽은 횟수를 올립니다(틱 밖, 게임 스레드). 읽지 못하면 옛 내용 그대로이고 false 입니다. */
        [[nodiscard]] bool reloadShared( string_view path, LoadFunction load )
        {
            shared_ptr<AssetType> live = findLive( path );
            if ( live == nullptr || load == nullptr )
                return false;
            AssetType fresh;
            if ( load( path, fresh ) == false )
                return false;
            *live = std::move( fresh );
            _reloadCount.fetch_add( 1, std::memory_order_release );
            return true;
        }

        /** @brief 이 표에서 제자리로 다시 읽은 횟수입니다. 쓰는 쪽은 값이 바뀌면 지은 것을 다시 짓습니다. */
        uint32 getReloadCount() const { return _reloadCount.load( std::memory_order_acquire ); }

        /** @brief 살아 있는 항목 수입니다. */
        size_t countLive()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            size_t                  liveCount{ 0 };
            for ( const auto& [key, asset] : _mapAsset )
            {
                if ( asset.expired() == false )
                    ++liveCount;
            }
            return liveCount;
        }

        /** @brief 표를 비웁니다(쥔 쪽의 객체는 그대로 삽니다). */
        void clear()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _mapAsset.clear();
        }

    private:
        /** @brief 표의 키입니다 — 핫 리로드가 같은 칸을 찾도록 구분자를 맞춥니다. */
        static string makeKey( string_view path ) { return FileUtil::normalizeSeparators( path ); }

        mutex                                      _mutex;
        unordered_map<string, weak_ptr<AssetType>> _mapAsset;
        atomic<uint32>                             _reloadCount;
    };
} // namespace sw
