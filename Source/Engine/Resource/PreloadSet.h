/**
 * @file PreloadSet.h
 * @brief 미리 올릴 에셋 묶음입니다 — 컷 · 레벨 전환 앞에 파일을 백그라운드로 읽어 두고(스트리밍 큐), 진행률과 완료를 알립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/AssetStreamingQueue.h"

namespace sw
{
    /**
     * @class PreloadSet
     * @brief 에셋 경로 묶음을 프레임 예산(동시에 걸어 둘 요청 수) 안에서 스트리밍 큐에 겁니다(언리얼 AssetManager 의 번들 프리로드 · 유니티 Addressables 의 의존 다운로드 자리).
     * @details `update` 가 빈 자리만큼 다음 경로를 요청하고, 완료 콜백(큐의 `update` 를 부른 스레드)이 끝남 · 실패를 센다. 실패한 경로는 `getFailedAssets` 에 남고
     *          묶음은 실패를 건너뛰고 끝까지 간다(무엇이 빠졌는지 부르는 쪽이 알린다). 묶음이 먼저 사라져도 늦게 온 콜백은 공유 상태만 고친다.
     *          이 묶음이 하는 일은 파일 읽기를 앞당기는 것(OS 캐시 · 팩 압축 해제)이다 — 에셋 객체를 붙들어 두는 고정(캐시는 약한 참조)은 종류마다의 캐시가 맡는다.
     */
    class SW_API PreloadSet
    {
    public:
        /**
         * @param maxInFlight 한 번에 걸어 둘 요청의 상한(1 이상)입니다 — 한 프레임에 IO 를 몰지 않는 예산이다.
         */
        PreloadSet( vector<string> listAssetPath, StreamingPriority priority, uint32 maxInFlight );
        ~PreloadSet();
        PreloadSet( const PreloadSet& )            = delete;
        PreloadSet& operator=( const PreloadSet& ) = delete;

        /** @brief 빈 자리만큼 다음 경로를 요청합니다. 큐의 `update` 와 같은 스레드에서 프레임마다 부릅니다. */
        void update( AssetStreamingQueue& queue );
        /** @brief 걸어 둔 요청을 거둡니다(전환이 취소됐을 때). 끝난 것은 그대로 셉니다. 같은 경로를 다른 쪽도 기다리면 그 요청도 거둬진다(큐가 경로 단위로 취소한다). */
        void cancel( AssetStreamingQueue& queue );

        /** @brief 끝난 수(실패 포함) / 전체입니다. 경로가 없으면 1 입니다. */
        float32 getProgress() const;
        /** @brief 모든 경로가 끝났으면(실패 포함) true 입니다. */
        bool isComplete() const;
        /** @brief 읽지 못한 경로들입니다. */
        const vector<string>& getFailedAssets() const;
        uint32                getTotalCount() const { return static_cast<uint32>( _listAssetPath.size() ); }

    private:
        /** @brief 콜백이 고치는 상태입니다. 묶음보다 오래 살 수 있어 공유한다. */
        struct State
        {
            vector<string> _listFailed;
            uint32         _completedCount{ 0 };
            uint32         _inFlightCount{ 0 };
            uint8          _bCancelled{ SW_FALSE };
        };

        vector<string>    _listAssetPath;
        shared_ptr<State> _state;
        size_t            _nextIndex;
        uint32            _maxInFlight;
        StreamingPriority _priority;
    };
} // namespace sw
