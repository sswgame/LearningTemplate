/**
 * @file PreloadSet.h
 * @brief 미리 올릴 에셋 세트입니다 — 컷 · 레벨 전환 앞에 파일을 백그라운드로 읽어 두고(스트리밍 큐), 고정을 켰으면 읽은 에셋을 캐시에 붙들어 둡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetPinSet.h"
#include "Engine/Asset/AssetStreamingQueue.h"

namespace sw
{
    class AssetManager;

    /**
     * @class PreloadSet
     * @brief 에셋 경로 세트를 프레임 예산(동시에 걸어 둘 요청 수) 안에서 스트리밍 큐에 겁니다(언리얼 AssetManager 의 번들 프리로드 · 유니티 Addressables 의 의존 다운로드 자리).
     * @details `update` 가 빈 자리만큼 다음 경로를 요청하고, 완료 콜백(큐의 `update` 를 부른 스레드)이 끝남 · 실패를 센다. 실패한 경로는 `getFailedAssets` 에 남고
     *          세트는 실패를 건너뛰고 끝까지 간다(무엇이 빠졌는지 부르는 쪽이 알린다). 세트가 먼저 사라져도 늦게 온 콜백은 공유 상태만 고친다.
     *          기본은 파일 읽기를 앞당기는 것(OS 캐시 · 팩 압축 해제)뿐이다. `enablePinning` 을 부르면 읽기에 성공한 경로를 그 종류의 캐시에 걸고
     *          (`AssetManager::pinAsset`) 토큰을 쥔다 — 캐시가 약한 참조라 쓰기 전에 사라지는 일을 막는다. 고정의 비용은 읽은 바이트 수이고,
     *          예산을 넘으면 오래 안 쓴 것부터 놓는다(`AssetPinSet`). 고정하는 캐시가 없는 종류(텍스처 · 오디오)는 읽기만 한다 — 텍스처는 그것을 쓰는 머티리얼의 고정이 붙든다.
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
         * @brief 걸어 둔 요청을 거두고 붙든 에셋을 놓습니다(전환이 취소됐을 때). 끝난 것은 그대로 셉니다.
         *        같은 경로를 다른 쪽도 기다리면 그 요청도 거둬진다(큐가 경로 단위로 취소한다).
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

        /** @brief 요청 하나가 끝났을 때(큐의 콜백) 센다. 성공했고 고정이 켜져 있으면 붙든다. */
        static void onAssetRead( State& state, string_view path, bool bSuccess, uint64 byteCount );

        vector<string>    _listAssetPath;
        shared_ptr<State> _state;
        size_t            _nextIndex;
        uint32            _maxInFlight;
        StreamingPriority _priority;
    };
} // namespace sw
