/**
 * @file PreloadSet.h
 * @brief 미리 올릴 에셋 세트입니다 — 컷 · 레벨 전환 앞에 파일을 백그라운드로 읽어 두고(스트리밍 큐), 고정을 켰으면 읽은 에셋을 캐시에 붙들어 둡니다.
 */
#pragma once
#include "Core/Container/LRUMap.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetStreamingQueue.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"

// 미리 읽은 에셋을 붙드는 고정 세트입니다. 메모리 예산을 넘으면 오래 안 쓴 것부터 놓습니다(LRU).
namespace sw
{
    /**
     * @class AssetPinSet
     * @brief 에셋 고정 토큰(`AssetManager::pinAsset`)을 경로별로 쥐는 세트입니다(언리얼 `FStreamableHandle` 의 유지 · 텍스처 스트리밍 풀의 예산과 같은 자리).
     * @details 캐시가 약한 참조인 종류(메시 · 프리팹)는 미리 읽은 에셋이 쓰기 전에 사라질 수 있다 — 세트가 토큰을 쥐는 동안 남는다.
     *          비용은 부르는 쪽이 정한다(프리로드 세트는 읽은 파일 바이트 수). 더해서 예산을 넘으면 마지막으로 쓴(`add` · `touch`) 때가 가장 오래된 것부터 놓고,
     *          혼자 예산보다 큰 것은 붙들지 않는다(false — 나머지를 모두 밀어내지 않게). 한 스레드(게임 스레드)에서만 씁니다.
     *          항목은 쓴 순서를 함께 드는 맵(`LRUMap`)에 둔다 — 넣기 · 쓰기 · 놓기가 해시 조회 하나다(언리얼 텍스처 스트리밍 풀 · 유니티 Addressables 의 캐시도 키 조회 + LRU 목록).
     */
    class SW_API AssetPinSet
    {
    public:
        /** @param budgetBytes 붙든 비용의 합의 상한입니다. */
        explicit AssetPinSet( uint64 budgetBytes );
        ~AssetPinSet();
        AssetPinSet( const AssetPinSet& )            = delete;
        AssetPinSet& operator=( const AssetPinSet& ) = delete;

        /**
         * @brief @p path 를 토큰 @p pin 으로 붙듭니다. 이미 붙든 경로면 토큰은 그대로 두고 쓴 때만 새로 합니다.
         * @return 붙들었으면(또는 이미 붙들었으면) true. 토큰이 비었거나 혼자 예산보다 크면 false 입니다.
         */
        bool add( string_view path, shared_ptr<const void> pin, uint64 costBytes );
        /** @brief 붙든 경로를 지금 썼다고 적습니다(LRU 순서). 붙들지 않은 경로면 아무 일도 없습니다. */
        void touch( string_view path );
        /** @brief 경로 하나를 놓습니다. 붙들지 않았으면 false 입니다. */
        [[nodiscard]] bool remove( string_view path );
        /** @brief 모두 놓습니다. */
        void clear();

        /** @brief 그 경로를 붙들고 있으면 true 입니다. */
        bool contains( string_view path ) const;
        /** @brief 붙든 경로 수입니다. */
        size_t getCount() const { return _mapEntry.size(); }
        /** @brief 붙든 비용의 합입니다. */
        uint64 getPinnedBytes() const { return _pinnedBytes; }
        /** @brief 예산입니다. */
        uint64 getBudgetBytes() const { return _budgetBytes; }

    private:
        struct Entry
        {
            shared_ptr<const void> _pin;
            uint64                 _costBytes{ 0 };
        };

        /** @brief 합이 예산 안에 들 때까지 가장 오래 안 쓴 것부터 놓습니다. 방금 넣은 것은 가장 최근이고 혼자 예산 안이라 놓이지 않습니다. */
        void evictOverBudget();

    private:
        LRUMap<string, Entry> _mapEntry; ///< 정규화한 경로 → 항목(쓴 순서를 함께 든다)
        uint64                _budgetBytes;
        uint64                _pinnedBytes;
    };
} // namespace sw

namespace sw
{
    class AssetManager;

    /**
     * @class PreloadSet
     * @brief 에셋 경로 세트를 프레임 예산(동시에 걸어 둘 요청 수) 안에서 스트리밍 큐에 겁니다(언리얼 AssetManager 의 번들 프리로드 · 유니티 Addressables 의 의존 다운로드 자리).
     * @details `update` 가 빈 자리만큼 다음 경로를 요청하고, 완료 콜백(큐의 `update` 를 부른 스레드)이 끝남 · 실패를 센다. 실패한 경로는 `getFailedAssets` 에 남고
     *          세트는 실패를 건너뛰고 끝까지 간다(무엇이 빠졌는지 부르는 쪽이 알린다). 세트가 먼저 사라져도 늦게 온 콜백은 공유 상태만 고친다.
     *          기본은 파일 읽기를 앞당기는 것(OS 캐시 · 팩 압축 해제)뿐이다. `enablePinning` 을 부르면 경로를 그 종류의 캐시에 걸고(`AssetManager::pinAsset`)
     *          토큰을 쥔다 — 캐시가 약한 참조라 쓰기 전에 사라지는 일을 막는다. 고정하는 종류는 큐로 읽지 않고 캐시의 로드가 곧 읽기다(읽기 한 번 — 감사 A-19,
     *          언리얼 `FStreamableHandle` 이 로드한 객체를 핸들이 쥔다). 고정의 비용은 파일 바이트 수(`AssetIO::findAssetSize`)이고,
     *          예산을 넘으면 오래 안 쓴 것부터 놓는다(`AssetPinSet`). 고정하는 캐시가 없는 종류(텍스처 · 오디오)는 큐로 읽기만 한다 — 텍스처는 그것을 쓰는 머티리얼의 고정이 붙든다.
     */
    class SW_API PreloadSet
    {
    public:
        /**
         * @param maxInFlight 한 번에 걸어 둘 요청의 상한(1 이상)입니다 — 한 프레임에 IO 를 몰지 않는 예산이다.
         */
        PreloadSet( vector<string> listAssetPath, StreamingPriority priority, uint32 maxInFlight );
        /** @brief 붙든 에셋을 놓습니다. 늦게 온 콜백은 더 붙들지 않습니다. */
        ~PreloadSet();
        PreloadSet( const PreloadSet& )            = delete;
        PreloadSet& operator=( const PreloadSet& ) = delete;

        /**
         * @brief 읽은 경로를 @p manager 의 캐시에 붙듭니다(고정). 첫 `update` 전에 한 번 부릅니다.
         * @param budgetBytes 붙든 에셋의 바이트 합의 상한입니다(`AssetPinSet`). @p manager 는 이 세트보다 오래 삽니다.
         */
        void enablePinning( AssetManager& manager, uint64 budgetBytes );

        /** @brief 빈 자리만큼 다음 경로를 요청합니다. 큐의 `update` 와 같은 스레드에서 프레임마다 부릅니다. */
        void update( AssetStreamingQueue& queue );
        /**
         * @brief 이 세트가 걸어 둔 요청을 거두고 붙든 에셋을 놓습니다(전환이 취소됐을 때). 끝난 것은 그대로 셉니다.
         *        같은 경로를 다른 쪽도 기다리면 그 요청은 그대로 받는다(큐가 요청자 단위로 취소한다).
         */
        void cancel( AssetStreamingQueue& queue );

        /** @brief 붙든 경로를 지금 썼다고 알립니다(예산을 넘을 때 늦게 놓인다). */
        void touch( string_view path );

        /** @brief 끝난 수(실패 포함) / 전체입니다. 경로가 없으면 1 입니다. */
        float32 getProgress() const;
        /** @brief 모든 경로가 끝났으면(실패 포함) true 입니다. */
        bool isComplete() const;
        /** @brief 읽지 못한 경로들입니다. */
        const vector<string>& getFailedAssets() const;
        uint32                getTotalCount() const { return static_cast<uint32>( _listAssetPath.size() ); }
        /** @brief 붙든 에셋 세트입니다. 고정을 켜지 않았으면 nullptr 입니다. */
        const AssetPinSet* findPins() const { return _state->_pPins.get(); }

    private:
        /** @brief 캐시의 로드를 기다리는 고정 하나입니다(`tryRequestPin`). */
        struct PendingPin
        {
            string   _path;
            AssetPin _pin;
        };

        /** @brief 콜백이 고치는 상태입니다. 세트보다 오래 살 수 있어 공유한다. */
        struct State
        {
            vector<string>          _listFailed;
            unique_ptr<AssetPinSet> _pPins{};                ///< 고정을 켰으면 붙든 에셋(`enablePinning`)
            AssetManager*           _pPinManager{ nullptr }; ///< 고정을 켰으면 토큰을 내줄 매니저
            uint32                  _completedCount{ 0 };
            uint32                  _inFlightCount{ 0 };
            uint8                   _bCancelled{ SW_FALSE };
        };

        /** @brief 큐의 읽기 하나가 끝났을 때(큐의 콜백) 센다. */
        static void onAssetRead( State& state, string_view path, bool bSuccess );
        /** @brief 고정하는 종류면 캐시에 걸고 true 입니다(큐로 읽지 않는다). 고정하는 캐시가 없는 종류면 false 입니다. */
        bool tryRequestPin( const string& path );
        /** @brief 고정 하나가 끝났다 — 크기를 찾아 붙들거나 실패로 센다. */
        void finishPin( const string& path, shared_ptr<const void> token, bool bLoaded );
        /** @brief 로드가 끝난 고정을 거둔다. */
        void pollPendingPins();

    private:
        vector<string>           _listAssetPath;
        vector<StreamingRequest> _listRequest;    ///< 낸 요청(취소가 이 세트의 요청만 거둔다)
        vector<PendingPin>       _listPendingPin; ///< 캐시의 로드를 기다리는 고정(게임 스레드에서만 고친다)
        shared_ptr<State>        _state;
        size_t                   _nextIndex;
        uint32                   _maxInFlight;
        StreamingPriority        _priority;
    };
} // namespace sw
