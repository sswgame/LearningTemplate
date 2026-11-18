/**
 * @file MaterialChangeLog.h
 * @brief 머티리얼 · 인스턴스의 변경을 세대 번호와 바꾼 주인으로 적는 고리 기록입니다. 읽는 쪽은 지난번 세대 뒤에 바뀐 주인만 받아 그것만 다시 봅니다.
 * @details 예전에는 세대 번호 하나뿐이라 "무언가 바뀌었다" 만 알 수 있었다 — 머티리얼 하나의 값을 바꾸면 장면의 모든 머티리얼 원소를 다시 맞추고(`GPUMaterialRegistry::refreshMaterialBindings`),
 *          키워드 하나를 켜면 모든 프리미티브를 다시 모았다(`GPUSceneBuilder` 의 전체 수집). 유니티 SRP Batcher 는 바뀐 머티리얼의 CBUFFER 만 다시 올리고,
 *          언리얼은 머티리얼 인스턴스가 바뀌면 그 유니폼 표현만 무효화한다 — 읽는 쪽 비용이 머티리얼 수가 아니라 바뀐 수에 비례한다.
 *          읽는 쪽이 여럿이라(장면마다 빌더 하나) 꺼내 비우는 줄이 아니라 고리다. 고리가 넘쳤거나 주인을 모르는 변경(nullptr — 묶음 값처럼 모두에 닿는 것)이 끼면
 *          `collectChangedOwners` 가 false 를 돌려주고 읽는 쪽은 전부 다시 본다(느릴 뿐 틀리지 않는다).
 */
#pragma once
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class MaterialChangeLog
     * @brief 변경 세대 → 바꾼 주인(머티리얼 · 인스턴스 주소, nullptr = 모두)의 고리입니다. 쓰기는 어느 스레드에서나(머티리얼 틱이 워커에서 값을 넣는다), 읽기는 빌더가 합니다.
     */
    class SW_API MaterialChangeLog
    {
    public:
        /** @brief 고리 칸 수입니다. 한 빌드 사이에 이보다 많이 바뀌면 읽는 쪽이 전부 다시 본다(프레임마다 값을 바꾸는 머티리얼 수백 개 × 파라미터 몇 개를 넉넉히 담는다). */
        static constexpr uint32 kCapacity = 8192;

        MaterialChangeLog();

        /** @brief 지금 세대입니다. 1 부터라 0 은 "모름" 이다. */
        uint64 getGeneration() const { return _generation.load( std::memory_order_acquire ); }
        /** @brief 세대를 올리고 @p pOwner 가 바뀌었다고 적습니다. nullptr 이면 "모두" 다(읽는 쪽이 전부 다시 본다). */
        void record( const void* pOwner );
        /**
         * @brief 세대 (@p sinceGeneration, @p untilGeneration] 에 바뀐 주인을 중복 없이 @p outListOwner 에 담습니다(앞 내용은 지운다).
         * @return 고리가 그 구간을 덮지 못했거나(넘침) "모두" 변경이 끼었으면 false — 부르는 쪽은 전부 다시 본다.
         */
        [[nodiscard]] bool collectChangedOwners( uint64 sinceGeneration, uint64 untilGeneration, vector<const void*>& outListOwner ) const;

    private:
        struct Entry
        {
            uint64      _generation{ 0 };
            const void* _pOwner{ nullptr };
        };
        mutable SpinLock         _lock;
        vector<Entry> _listEntry SW_GUARDED_BY( _lock ); ///< 고리(`kCapacity` 칸). 세대 g 는 g % kCapacity 칸이다
        atomic<uint64>           _generation;
    };
} // namespace sw
