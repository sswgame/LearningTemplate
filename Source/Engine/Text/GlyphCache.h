/**
 * @file GlyphCache.h
 * @brief (면, 글리프 번호)에서 아틀라스 사각형과 래스터 메트릭을 찾는 테이블입니다. 없으면 래스터화해 아틀라스에 올립니다(게임 스레드 전용).
 * @details 언리얼 `FSlateFontCache`, 유니티 TextCore 동적 아틀라스에 해당한다. "최근" 은 최근 `GlyphAtlasLayout::_retainFrameCount` 프레임 안에 찾은 글리프다.
 *          아틀라스가 가득 차면 (1) 글리프가 모두 최근이 아닌 페이지를 오래된 순으로 하나 통째로 비우고, (2) 그런 페이지가 없으면 그 글리프를 `Deferred` 로 미루고
 *          다음 `beginFrame` 에 압축한다 — 오래된 글리프가 있는 페이지를 비우고 최근 글리프는 바이트를 복사해 새 자리로 옮긴다(다시 래스터화하지 않는다).
 *          압축은 프레임 첫머리에만 해서 이번 프레임에 이미 그린 글리프의 사각형이 어긋나지 않고, 이번 프레임에 쓴 글리프는 어느 길에서도 버리지 않는다.
 *          (감사 RN-14: 예전에는 이번 프레임에 아직 안 찾은 페이지를 프레임 중간에 통째로 비워, 지난 프레임에 쓰고 이번 프레임 뒤쪽에 다시 쓸 글리프까지
 *          매 프레임 다시 래스터화했다 — 한글처럼 글자 수가 많은 글에서 폭주. 페이지 중간 압축은 이번 프레임 보호가 페이지마다 번져 더 나빴다.
 *          언리얼 Slate 도 아틀라스가 차면 플러시를 요청해 다음 프레임 첫머리에 비운다 — Slate 는 통째로, 여기서는 최근 글리프를 옮겨 살린다.)
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Text/GlyphAtlas.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    class IFontRasterizer;

    /** @brief 아틀라스에 올린 글리프 하나입니다. */
    struct CachedGlyph
    {
        GlyphAtlasRect _rect{};               ///< 빈 글리프(공백)는 크기 0
        float2         _bearingPx{};          ///< 원점에서 비트맵 왼쪽 위까지입니다(래스터 픽셀이며 SDFRasterParams::_pixelSize 기준, y 는 기준선 위가 +).
        float32        _advancePx{ 0.0f };    ///< 래스터 픽셀 전진
        uint32         _atlasGeneration{ 0 }; ///< 올리거나 옮긴 때의 아틀라스 세대입니다. 다르면 페이지가 비워졌을 수 있습니다.
        uint64         _lastUsedFrame{ 0 };   ///< 마지막으로 찾은 프레임입니다(압축 때 옮겨 살릴지의 근거).
    };
} // namespace sw

namespace sw
{
    /** @brief `GlyphCache::getOrCreateGlyph` 의 결과 상태입니다. 실패와 미룸을 가르는 것이 이유입니다 — 미룬 글리프는 다음 프레임에 다시 부르면 만들어지지만 실패는 그렇지 않습니다. */
    enum class GlyphLookupStatus : uint8
    {
        Ready,    ///< 글리프가 표에 있다(`_pGlyph` 가 있다)
        Deferred, ///< 이번 프레임의 래스터화 예산이 다 찼거나 아틀라스가 차 다음 프레임 첫머리에 압축한다. 글리프는 아직 없다 — 부른 쪽이 다음 프레임에 다시 그려야 한다
        Failed    ///< 래스터화 실패나 글리프가 아틀라스 페이지보다 크다(다시 불러도 같은 결과다)
    };

    /** @brief `GlyphCache::getOrCreateGlyph` 의 결과입니다. */
    struct GlyphLookup
    {
        const CachedGlyph* _pGlyph{ nullptr };                   ///< `Ready` 일 때만 있습니다.
        GlyphLookupStatus  _status{ GlyphLookupStatus::Failed }; ///< 결과 상태입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class GlyphCache
     * @brief 글리프 캐시입니다. `FontService` 이 하나 듭니다(`getGlyphCache()`).
     */
    class SW_API GlyphCache
    {
    public:
        /** @brief @p kind 의 거리장으로 글리프를 만들어 @p atlasLayout 모양의 아틀라스에 올립니다(종류 · 모양은 만들 때 정해 바뀌지 않는다). */
        GlyphCache( IFontRasterizer& rasterizer, GlyphFieldKind kind, const GlyphAtlasLayout& atlasLayout );

        GlyphCache( const GlyphCache& )            = delete;
        GlyphCache& operator=( const GlyphCache& ) = delete;

        /**
         * @brief 프레임 시계를 하나 올립니다. 엔진이 게임 프레임마다 정확히 한 번 부릅니다(`Engine::updateFrameServices`).
         * @details 래스터화 예산과 아틀라스 페이지 사용 표시가 쓰는 프레임 번호의 유일한 출처다. 프로파일러의 프레임 수는 프로파일러가 꺼지면 늘 0 이라
         *          예산이 되살아나지 않아 새 글리프가 영영 안 만들어졌다 — 켜고 끄는 시계에서 번호를 가져오지 않는다.
         *          지난 프레임에 아틀라스가 찼으면 여기서 압축한다(파일 머리 참고).
         */
        void beginFrame();
        /** @brief 지금 프레임 번호입니다(`beginFrame` 을 부를 때마다 하나 오르며 처음은 0 입니다). `getOrCreateGlyph` 의 `frameIndex` 로 넘깁니다. */
        uint64 getFrameIndex() const { return _frameIndex; }

        /**
         * @brief 글리프를 찾고, 없으면 래스터화해 아틀라스에 올립니다. 이번 프레임에 쓴 페이지로 표시합니다.
         * @details 아틀라스가 가득 차면 최근 글리프가 없는 페이지를 비워 자리를 내고, 그런 페이지가 없으면 `Deferred` 를 반환하고 다음 `beginFrame` 에 압축한다
         *          (그 프레임의 나머지 새 글리프도 래스터화하지 않고 `Deferred` — 파일 머리 참고). 압축해도 비울 것이 없으면(최근 글리프만으로 아틀라스가 찬다) 한 번 경고한다.
         *          래스터화가 실패하거나 글리프가 페이지보다 크면 `Failed` 이다.
         *          프레임마다의 래스터화 시간 예산(`setRasterBudgetNanoseconds`)이 이미 찼으면 새 글리프를 만들지 않고 `Deferred` 를 반환한다.
         *          프레임의 첫 새 글리프는 예산이 0 보다 크면 늘 만든다(글리프 하나가 예산을 넘겨도 프레임마다 하나씩은 나아간다).
         *          돌려준 포인터는 다음 `getOrCreateGlyph` 까지만 유효하다(테이블이 늘어나면 위치가 옮겨진다).
         */
        [[nodiscard]] GlyphLookup getOrCreateGlyph( FontFaceID face, uint32 glyphIndex, uint64 frameIndex );
        /**
         * @brief 한 프레임에 새 글리프를 만드는 데 쓸 수 있는 시간(나노초)입니다. 0 이면 제한이 없습니다(테스트 · 벤치).
         * @details 프레임은 `getOrCreateGlyph` 의 `frameIndex` 로 가른다. 값의 정본은 데이터이며 부르는 쪽(`UISystem`)이 `gv_uiGlyphRasterBudgetMicros` 에서 넣는다.
         */
        void setRasterBudgetNanoseconds( int64 budgetNanoseconds ) { _rasterBudgetNanoseconds = budgetNanoseconds; }
        /** @brief 면 하나의 글리프를 모두 테이블에서 지웁니다(면을 닫을 때). 아틀라스 영역은 페이지를 비울 때 돌아옵니다. */
        void removeFace( FontFaceID face );

        /** @brief 아틀라스입니다(렌더러에 업로드를 넘길 때 `takeUploads`). */
        GlyphAtlas&       getAtlas() { return _atlas; }
        const GlyphAtlas& getAtlas() const { return _atlas; }
        /** @brief 래스터화 인자입니다(래스터 픽셀 → em 비율 환산에 `_pixelSize`). */
        const SDFRasterParams& getRasterParams() const { return _rasterParams; }
        /** @brief 글리프를 담는 거리장 종류입니다. */
        GlyphFieldKind getFieldKind() const { return _atlas.getFieldKind(); }
        /** @brief 표의 글리프 수입니다. */
        uint32 getGlyphCount() const { return static_cast<uint32>( _mapGlyph.size() ); }

    private:
        /** @brief 테이블 키는 (면 << 32) | 글리프입니다. */
        static uint64 makeKey( FontFaceID face, uint32 glyphIndex ) { return ( static_cast<uint64>( face ) << 32 ) | glyphIndex; }
        /** @brief 표에 없는 글리프를 래스터화해 아틀라스에 올리고 표에 넣습니다. 자리가 없으면 압축을 요청하고 `Deferred` 입니다. */
        [[nodiscard]] GlyphLookup rasterizeAndAdd( FontFaceID face, uint32 glyphIndex, uint64 frameIndex );
        /** @brief 영역을 잡고, 자리가 없으면 글리프가 모두 최근이 아닌 가장 오래된 페이지를 통째로 비워 자리를 냅니다. 그런 페이지가 없으면 false 입니다. */
        [[nodiscard]] bool allocateOrEvictStalePage( uint32 width, uint32 height, uint64 frameIndex, GlyphAtlasRect& outRect );
        /** @brief 글리프가 @p frameIndex 기준 최근 `_retainFrameCount` 프레임 안에 쓰이지 않았는지 봅니다. */
        [[nodiscard]] bool isStale( const CachedGlyph& glyph, uint64 frameIndex ) const;
        /** @brief 오래된 글리프가 있는 페이지를 모두 비우고 그 페이지의 최근 글리프를 바이트 복사로 새 자리에 옮깁니다(프레임 첫머리에만). 오래된 글리프는 표에서 지웁니다. */
        void compactStaleGlyphs( uint64 frameIndex );
        /** @brief 페이지에 있던 글리프를 모두 표에서 지우고 페이지를 비웁니다. */
        void removePage( uint32 page );

    private:
        IFontRasterizer&                   _rasterizer;
        GlyphAtlas                         _atlas;
        unordered_map<uint64, CachedGlyph> _mapGlyph;                ///< 키 = makeKey
        SDFRasterParams                    _rasterParams;            ///< 래스터화 인자(모든 글리프 같은 크기)
        SDFGlyphBitmap                     _scratchBitmap;           ///< 래스터 결과를 받는 재사용 버퍼입니다.
        vector<uint32>                     _listEvictablePage;       ///< 비울 페이지 후보를 받는 재사용 버퍼입니다.
        vector<uint8>                      _listCompactPage;         ///< 압축할 페이지 표시입니다(페이지 번호 → SW_TRUE). 재사용 버퍼입니다.
        vector<uint64>                     _listRetainKey;           ///< 옮겨 살릴 글리프 키를 받는 재사용 버퍼입니다.
        vector<uint8>                      _retainBytes;             ///< 옮겨 살릴 글리프 바이트를 이어 붙인 재사용 버퍼입니다(`_listRetainKey` 순서).
        uint32                             _retainFrameCount;        ///< 최근으로 치는 프레임 수입니다(`GlyphAtlasLayout::_retainFrameCount`).
        int64                              _rasterBudgetNanoseconds; ///< 프레임당 새 글리프 시간 예산입니다(0 이면 제한 없음).
        int64                              _spentNanoseconds;        ///< `_budgetFrameIndex` 프레임에 새 글리프에 쓴 시간입니다.
        uint64                             _budgetFrameIndex;        ///< `_spentNanoseconds` 가 센 프레임입니다(다른 프레임 번호가 오면 0 으로 되돌립니다).
        uint64                             _frameIndex;              ///< 프레임 시계입니다(`beginFrame` 이 올린다).
        uint8                              _bCompactRequested : 1;   ///< 이번 프레임에 아틀라스가 찼다 — 다음 `beginFrame` 이 압축한다.
        uint8                              _bAtlasFullWarned  : 1;   ///< 최근 글리프만으로 찬 아틀라스를 이미 경고했다(압축이 무언가 비우면 되돌린다).
    };
} // namespace sw
