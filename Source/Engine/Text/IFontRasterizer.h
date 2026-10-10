/**
 * @file IFontRasterizer.h
 * @brief 글꼴 래스터라이저 계약입니다 — 글꼴 바이트를 면으로 열고, 메트릭 · 글리프 번호 · 커닝을 답하고, 글리프를 SDF 로 래스터화합니다.
 * @details 구현은 FreeType 하나(`Text/FreeType/`)이고, 시험은 결정적인 가짜 구현을 씁니다. 스레드: 게임 스레드에서만 부릅니다(FreeType 라이브러리
 *          객체는 스레드 안전하지 않습니다). 셰이핑은 이 계약이 아니라 `ITextShaper` 가 합니다(HarfBuzz 를 그 자리에 꽂는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Text/TextTypes.h"

namespace sw
{
    /**
     * @class IFontRasterizer
     * @brief 글꼴 면 · 글리프 래스터화 창구입니다(언리얼 `FFreeTypeFace` · 유니티 `FontEngine` 자리).
     */
    class SW_API IFontRasterizer
    {
    public:
        /** @brief 기본 구현(FreeType)을 만듭니다. 초기화에 실패하면 nullptr 입니다(이유는 로그). */
        static unique_ptr<IFontRasterizer> createDefault();

        IFontRasterizer()          = default;
        virtual ~IFontRasterizer() = default;

        IFontRasterizer( const IFontRasterizer& )            = delete;
        IFontRasterizer& operator=( const IFontRasterizer& ) = delete;
        IFontRasterizer( IFontRasterizer&& )                 = delete;
        IFontRasterizer& operator=( IFontRasterizer&& )      = delete;

        /**
         * @brief 글꼴 파일 바이트에서 면 하나를 엽니다. 바이트는 면이 사는 동안 래스터라이저가 쥡니다.
         * @param faceIndex 컬렉션(.ttc)의 면 번호. 보통 0.
         * @return 면 번호. 열지 못하면 `kInvalidFontFaceID`(이유는 @p debugName 과 함께 로그).
         */
        [[nodiscard]] virtual FontFaceID loadFace( vector<uint8> fileBytes, uint32 faceIndex, string_view debugName ) = 0;
        /** @brief 면을 닫습니다. 없는 번호면 아무것도 하지 않습니다. */
        virtual void unloadFace( FontFaceID face ) = 0;

        /** @brief 면의 세로 메트릭입니다. 없는 면이면 false. */
        [[nodiscard]] virtual bool findFaceMetrics( FontFaceID face, FontFaceMetrics& outMetrics ) const = 0;
        /** @brief 코드 포인트의 글리프 번호입니다. 그 면에 없으면 0(.notdef)입니다 — 대체 사슬이 다음 면을 묻는 근거입니다. */
        virtual uint32 findGlyphIndex( FontFaceID face, uint32 codepoint ) const = 0;
        /** @brief 글리프 메트릭입니다. 없는 면 · 글리프면 false. */
        [[nodiscard]] virtual bool findGlyphMetrics( FontFaceID face, uint32 glyphIndex, GlyphMetrics& outMetrics ) const = 0;
        /** @brief 두 글리프 사이 커닝(em 비율, 보통 음수)입니다. 표가 없으면 0. */
        virtual float32 getKerning( FontFaceID face, uint32 leftGlyph, uint32 rightGlyph ) const = 0;
        /** @brief 면을 열 때 받은 파일 바이트입니다(셰이퍼가 같은 바이트로 자기 면을 만든다). 없는 면이면 nullptr. */
        virtual const vector<uint8>* findFaceBytes( FontFaceID face ) const = 0;

        /**
         * @brief 글리프를 SDF 로 래스터화합니다.
         * @return 실패(없는 면 · 윤곽 없는 비트맵 글꼴 · SDF 모듈 없음)면 false 와 로그. 공백처럼 윤곽이 빈 글리프는 true + 크기 0.
         */
        [[nodiscard]] virtual bool rasterizeSdf( FontFaceID face, uint32 glyphIndex, const SdfRasterParams& params, SdfGlyphBitmap& outBitmap ) = 0;
    };
} // namespace sw
