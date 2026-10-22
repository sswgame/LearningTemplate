/**
 * @file GlyphCache.h
 * @brief (면, 글리프 번호)에서 아틀라스 사각형과 래스터 메트릭을 찾는 테이블입니다. 없으면 래스터화해 아틀라스에 올립니다(게임 스레드 전용).
 * @details 언리얼 `FSlateFontCache`, 유니티 TextCore 동적 아틀라스에 해당한다. 아틀라스가 가득 차면 이번 프레임에 안 쓴 가장 오래된 페이지를 비우고
 *          그 페이지에 있던 글리프를 테이블에서 지운다. 다음 조회가 다시 래스터화한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
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
        float2         _bearingPx{};          ///< 원점에서 비트맵 왼쪽 위까지입니다(래스터 픽셀이며 SdfRasterParams::_pixelSize 기준, y 는 기준선 위가 +).
        float32        _advancePx{ 0.0f };    ///< 래스터 픽셀 전진
        uint32         _atlasGeneration{ 0 }; ///< 올린 때의 아틀라스 세대입니다. 다르면 페이지가 비워졌을 수 있습니다.
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
        /** @brief @p kind 의 거리장으로 글리프를 만들어 아틀라스에 올립니다(종류는 만들 때 정해 바뀌지 않는다). */
        GlyphCache( IFontRasterizer& rasterizer, GlyphFieldKind kind );

        GlyphCache( const GlyphCache& )            = delete;
        GlyphCache& operator=( const GlyphCache& ) = delete;

        /**
         * @brief 글리프를 찾고, 없으면 래스터화해 아틀라스에 올립니다. 이번 프레임에 쓴 페이지로 표시합니다.
         * @details 아틀라스가 가득 차면 이번 프레임에 안 쓴 가장 오래된 페이지를 비우고(그 페이지 글리프를 테이블에서 지운다) 한 번 더 시도한다.
         *          그래도 자리가 없으면(한 프레임이 페이지를 다 쓴다) nullptr 를 반환하고 경고한다. 그 글자는 이번 프레임에 보이지 않는다. 래스터화가 실패해도 nullptr 이다.
         *          돌려준 포인터는 다음 `findOrAddGlyph` 까지만 유효하다(테이블이 늘어나면 위치가 옮겨진다).
         */
        const CachedGlyph* findOrAddGlyph( FontFaceID face, uint32 glyphIndex, uint64 frameIndex );
        /** @brief 면 하나의 글리프를 모두 테이블에서 지웁니다(면을 닫을 때). 아틀라스 영역은 페이지를 비울 때 돌아옵니다. */
        void forgetFace( FontFaceID face );

        /** @brief 아틀라스입니다(렌더러에 업로드를 넘길 때 `takeUploads`). */
        GlyphAtlas&       getAtlas() { return _atlas; }
        const GlyphAtlas& getAtlas() const { return _atlas; }
        /** @brief 래스터화 인자입니다(래스터 픽셀 → em 비율 환산에 `_pixelSize`). */
        const SdfRasterParams& getRasterParams() const { return _rasterParams; }
        /** @brief 글리프를 담는 거리장 종류입니다. */
        GlyphFieldKind getFieldKind() const { return _atlas.getFieldKind(); }
        /** @brief 표의 글리프 수입니다. */
        uint32 getGlyphCount() const { return static_cast<uint32>( _mapGlyph.size() ); }

    private:
        /** @brief 테이블 키는 (면 << 32) | 글리프입니다. */
        static uint64 makeKey( FontFaceID face, uint32 glyphIndex ) { return ( static_cast<uint64>( face ) << 32 ) | glyphIndex; }
        /** @brief 비운 페이지에 있던 글리프를 표에서 지웁니다. */
        void forgetPage( uint32 page );

        IFontRasterizer&                   _rasterizer;
        GlyphAtlas                         _atlas;
        unordered_map<uint64, CachedGlyph> _mapGlyph;      ///< 키 = makeKey
        SdfRasterParams                    _rasterParams;  ///< 래스터화 인자(모든 글리프 같은 크기)
        SdfGlyphBitmap                     _scratchBitmap; ///< 래스터 결과를 받는 재사용 버퍼입니다.
    };
} // namespace sw
