/**
 * @file ITextShaper.h
 * @brief 셰이핑 계약을 정의합니다. 같은 면과 같은 방향의 텍스트 한 런을 글리프 열(번호, 클러스터, 전진, 오프셋)로 바꿉니다.
 * @details 언리얼 `FShapedGlyphSequence` 와 HarfBuzz `hb_shape` 의 입출력과 같은 구조다. 길이는 em 비율이고 크기는 배치가 곱한다.
 *          클러스터는 그 글리프가 나온 텍스트의 **바이트 위치**다. 커서, 선택, 줄 바꿈이 텍스트 위치로 되돌아가는 근거가 된다.
 *          HarfBuzz 는 같은 계약으로 끼운다(런 하나이며, 같은 폰트 바이트 `IFontRasterizer::findFaceBytes` 로 `hb_face` 를 만든다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

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
        float2     _offset{};                   ///< 원점에서의 그리기 위치 조정입니다(결합 문자와 위치 테이블이며, 단순 셰이퍼는 0 입니다).
        float32    _advance{ 0.0f };            ///< 다음 글리프까지(커닝 포함)
        uint32     _glyphIndex{ 0 };            ///< 면 안의 글리프 번호(0 = 두부)
        uint32     _cluster{ 0 };               ///< 원문 바이트 위치
        uint32     _codepoint{ 0 };             ///< 이 글리프가 나온 코드 포인트입니다(줄 바꿈 판정이 읽으며, 합자면 클러스터의 첫 코드 포인트입니다).
        FontFaceID _face{ kInvalidFontFaceID }; ///< 글리프를 가진 면
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이핑할 런 하나입니다. 한 면과 한 방향을 가집니다. */
    struct ShapingRun
    {
        string_view   _text{};                                  ///< 런의 글(전체 글의 부분)
        uint32        _byteOffset{ 0 };                         ///< 전체 텍스트 안의 시작 바이트입니다. 클러스터를 전체 기준으로 맞춥니다.
        FontFaceID    _face{ kInvalidFontFaceID };              ///< 런의 면입니다.
        TextDirection _direction{ TextDirection::LeftToRight }; ///< 런의 방향입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITextShaper
     * @brief 런 하나를 셰이핑합니다. 결과는 **논리 순서**(텍스트 순서)로 붙입니다. RTL 을 눈에 보이는 순서로 뒤집는 것은 배치가 합니다.
     */
    class SW_API ITextShaper
    {
    public:
        /**
         * @brief 이 빌드의 셰이퍼를 만듭니다. 복합 문자 빌드(`SW_ENABLE_COMPLEX_TEXT`)는 HarfBuzz 셰이퍼(`Text/HarfBuzz/`), 아니면 단순 셰이퍼(`SimpleTextShaper`)입니다.
         * @details 고르는 것은 빌드 구성이다 — 실행 중에 라이브러리가 없다고 다른 셰이퍼로 물러나는 길은 없다(구성이 패키지를 못 찾으면 구성이 선다).
         */
        static unique_ptr<ITextShaper> createDefault();

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
