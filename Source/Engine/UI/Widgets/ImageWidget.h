/**
 * @file ImageWidget.h
 * @brief 그림(또는 단색 상자) 하나를 그리는 위젯입니다(UMG Image · 유니티 Image · Godot TextureRect).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Widgets/UiBrush.h"

namespace sw
{
    class Texture2D;

    /**
     * @class ImageWidget
     * @brief 브러시(색 · 둥근 모서리 · 9-슬라이스)로 그림을 칠합니다. 그림이 없으면 브러시 색의 상자입니다.
     * @details 원하는 크기 = `_imageSize`(0 이면 텍스처 픽셀 크기, 그림도 없으면 0 — 슬롯 덮어쓰기로 정한다). `_bMirrorInRtl` 이면 오른쪽에서 왼쪽 배치에서
     *          그림을 좌우로 뒤집습니다(화살표 · 진행 방향 아이콘 — UMG 의 Flip For Right To Left Flow Direction).
     *          `_imagePath` 를 텍스처로 푸는 것은 문서 로드(5-1)의 일입니다 — 지금은 코드가 `setImage` 로 넘깁니다.
     */
    REFLECT( Category = "UI", DisplayName = "Image", Tooltip = "Draws a texture or a solid box with a brush" )
    class SW_API ImageWidget : public Widget
    {
    public:
        REFLECT_BODY();

        ImageWidget();
        ~ImageWidget() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 그림을 바꿉니다(크기가 따라 바뀔 수 있어 kLayout). nullptr 이면 단색 상자입니다. */
        void                               setImage( shared_ptr<const Texture2D> image );
        const shared_ptr<const Texture2D>& getImage() const { return _image; }
        /** @brief 브러시를 바꿉니다. kPaint 만. */
        void           setBrush( const UiBrush& brush );
        const UiBrush& getBrush() const { return _brush; }
        /** @brief 원하는 크기를 정합니다(0 이면 텍스처 크기). kLayout. */
        void setImageSize( const float2& imageSize );
        /** @brief 오른쪽에서 왼쪽 배치에서 좌우로 뒤집는가를 바꿉니다. kPaint. */
        void setMirrorInRtl( bool bMirror );
        bool isMirroredInRtl() const { return _bMirrorInRtl; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   paint( CanvasPainter& painter, const UiPaintContext& context ) const override;

    private:
        shared_ptr<const Texture2D> _image; ///< 칠할 그림(패킷이 수명을 쥔다 — 그리기 목록의 텍스처 참조)
        PROPERTY( DisplayName = "Image", AssetPath, AssetType = "Texture", Tooltip = "Texture path (documents resolve it on load)" )
        string _imagePath;
        PROPERTY( DisplayName = "Brush" )
        UiBrush _brush;
        PROPERTY( DisplayName = "Image Size", Tooltip = "Desired size; 0 uses the texture size", Meta = "Units=ui" )
        float2 _imageSize;
        PROPERTY( DisplayName = "Mirror In Right To Left", Tooltip = "Flip horizontally when the flow direction is right to left" )
        bool _bMirrorInRtl;
    };
} // namespace sw
