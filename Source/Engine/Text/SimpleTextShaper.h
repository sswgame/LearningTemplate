/**
 * @file SimpleTextShaper.h
 * @brief 단순 셰이퍼입니다 — 코드 포인트 → 글리프(cmap) + 커닝. 합자 · 위치 표 · 결합 문자 겹쳐 그리기는 없습니다.
 * @details 라틴 · 한글(완성형 음절) · 한자 · 가나는 이것으로 맞습니다. 아랍어 연결형 · 인도계 문자 · 합자 · 결합 분음(U+0300..U+036F)은
 *          HarfBuzz 가 필요한 경계입니다 — 결합 분음 · 폭 없는 제어 문자는 글리프를 내지 않고 버립니다(Engine/Text/README.md).
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Text/ITextShaper.h"

namespace sw
{
    /**
     * @class SimpleTextShaper
     * @brief cmap + 커닝 셰이퍼입니다(`ITextShaper` 의 기본 구현).
     */
    class SW_API SimpleTextShaper final : public ITextShaper
    {
    public:
        void shape( const IFontRasterizer& rasterizer, const ShapingRun& run, vector<ShapedGlyph>& inoutListGlyph ) const override;
    };
} // namespace sw
