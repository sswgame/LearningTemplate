/**
 * @file ITextShaper.h
 * @brief 셰이핑 계약입니다 — 같은 면 · 같은 방향의 글 한 런을 글리프 열(번호 · 클러스터 · 전진 · 오프셋)로 바꿉니다.
 * @details 언리얼 `FShapedGlyphSequence` · HarfBuzz `hb_shape` 의 입출력과 같은 모양입니다. 길이는 em 비율이고 크기는 배치가 곱합니다.
 *          클러스터 = 그 글리프가 나온 글의 **바이트 위치**입니다 — 커서 · 선택 · 줄 바꿈이 글 위치로 되돌아가는 근거입니다.
 *          HarfBuzz 는 이 자리에 같은 계약으로 꽂습니다(런 하나, 같은 글꼴 바이트 `IFontRasterizer::findFaceBytes` 로 `hb_face` 를 만든다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Text/TextTypes.h"

namespace sw
{
    class IFontRasterizer;

    /** @brief 글의 쓰기 방향입니다. */
    enum class TextDirection : uint8
    {
        LeftToRight,
        RightToLeft
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이핑 결과 글리프 하나입니다(em 비율). */
    struct ShapedGlyph
    {
        float2     _offset{};                   ///< 원점에서의 그림 위치 조정(결합 문자 · 위치 표 — 단순 셰이퍼는 0)
        float32    _advance{ 0.0f };            ///< 다음 글리프까지(커닝 포함)
        uint32     _glyphIndex{ 0 };            ///< 면 안의 글리프 번호(0 = 두부)
        uint32     _cluster{ 0 };               ///< 원문 바이트 위치
        uint32     _codepoint{ 0 };             ///< 이 글리프가 나온 코드 포인트(줄 바꿈 판정이 읽는다 — 합자면 클러스터 첫 코드 포인트)
        FontFaceId _face{ kInvalidFontFaceId }; ///< 글리프를 가진 면
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이핑할 런 하나입니다 — 한 면 · 한 방향. */
    struct ShapingRun
    {
        string_view   _text{};                                  ///< 런의 글(전체 글의 부분)
        uint32        _byteOffset{ 0 };                         ///< 전체 글 안의 시작 바이트 — 클러스터를 전체 기준으로 맞춘다
        FontFaceId    _face{ kInvalidFontFaceId };              ///< 런의 면
        TextDirection _direction{ TextDirection::LeftToRight }; ///< 런의 방향
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITextShaper
     * @brief 런 하나를 셰이핑합니다. 결과는 **논리 순서**(글 순서)로 붙입니다 — RTL 을 눈에 보이는 순서로 뒤집는 것은 배치의 일입니다.
     */
    class SW_API ITextShaper
    {
    public:
        ITextShaper()          = default;
        virtual ~ITextShaper() = default;

        ITextShaper( const ITextShaper& )            = delete;
        ITextShaper& operator=( const ITextShaper& ) = delete;
        ITextShaper( ITextShaper&& )                 = delete;
        ITextShaper& operator=( ITextShaper&& )      = delete;

        /** @brief @p run 을 셰이핑해 @p inoutListGlyph 뒤에 붙입니다. */
        virtual void shape( const IFontRasterizer& rasterizer, const ShapingRun& run, vector<ShapedGlyph>& inoutListGlyph ) const = 0;
    };
} // namespace sw
