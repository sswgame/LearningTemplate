/**
 * @file WaylandTextPainter.h
 * @brief CPU 픽셀 버퍼에 UTF-8 글을 그립니다 — Wayland 에는 X11 의 서버 글꼴(`XDrawString` · Xft)이 없어 스플래시 창 · 개발 콘솔 덧그림이 이것으로 그린다.
 * @details 엔진의 글꼴 래스터라이저(`IFontRasterizer`, FreeType)로 시스템 글꼴의 거리장(SDF) 글리프를 만들고 윤곽(거리 0)을 기준으로 가장자리 덮임 비율을 구해 찍는다.
 *          글꼴은 라틴 고정폭 한 벌과 한글 · 일본어를 채우는 폴백 한 벌이다(`SystemFontLocator` 가 이름으로 찾는다). 라틴 글꼴이 하나도 없으면 `initialize` 가 false 다(깨진 글자로 그리지 않는다).
 *          폴백 글꼴이 없으면 그 글꼴에만 있는 글자는 건너뛴다. 그린 글리프는 코드 포인트별로 기억한다. 게임 스레드에서만 부른다(래스터라이저가 그렇다).
 *          픽셀은 XRGB8888(`WaylandShmBuffer`)이고 행이 빈틈없이 붙어 있다(한 행 = @c width 픽셀).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Text/TextTypes.h"

namespace sw
{
    class IFontRasterizer;

    /** @class WaylandTextPainter @brief 파일 머리말 참고. */
    class WaylandTextPainter
    {
    public:
        WaylandTextPainter();
        ~WaylandTextPainter();

        WaylandTextPainter( const WaylandTextPainter& )            = delete;
        WaylandTextPainter& operator=( const WaylandTextPainter& ) = delete;

        /** @brief 글자 크기(em 의 픽셀 수)를 정하고 글꼴을 엽니다. 라틴 글꼴을 못 열면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool initialize( uint32 pixelSize );

        /** @brief 기준선 위 높이(픽셀)입니다. */
        int32 getAscent() const { return _ascent; }
        /** @brief 줄 높이(픽셀)입니다. */
        int32 getLineHeight() const { return _lineHeight; }

        /**
         * @brief @p text 를 기준선 왼쪽 끝 (@p originX, @p baselineY) 에서 @p rgb(0x00RRGGBB)로 그립니다. 버퍼 밖은 자른다.
         * @return 그린 만큼의 가로 전진(픽셀)입니다.
         */
        int32 drawText( uint32* pPixels, uint32 width, uint32 height, int32 originX, int32 baselineY, string_view text, uint32 rgb );

    private:
        /** @brief 글리프 하나의 덮임 비율(0 ~ 255) 비트맵과 배치입니다. 글꼴에 없는 글자는 `_bAvailable` 이 false 다. */
        struct Glyph
        {
            vector<uint8> _listCoverage{};
            int32         _left{ 0 }; ///< 원점에서 비트맵 왼쪽까지
            int32         _top{ 0 };  ///< 기준선에서 비트맵 위쪽까지(위가 +)
            uint32        _width{ 0 };
            uint32        _height{ 0 };
            int32         _advance{ 0 };
            bool          _bAvailable{ false };
        };

        const Glyph& findGlyph( uint32 codepoint );
        bool         rasterizeGlyph( uint32 codepoint, Glyph& outGlyph );
        /** @brief 후보 파일 이름 @p listFileName 가운데 시스템 글꼴 폴더에서 처음 열리는 것을 연다. 못 열면 `kInvalidFontFaceID` 다. */
        FontFaceID openFirstFace( vector_reference<const string_view> listFileName );

        unique_ptr<IFontRasterizer>  _pRasterizer;
        FontFaceID                   _latinFace;
        FontFaceID                   _fallbackFace;
        uint32                       _pixelSize;
        int32                        _ascent;
        int32                        _lineHeight;
        unordered_map<uint32, Glyph> _mapGlyph;
    };
} // namespace sw
