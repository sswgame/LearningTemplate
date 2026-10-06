#include "pch.h"

#include "Editor/Panels/UiPreviewLogic.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Layout/UiScale.h"

namespace sw::editor
{
    namespace
    {
        struct UiPreviewLogicInternal
        {
            static constexpr UiPreviewResolution kArrResolution[] = {
                {       "1280x720 (HD)", 1280,  720},
                { "1920x1080 (Full HD)", 1920, 1080},
                {     "2560x1440 (QHD)", 2560, 1440},
                {      "3840x2160 (4K)", 3840, 2160},
                {    "2560x1080 (21:9)", 2560, 1080},
                {"1080x1920 (Portrait)", 1080, 1920},
            };
            static constexpr uint32 kResolutionCount = static_cast<uint32>( sizeof( kArrResolution ) / sizeof( kArrResolution[0] ) );
        };
    } // namespace

    uint32 UiPreviewLogic::getResolutionCount()
    {
        return UiPreviewLogicInternal::kResolutionCount;
    }

    const UiPreviewResolution& UiPreviewLogic::getResolution( uint32 index )
    {
        return UiPreviewLogicInternal::kArrResolution[index < UiPreviewLogicInternal::kResolutionCount ? index : 0];
    }

    UiViewport UiPreviewLogic::makeViewport( const UiScaleSettings& settings, uint32 index, float32 userScale, float32 safeZoneRatio )
    {
        const UiPreviewResolution& resolution = getResolution( index );
        const float2               physicalSize{ static_cast<float32>( resolution._width ), static_cast<float32>( resolution._height ) };
        // 창 OS 배율은 1 — 미리보기는 해상도 규칙과 사용자 배율만 본다(모니터 DPI 는 게임 창의 일).
        return UiScaleUtil::makeViewport( settings, physicalSize, userScale, 1.0f, safeZoneRatio );
    }

    float2 UiPreviewLogic::fitImage( const float2& imageSize, const float2& available )
    {
        if ( imageSize._x <= 0.0f || imageSize._y <= 0.0f || available._x <= 0.0f || available._y <= 0.0f )
            return float2{};
        const float32 scale = MathUtil::min( 1.0f, MathUtil::min( available._x / imageSize._x, available._y / imageSize._y ) );
        return float2{ imageSize._x * scale, imageSize._y * scale };
    }

    float2 UiPreviewLogic::mapImageToUi( const float2& imagePoint, const float2& drawSize, const UiViewport& viewport )
    {
        if ( drawSize._x <= 0.0f || drawSize._y <= 0.0f )
            return float2{};
        return float2{ imagePoint._x / drawSize._x * viewport._size._x, imagePoint._y / drawSize._y * viewport._size._y };
    }

    UiRect UiPreviewLogic::mapUiToImage( const UiRect& rect, const float2& drawSize, const UiViewport& viewport )
    {
        if ( viewport._size._x <= 0.0f || viewport._size._y <= 0.0f )
            return UiRect{};
        const float32 scaleX = drawSize._x / viewport._size._x;
        const float32 scaleY = drawSize._y / viewport._size._y;
        return UiRect{ rect._left * scaleX, rect._top * scaleY, rect._right * scaleX, rect._bottom * scaleY };
    }

    void UiPreviewLogic::collectWidgetRows( const WidgetTree& tree, vector<UiPreviewWidgetRow>& outListRow )
    {
        outListRow.clear();
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( const Widget* pWidget : listWidget )
        {
            UiPreviewWidgetRow row{};
            row._widget = pWidget->getId();
            for ( const PanelWidget* pParent = pWidget->getParent(); pParent != nullptr; pParent = pParent->getParent() )
                ++row._depth;
            const TypeInfo* pType = pWidget->getTypeInfo();
            row._label            = pType != nullptr ? string( pType->_name.c_str() ) : string( "Widget" );
            if ( pWidget->getName().empty() == false )
                row._label += string( " #" ) + pWidget->getName().c_str();
            outListRow.push_back( row );
        }
    }

    WidgetId UiPreviewLogic::findWidgetAt( const WidgetTree& tree, const float2& point )
    {
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( size_t index = listWidget.size(); index > 0; --index )
        {
            const Widget* pWidget  = listWidget[index - 1];
            bool          bVisible = pWidget->isVisible();
            for ( const PanelWidget* pParent = pWidget->getParent(); pParent != nullptr && bVisible; pParent = pParent->getParent() )
                bVisible = pParent->isVisible(); // 접힌 조상 아래의 기하는 지난 배치다
            if ( bVisible == false )
                continue;
            const UiRect bounds = pWidget->getGeometry().computeScreenBounds();
            if ( bounds._left <= point._x && point._x < bounds._right && bounds._top <= point._y && point._y < bounds._bottom )
                return pWidget->getId();
        }
        return kInvalidWidgetId;
    }

    string UiPreviewLogic::makeTargetPath( const UiViewport& viewport )
    {
        return "rendertarget/editor_ui_preview_" + to_string( static_cast<uint32>( viewport._physicalSize._x ) ) + "x" +
               to_string( static_cast<uint32>( viewport._physicalSize._y ) );
    }
} // namespace sw::editor
