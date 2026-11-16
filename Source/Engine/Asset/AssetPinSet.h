/**
 * @file AssetPinSet.h
 * @brief 미리 읽은 에셋을 붙드는 고정 세트입니다. 메모리 예산을 넘으면 오래 안 쓴 것부터 놓습니다(LRU).
 */
#pragma once
#include "Core/Container/list.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class AssetPinSet
     * @brief 에셋 고정 토큰(`AssetManager::pinAsset`)을 경로별로 쥐는 세트입니다(언리얼 `FStreamableHandle` 의 유지 · 텍스처 스트리밍 풀의 예산과 같은 자리).
     * @details 캐시가 약한 참조인 종류(메시 · 프리팹)는 미리 읽은 에셋이 쓰기 전에 사라질 수 있다 — 세트가 토큰을 쥐는 동안 남는다.
     *          비용은 부르는 쪽이 정한다(프리로드 세트는 읽은 파일 바이트 수). 더해서 예산을 넘으면 마지막으로 쓴(`add` · `touch`) 때가 가장 오래된 것부터 놓고,
     *          혼자 예산보다 큰 것은 붙들지 않는다(false — 나머지를 모두 밀어내지 않게). 한 스레드(게임 스레드)에서만 씁니다.
     *          항목은 쓴 순서 목록(앞이 가장 오래 안 씀)에 두고 경로 → 목록 위치 맵으로 찾는다 — 넣기 · 쓰기 · 놓기가 해시 조회 하나다(언리얼 텍스처 스트리밍 풀 · 유니티 Addressables 의 캐시도 키 조회 + LRU 목록).
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
        size_t getCount() const { return _listEntryByUse.size(); }
        /** @brief 붙든 비용의 합입니다. */
        uint64 getPinnedBytes() const { return _pinnedBytes; }
        /** @brief 예산입니다. */
        uint64 getBudgetBytes() const { return _budgetBytes; }

    private:
        struct Entry
        {
            string                 _path;
            shared_ptr<const void> _pin;
            uint64                 _costBytes{ 0 };
        };
        using EntryList = list<Entry>;

        /** @brief @p entry 를 가장 최근에 쓴 것으로 옮깁니다(목록 끝). */
        void markUsed( EntryList::iterator entry );
        /** @brief 합이 예산 안에 들 때까지 가장 오래 안 쓴 것(목록 앞)부터 놓습니다. 방금 넣은 것은 끝에 있고 혼자 예산 안이라 놓이지 않습니다. */
        void evictOverBudget();

    private:
        EntryList                                  _listEntryByUse; ///< 쓴 순서(앞이 가장 오래 안 씀)
        unordered_map<string, EntryList::iterator> _mapPathToEntry; ///< 정규화한 경로 → 목록 위치
        uint64                                     _budgetBytes;
        uint64                                     _pinnedBytes;
    };
} // namespace sw
