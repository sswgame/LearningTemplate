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
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    class Texture2D;

    /**
     * @class ImageWidget
     * @brief 브러시(색 · 둥근 모서리 · 9-슬라이스)로 그림을 칠합니다. 그림이 없으면 브러시 색의 상자입니다.
     * @details 원하는 크기 = `_imageSize`(0 이면 텍스처 픽셀 크기, 그림도 없으면 0 — 슬롯 덮어쓰기로 정한다). `_bMirrorInRtl` 이면 오른쪽에서 왼쪽 배치에서
     *          그림을 좌우로 뒤집습니다(화살표 · 진행 방향 아이콘 — UMG 의 Flip For Right To Left Flow Direction).
     *          그림은 둘 중 하나입니다 — 코드가 넘기는 텍스처 객체(`setImage`), 또는 경로(`_imagePath` — 문서 · `setImagePath`). 경로는 게임 스레드에서 풀지 않고
     *          그리기 목록에 경로로 실어 렌더 스레드의 캔버스 렌더러가 `TextureCache` 로 풉니다(머티리얼 텍스처와 같은 길). 경로 그림은 크기를 모르므로
     *          원하는 크기는 `_imageSize`(또는 슬롯 크기 덮어쓰기)입니다.
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
        /** @brief 그림 경로(DDS 리소스 경로)를 바꿉니다. 텍스처 객체(`setImage`)가 있으면 그것이 이깁니다. kPaint. */
        void          setImagePath( string_view imagePath );
        const string& getImagePath() const { return _imagePath; }
        /** @brief 브러시를 바꿉니다. kPaint 만. */
        void           setBrush( const UIBrush& brush );
        const UIBrush& getBrush() const { return _brush; }
        /** @brief 원하는 크기를 정합니다(0 이면 텍스처 크기). kLayout. */
        void setImageSize( const float2& imageSize );
        /** @brief 오른쪽에서 왼쪽 배치에서 좌우로 뒤집는가를 바꿉니다. kPaint. */
        void setMirrorInRtl( bool bMirror );
        bool isMirroredInRtl() const { return _bMirrorInRtl; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   paint( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        shared_ptr<const Texture2D> _image; ///< 칠할 그림(패킷이 수명을 쥔다 — 그리기 목록의 텍스처 참조)
        PROPERTY( DisplayName = "Image", AssetPath, AssetType = "Texture", Tooltip = "Texture path; the canvas renderer resolves it on the render thread" )
        string _imagePath;
        PROPERTY( DisplayName = "Brush" )
        UIBrush _brush;
        PROPERTY( DisplayName = "Image Size", Tooltip = "Desired size; 0 uses the texture size", Meta = "Units=ui" )
        float2 _imageSize;
        PROPERTY( DisplayName = "Mirror In Right To Left", Tooltip = "Flip horizontally when the flow direction is right to left" )
        bool _bMirrorInRtl;
    };
} // namespace sw
