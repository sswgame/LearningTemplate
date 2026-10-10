#include "pch.h"

#include "Engine/UI/Widgets/ImageWidget.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Graphics/Texture/Texture2D.h"

namespace sw
{
    ImageWidget::ImageWidget()
        : Widget{}
        , _image{}
        , _imagePath{}
        , _brush{}
        , _imageSize{}
        , _bMirrorInRtl{ false }
    {
    }

    ImageWidget::~ImageWidget() = default;

    const TypeInfo* ImageWidget::getTypeInfo() const
    {
        return StaticType();
    }

    void ImageWidget::setImage( shared_ptr<const Texture2D> image )
    {
        if ( _image == image )
            return;
        _image = std::move( image );
        invalidate( WidgetDirty::kLayout | WidgetDirty::kPaint );
    }

    void ImageWidget::setImagePath( string_view imagePath )
    {
        if ( _imagePath == imagePath )
            return;
        _imagePath = string( imagePath );
        invalidate( WidgetDirty::kPaint );
    }

    void ImageWidget::setBrush( const UIBrush& brush )
    {
        _brush = brush;
        invalidate( WidgetDirty::kPaint );
    }

    void ImageWidget::setImageSize( const float2& imageSize )
    {
        if ( _imageSize == imageSize )
            return;
        _imageSize = imageSize;
        invalidate( WidgetDirty::kLayout );
    }

    void ImageWidget::setMirrorInRtl( bool bMirror )
    {
        if ( _bMirrorInRtl == bMirror )
            return;
        _bMirrorInRtl = bMirror;
        invalidate( WidgetDirty::kPaint );
    }

    float2 ImageWidget::computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const
    {
        (void)context;
        (void)availableSize;
        if ( _imageSize._x > 0.0f && _imageSize._y > 0.0f )
            return _imageSize;
        if ( _image != nullptr )
            return float2{ static_cast<float32>( _image->getWidth() ), static_cast<float32>( _image->getHeight() ) };
        return float2{};
    }

    void ImageWidget::paint( CanvasPainter& painter, const UIPaintContext& context ) const
    {
        (void)context;
        const bool bImagePath = _image == nullptr && _imagePath.empty() == false;
        if ( _image == nullptr && bImagePath == false && _brush.isInvisible() )
            return;
        CanvasBrush brush = _brush.makeCanvasBrush( _image );
        if ( bImagePath )
            brush._imagePath = hashed_string( _imagePath );
        if ( _bMirrorInRtl && isRightToLeft() )
        {
            const float32 u0 = brush._uvRect._x;
            brush._uvRect._x = brush._uvRect._z;
            brush._uvRect._z = u0;
            // 9-슬라이스 여백도 좌우가 바뀐다.
            const float32 left        = brush._nineSliceMargin._x;
            brush._nineSliceMargin._x = brush._nineSliceMargin._z;
            brush._nineSliceMargin._z = left;
        }
        painter.fillRect( float2{}, getGeometry()._size, brush );
    }
} // namespace sw
