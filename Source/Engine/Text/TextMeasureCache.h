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
 *          **키는 해시 후 원문 확인입니다.** 표 키는 64 비트 해시지만 항목이 글 사본과 문맥 해시를 함께 들고, 조회가 해시로 칸을 찾은 뒤 둘이 같은지 확인한다(`TextMeasureKey`).
 *          해시가 부딪혀도 다른 글의 크기를 돌려주지 않고 실패로 센다(그 칸은 다음 `insert` 가 덮어쓴다). 값 비교의 비용은 **넣을 때** 글 복사 한 번(넣는 자리는 실패 경로라 방금 글을
 *          통째로 배치했으니 복사는 잡음 수준)과 **적중할 때** `memcmp` 한 번(해시가 이미 글을 한 번 훑었다)이다. 메모리는 항목마다 글 길이만큼 늘고
 *          상한 안(세대당 4096 개 × 2)이라 글이 길어도 수 MB 이내다. 문맥(스타일 · 너비 · 면 연쇄 · 리치 구간)은 사본 대신 64 비트 해시로 확인한다 — 글이 같고 문맥 해시만
 *          부딪혀야 틀리고(2^-64), 사본을 들면 `TextLayoutStyle` 전체와 구간 목록을 복사해야 한다.
 *
 *          스레드: 게임 스레드에서만 씁니다(`TextLayoutEngine` 이 게임 스레드 전용).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @class TextMeasureKey
     * @brief 측정 캐시의 조회 키입니다. 글과 문맥 해시로만 만들 수 있어, 해시만 들고 조회하는 길이 없습니다.
     * @details 표 키(`getHash`)는 글을 문맥 해시로 시드해 훑은 64 비트 값이고, 칸을 찾은 뒤 글 · 문맥 해시가 같은지 `TextMeasureCache` 가 확인합니다.
     *          글은 **보기**만 들므로 키보다 글이 오래 살아야 합니다(조회 · 넣기 호출 동안만).
     */
    class TextMeasureKey
    {
    public:
        /**
         * @param text        측정하는 글입니다(키가 쓰이는 동안 살아 있어야 합니다).
         * @param contextHash 글 밖에서 결과를 정하는 입력(스타일 · 너비 · 면 연쇄 · 리치 구간)을 섞은 해시입니다.
         */
        TextMeasureKey( string_view text, uint64 contextHash )
            : _hash{ HashUtil::computeStringHash64( text.data(), text.size(), false, contextHash ) }
            , _text{ text }
            , _contextHash{ contextHash }
        {
        }

        /**
         * @brief 해시를 직접 정합니다 — 부딪힘을 일부러 만드는 테스트의 자리입니다. 생산 코드는 위 생성자를 씁니다.
         */
        static TextMeasureKey fromHashForTest( uint64 hash, string_view text, uint64 contextHash ) { return TextMeasureKey{ hash, text, contextHash }; }

        uint64      getHash() const { return _hash; }
        string_view getText() const { return _text; }
        uint64      getContextHash() const { return _contextHash; }

    private:
        TextMeasureKey( uint64 hash, string_view text, uint64 contextHash )
            : _hash{ hash }
            , _text{ text }
            , _contextHash{ contextHash }
        {
        }

        uint64      _hash;
        string_view _text;
        uint64      _contextHash;
    };
} // namespace sw

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
         * @details 해시가 같아도 글 · 문맥 해시가 다르면(해시 부딪힘) 없는 것으로 칩니다 — 실패로 센다.
         *          돌려준 포인터는 다음 `find` · `insert` · `clear` 까지만 유효합니다(승격이나 세대 교대가 표를 바꾼다).
         */
        const float2* find( const TextMeasureKey& key )
        {
            const uint64 hash        = key.getHash();
            const auto   currentIter = _mapCurrent.find( hash );
            if ( currentIter != _mapCurrent.end() )
            {
                if ( isSameKey( currentIter->second, key ) == false )
                {
                    _stats.recordMiss(); // 부딪힘: 이 해시는 다른 글의 칸이다. 같은 해시가 이전 세대에도 있을 수 없다(넣을 때 이전의 같은 해시를 지운다)
                    return nullptr;
                }
                _stats.recordHit();
                return &currentIter->second._size;
            }
            const auto previousIter = _mapOlder.find( hash );
            if ( previousIter == _mapOlder.end() || isSameKey( previousIter->second, key ) == false )
            {
                _stats.recordMiss();
                return nullptr;
            }
            _stats.recordHit();
            Entry entry = std::move( previousIter->second );
            _mapOlder.erase( previousIter );
            _stats.addItemCount( -1 ); // 아래 insertCurrent 가 다시 하나를 더한다 — 승격은 항목 수를 바꾸지 않는다
            return &insertCurrent( hash, std::move( entry ) )._size;
        }

        /**
         * @brief 키의 크기를 현재 세대에 넣습니다. 현재가 차 있으면 먼저 세대를 교대합니다. 같은 해시의 칸이 있으면(같은 키든 부딪힌 다른 키든) 새 키와 값으로 덮어씁니다.
         */
        void insert( const TextMeasureKey& key, const float2& size )
        {
            const uint64 hash         = key.getHash();
            const auto   previousIter = _mapOlder.find( hash );
            if ( previousIter != _mapOlder.end() )
            {
                _mapOlder.erase( previousIter );
                _stats.addItemCount( -1 );
            }
            const auto currentIter = _mapCurrent.find( hash );
            if ( currentIter != _mapCurrent.end() )
            {
                currentIter->second = makeEntry( key, size );
                return;
            }
            (void)insertCurrent( hash, makeEntry( key, size ) );
        }

        /** @brief 두 세대를 모두 비웁니다. 비우기 횟수를 하나 셉니다. */
        void clear()
        {
            _stats.addItemCount( -static_cast<int64>( getCount() ) );
            _mapCurrent = unordered_map<uint64, Entry>{};
            _mapOlder   = unordered_map<uint64, Entry>{};
            _stats.recordClear();
        }

        /** @brief 두 세대에 든 항목 수입니다. 언제나 `2 × generationCapacity` 이하입니다. */
        size_t getCount() const { return _mapCurrent.size() + _mapOlder.size(); }
        /** @brief 한 세대가 담는 항목 수입니다. */
        size_t getGenerationCapacity() const { return _generationCapacity; }

    private:
        /** @brief 칸 하나입니다. 해시로 찾은 뒤 글 · 문맥 해시가 같을 때만 `_size` 가 답입니다. */
        struct Entry
        {
            string _text{};
            uint64 _contextHash{ 0 };
            float2 _size{};
        };

        static Entry makeEntry( const TextMeasureKey& key, const float2& size )
        {
            Entry entry{};
            entry._text        = string( key.getText() );
            entry._contextHash = key.getContextHash();
            entry._size        = size;
            return entry;
        }

        static bool isSameKey( const Entry& entry, const TextMeasureKey& key ) { return entry._contextHash == key.getContextHash() && string_view{ entry._text } == key.getText(); }

        /** @brief 새 칸을 현재 세대에 넣고 그 값을 돌려줍니다. 현재가 가득이면 이전을 버리고 현재를 이전으로 넘긴 뒤 넣습니다. */
        Entry& insertCurrent( uint64 hash, Entry&& entry )
        {
            if ( _generationCapacity <= _mapCurrent.size() )
            {
                _stats.addItemCount( -static_cast<int64>( _mapOlder.size() ) );
                _mapOlder   = std::move( _mapCurrent );
                _mapCurrent = unordered_map<uint64, Entry>{};
            }
            _stats.addItemCount( 1 );
            return _mapCurrent.emplace( hash, std::move( entry ) ).first->second;
        }

        unordered_map<uint64, Entry> _mapCurrent; ///< 현재 세대입니다. 새로 잰 것과 승격한 것이 들어옵니다.
        unordered_map<uint64, Entry> _mapOlder;   ///< 이전 세대입니다. 다음 교대에 통째로 버려집니다.
        size_t                       _generationCapacity;
        CacheStatsHandle             _stats;
    };
} // namespace sw
