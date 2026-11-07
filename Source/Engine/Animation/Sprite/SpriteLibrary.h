/**
 * @file SpriteLibrary.h
 * @brief 스프라이트 라이브러리(`.spritelib.xml`)입니다. 분류 · 라벨 → 아틀라스 프레임 표이고, 클립의 프레임(`SpriteClipFrame::_label`)이 라벨로 가리킵니다.
 * @details 클립은 시간과 라벨만 적고 그림은 라이브러리가 정한다 — 라이브러리를 바꾸면 같은 클립이 다른 캐릭터가 된다. 유니티 Sprite Library Asset(Category · Label)과 같은 모델이고
 *          (기억에 의존), 유니티의 Variant Library 자리는 공통 상속 `_base` 다. 분류와 라벨이 열쇠이므로 차이 파일은 바뀐 라벨만 적는다.
 *          아틀라스는 파일에 하나다. 유니티는 라벨마다 다른 텍스처를 쓸 수 있지만, 여기는 프레임이 GPU 인스턴스에 실리고 텍스처는 머티리얼 인스턴스라 프레임을 넘겨도 배치가 깨지지 않게 한다
 *          (`SpriteComponent`). 다른 아틀라스의 변형은 `_base` 위에 `_atlasPath` 를 바꾼 라이브러리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Texture2D;

    /** @brief 라벨 하나입니다. 아틀라스 안의 프레임과 9-슬라이스 테두리를 담습니다(뜻은 `SpriteClipFrame` 의 같은 칸과 같다). */
    REFLECT()
    struct SW_API SpriteLibraryLabel
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Required, Tooltip = "Label a clip frame names (unique in the category)" )
        string _name{};
        PROPERTY( Tooltip = "Frame rectangle in the atlas (u, v, width, height) as fractions of the texture; a negative width mirrors the frame" )
        float4 _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Nine-slice border (left, bottom, right, top) in atlas pixels; all zero is no border" )
        float4 _borderPixels{ 0.0f, 0.0f, 0.0f, 0.0f };
        float4 _border{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< `_borderPixels` 를 프레임 크기로 나눈 비율이다. 읽을 때 채우고 파일에 적지 않는다

        /** @brief 테두리가 하나라도 있으면 true 입니다. */
        bool hasBorder() const { return _border._x > 0.0f || _border._y > 0.0f || _border._z > 0.0f || _border._w > 0.0f; }
    };

    /** @brief 분류 하나입니다(몸 · 머리 · 달리기처럼 묶는 이름). */
    REFLECT()
    struct SW_API SpriteLibraryCategory
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Required, Tooltip = "Category a clip frame names (unique in the library)" )
        string _name{};
        PROPERTY( Tooltip = "Labels of this category" )
        vector<SpriteLibraryLabel> _listLabel;
    };
} // namespace sw

namespace sw
{
    /** @class SpriteLibrary @brief 파일 머리말을 참고하세요. 쓰는 쪽은 `SharedAssetCache<SpriteLibrary>` 로 받아 나눠 씁니다. */
    REFLECT()
    class SW_API SpriteLibrary
    {
        friend struct AssetTypeLoader;

    public:
        REFLECT_BODY();

        /** @brief 라이브러리 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".spritelib.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SpriteLibrary.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "SpriteLibrary", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 빈 라이브러리입니다. .cpp 에 두어 헤더가 `Texture2D` 를 앞선 선언으로만 안다. */
        SpriteLibrary();
        /** @brief XML 본문을 읽습니다(`_base` 를 펼칩니다). 모르는 칸 · 겹친 이름 · 음수 테두리 · 아틀라스 크기 없는 테두리는 오류이고 내용은 비어 있습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 분류 · 라벨의 프레임입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const SpriteLibraryLabel* findLabel( string_view category, string_view label ) const;
        /** @brief 내용을 비웁니다. */
        void clear();

        PROPERTY( Tooltip = "Atlas texture every label's frame is cut from" )
        AssetRef<Texture2D> _atlasPath;
        PROPERTY( Tooltip = "Atlas size in pixels (width, height); needed only by pixel borders, 0 is unknown" )
        float2 _atlasPixelSize{ 0.0f, 0.0f };
        PROPERTY( Tooltip = "Categories of labels" )
        vector<SpriteLibraryCategory> _listCategory;

    private:
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<SpriteLibrary>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 읽은 값의 의미를 보고 비율 테두리를 채웁니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool resolveLoadedValues( string_view sourceLabel );
    };
} // namespace sw
