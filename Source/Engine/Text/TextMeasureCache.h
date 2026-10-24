/**
 * @file TextMeasureCache.h
 * @brief 텍스트 측정 결과(키 → 크기)를 두 세대로 보관하는 캐시입니다. 상한을 넘어도 전체를 비우지 않고 오래된 세대만 버립니다.
 * @details **상한과 넘칠 때의 동작.** 세대는 둘(현재 · 이전)이고 한 세대는 `generationCapacity` 개까지 듭니다. 현재가 차면 이전을 버리고 현재를 이전으로 넘긴 뒤 빈
 *          현재에 넣습니다. 그래서 항목 수는 언제나 `2 × generationCapacity` 이하이고, 방금 쓴 항목 `generationCapacity` 개는 반드시 남습니다.
 *          조회는 현재 → 이전 순서이며, 이전에서 찾으면 현재로 승격합니다(이전에서는 뗀다 — 두 세대가 겹치지 않아 항목 수가 정확하다). 자주 쓰는 항목은 세대가
 *          바뀔 때마다 현재로 돌아와 남고, 한 번 쓰고 안 쓴 항목은 두 번 넘어가면 사라집니다.
 *
 *          **왜 전체 비우기가 아닌가.** 옛 구현은 4096 개가 차면 전부 비웠다. 그러면 화면에 쓰이는 글 수가 상한 근처인 화면에서 상한을 넘는 순간 모든 글이 같은 프레임에 측정을
 *          다시 해야 하고(경계에서 한꺼번에 다시 계산하는 thrash) 그 프레임이 튄다. 두 세대 교대는 LRU 의 근사이고 항목마다 순서 갱신이 없어 조회가 해시 한 번이다
 *          (유니티 TextCore · 언리얼 `FSlateFontMeasure` 의 글 캐시도 상한이 있는 근사 LRU).
 *
 *          **무효화 사유.** 키가 결과를 정하는 모든 입력(글 · 스타일 · 너비 · 지금 면 연쇄 · 리치 구간)을 해시로 담으므로 입력이 바뀌면 키가 바뀌어 낡은 항목은 조회되지 않고
 *          세대 교대로 사라집니다. 그래서 문화권 · 폰트 교체 알림에 연결하지 않습니다(`TextLayoutEngine::measure` 의 주석). 직접 비우는 것은 `clear` 뿐입니다.
 *
 *          **키는 해시 하나입니다(값 비교 없음).** 64 비트 해시가 부딪히면 서로 다른 글이 같은 크기를 받아 조용히 틀립니다. 상한 8192 개에서 한 세대가 차는 동안 부딪힐
 *          확률은 약 2^-39(8192² / 2^65 ≈ 1.8e-12)이고 결과는 줄 높이가 아니라 크기 몇 픽셀이 어긋나는 것입니다. 값 비교로 바꾸려면 항목마다 글 사본이 필요해 메모리가
 *          글 길이만큼 늡니다 — 측정으로 부딪힘이 보이면 두 번째 독립 해시를 항목에 넣는 안으로 간다.
 *
 *          스레드: 게임 스레드에서만 씁니다(`TextLayoutEngine` 이 게임 스레드 전용).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @class TextMeasureCache
     * @brief 해시 키 → 측정 크기 표이며 두 세대로 교대합니다(파일 머리말). 적중 · 실패 · 비우기 · 항목 수를 `CacheStats` 의 칸에 셉니다.
     */
    class TextMeasureCache
    {
    public:
        /**
         * @param generationCapacity 한 세대가 담는 항목 수입니다(0 은 1 로 올립니다 — 0 이면 세대가 늘 넘칩니다).
         * @param statsName 계측 칸의 이름입니다. 같은 이름의 캐시 객체가 여럿(UI 시스템마다 하나)이어도 한 칸에 합쳐집니다.
         */
        TextMeasureCache( size_t generationCapacity, string_view statsName )
            : _mapCurrent{}
            , _mapOlder{}
            , _generationCapacity{ generationCapacity == 0 ? 1 : generationCapacity }
            , _stats{ CacheStats::getOrCreate( statsName ) }
        {
        }

        ~TextMeasureCache() { _stats.addItemCount( -static_cast<int64>( getCount() ) ); }

        TextMeasureCache( const TextMeasureCache& )            = delete;
        TextMeasureCache& operator=( const TextMeasureCache& ) = delete;

        /**
         * @brief 키의 크기를 찾습니다. 없으면 nullptr 입니다. 이전 세대에서 찾으면 현재로 승격합니다.
         * @details 돌려준 포인터는 다음 `find` · `insert` · `clear` 까지만 유효합니다(승격이나 세대 교대가 표를 바꾼다).
         */
        const float2* find( uint64 key )
        {
            const auto currentIter = _mapCurrent.find( key );
            if ( currentIter != _mapCurrent.end() )
            {
                _stats.recordHit();
                return &currentIter->second;
            }
            const auto previousIter = _mapOlder.find( key );
            if ( previousIter == _mapOlder.end() )
            {
                _stats.recordMiss();
                return nullptr;
            }
            _stats.recordHit();
            const float2 size = previousIter->second;
            _mapOlder.erase( previousIter );
            _stats.addItemCount( -1 ); // 아래 insertCurrent 가 다시 하나를 더한다 — 승격은 항목 수를 바꾸지 않는다
            return &insertCurrent( key, size );
        }

        /** @brief 키의 크기를 현재 세대에 넣습니다. 현재가 차 있으면 먼저 세대를 교대합니다. 이미 있는 키면 값을 덮어씁니다. */
        void insert( uint64 key, const float2& size )
        {
            const auto previousIter = _mapOlder.find( key );
            if ( previousIter != _mapOlder.end() )
            {
                _mapOlder.erase( previousIter );
                _stats.addItemCount( -1 );
            }
            const auto currentIter = _mapCurrent.find( key );
            if ( currentIter != _mapCurrent.end() )
            {
                currentIter->second = size;
                return;
            }
            insertCurrent( key, size );
        }

        /** @brief 두 세대를 모두 비웁니다. 비우기 횟수를 하나 셉니다. */
        void clear()
        {
            _stats.addItemCount( -static_cast<int64>( getCount() ) );
            _mapCurrent = unordered_map<uint64, float2>{};
            _mapOlder   = unordered_map<uint64, float2>{};
            _stats.recordClear();
        }

        /** @brief 두 세대에 든 항목 수입니다. 언제나 `2 × generationCapacity` 이하입니다. */
        size_t getCount() const { return _mapCurrent.size() + _mapOlder.size(); }
        /** @brief 한 세대가 담는 항목 수입니다. */
        size_t getGenerationCapacity() const { return _generationCapacity; }

    private:
        /** @brief 새 키를 현재 세대에 넣고 그 값을 돌려줍니다. 현재가 가득이면 이전을 버리고 현재를 이전으로 넘긴 뒤 넣습니다. */
        float2& insertCurrent( uint64 key, const float2& size )
        {
            if ( _generationCapacity <= _mapCurrent.size() )
            {
                _stats.addItemCount( -static_cast<int64>( _mapOlder.size() ) );
                _mapOlder   = std::move( _mapCurrent );
                _mapCurrent = unordered_map<uint64, float2>{};
            }
            _stats.addItemCount( 1 );
            return _mapCurrent.emplace( key, size ).first->second;
        }

        unordered_map<uint64, float2> _mapCurrent; ///< 현재 세대입니다. 새로 잰 것과 승격한 것이 들어옵니다.
        unordered_map<uint64, float2> _mapOlder;   ///< 이전 세대입니다. 다음 교대에 통째로 버려집니다.
        size_t                        _generationCapacity;
        CacheStatsHandle              _stats;
    };
} // namespace sw
