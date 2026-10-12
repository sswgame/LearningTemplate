#include "pch.h"

#include "Editor/Panels/UIPreview/UIPreviewLogic.h"

#include "Core/Common/Defines.h"
#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"

namespace sw::editor
{
    namespace
    {
        struct UIPreviewLogicInternal
        {
            static constexpr UIPreviewResolution kArrResolution[] = {
                {       "1280x720 (HD)", 1280,  720},
                { "1920x1080 (Full HD)", 1920, 1080},
                {     "2560x1440 (QHD)", 2560, 1440},
                {      "3840x2160 (4K)", 3840, 2160},
                {    "2560x1080 (21:9)", 2560, 1080},
                {"1080x1920 (Portrait)", 1080, 1920},
            };
            static constexpr uint32 kResolutionCount = static_cast<uint32>( sizeof( kArrResolution ) / sizeof( kArrResolution[0] ) );
            /// 의사 문화권 코드(정규 표기)입니다. 엔진 문화권 표(`LocalizationManager::normalizeLanguageCode`)가 쓰는 이름과 같다.
            static constexpr string_view kPseudoAccented = "qps_ploc";
            static constexpr string_view kPseudoMirrored = "qps_plocm";
        };
    } // namespace

    uint32 UIPreviewLogic::getResolutionCount()
    {
        return UIPreviewLogicInternal::kResolutionCount;
    }

    const UIPreviewResolution& UIPreviewLogic::getResolution( uint32 index )
    {
        return UIPreviewLogicInternal::kArrResolution[index < UIPreviewLogicInternal::kResolutionCount ? index : 0];
    }

    UIViewport UIPreviewLogic::makeViewport( const UIScaleSettings& settings, uint32 index, float32 userScale, float32 safeZoneRatio )
    {
        const UIPreviewResolution& resolution = getResolution( index );
        const float2               physicalSize{ static_cast<float32>( resolution._width ), static_cast<float32>( resolution._height ) };
        // 창 OS 배율은 1 — 미리보기는 해상도 규칙과 사용자 배율만 본다(모니터 DPI 는 게임 창의 일).
        return UIScaleUtil::makeViewport( settings, physicalSize, userScale, 1.0f, safeZoneRatio );
    }

    float2 UIPreviewLogic::fitImage( const float2& imageSize, const float2& available )
    {
        if ( imageSize._x <= 0.0f || imageSize._y <= 0.0f || available._x <= 0.0f || available._y <= 0.0f )
            return float2{};
        const float32 scale = MathUtil::min( 1.0f, MathUtil::min( available._x / imageSize._x, available._y / imageSize._y ) );
        return float2{ imageSize._x * scale, imageSize._y * scale };
    }

    float2 UIPreviewLogic::mapImageToUI( const float2& imagePoint, const float2& drawSize, const UIViewport& viewport )
    {
        if ( drawSize._x <= 0.0f || drawSize._y <= 0.0f )
            return float2{};
        return float2{ imagePoint._x / drawSize._x * viewport._size._x, imagePoint._y / drawSize._y * viewport._size._y };
    }

    UIRect UIPreviewLogic::mapUIToImage( const UIRect& rect, const float2& drawSize, const UIViewport& viewport )
    {
        if ( viewport._size._x <= 0.0f || viewport._size._y <= 0.0f )
            return UIRect{};
        const float32 scaleX = drawSize._x / viewport._size._x;
        const float32 scaleY = drawSize._y / viewport._size._y;
        return UIRect{ rect._left * scaleX, rect._top * scaleY, rect._right * scaleX, rect._bottom * scaleY };
    }

    void UIPreviewLogic::collectWidgetRows( const WidgetTree& tree, vector<UIPreviewWidgetRow>& outListRow )
    {
        outListRow.clear();
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( const Widget* pWidget : listWidget )
        {
            UIPreviewWidgetRow row{};
            row._widget = pWidget->getID();
            for ( const PanelWidget* pParent = pWidget->getParent(); pParent != nullptr; pParent = pParent->getParent() )
            {
                ++row._depth;
            }
            const TypeInfo* pType = pWidget->getTypeInfo();
            row._label            = pType != nullptr ? string( pType->_name.c_str() ) : string( "Widget" );
            if ( pWidget->getName().empty() == false )
                row._label += string( " #" ) + pWidget->getName().c_str();
            outListRow.push_back( row );
        }
    }

    WidgetID UIPreviewLogic::findWidgetAt( const WidgetTree& tree, const float2& point )
    {
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( size_t index = listWidget.size(); index > 0; --index )
        {
            const Widget* pWidget  = listWidget[index - 1];
            bool          bVisible = pWidget->isVisible();
            for ( const PanelWidget* pParent = pWidget->getParent(); pParent != nullptr && bVisible; pParent = pParent->getParent() )
            {
                bVisible = pParent->isVisible(); // 접힌 조상 아래의 기하는 지난 배치다
            }
            if ( bVisible == false )
                continue;
            const UIRect bounds = pWidget->getGeometry().computeScreenBounds();
            if ( bounds._left <= point._x && point._x < bounds._right && bounds._top <= point._y && point._y < bounds._bottom )
                return pWidget->getID();
        }
        return kInvalidWidgetID;
    }

    string UIPreviewLogic::makeTargetPath( const UIViewport& viewport )
    {
        return "rendertarget/editor_ui_preview_" + to_string( static_cast<uint32>( viewport._physicalSize._x ) ) + "x" +
               to_string( static_cast<uint32>( viewport._physicalSize._y ) );
    }

    bool UIPreviewLogic::isPseudoLanguage( string_view code )
    {
        return code == UIPreviewLogicInternal::kPseudoAccented || code == UIPreviewLogicInternal::kPseudoMirrored;
    }

    void UIPreviewLogic::makeLanguageChoices( const vector<string>& listAvailable, vector<string>& outListCode )
    {
        outListCode.clear();
        outListCode.push_back( string( kGameLanguage ) );
        vector<string> listReal;
        for ( const string& code : listAvailable )
        {
            if ( isPseudoLanguage( code ) == false && code.empty() == false )
                listReal.push_back( code );
        }
        std::sort( listReal.begin(), listReal.end() );
        listReal.erase( std::unique( listReal.begin(), listReal.end() ), listReal.end() );
        for ( const string& code : listReal )
        {
            outListCode.push_back( code );
        }
        outListCode.push_back( string( UIPreviewLogicInternal::kPseudoAccented ) );
        outListCode.push_back( string( UIPreviewLogicInternal::kPseudoMirrored ) );
    }

    uint32 UIPreviewLogic::findWidgetOrdinal( const WidgetTree& tree, WidgetID id )
    {
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( size_t index = 0; index < listWidget.size(); ++index )
        {
            if ( listWidget[index]->getID() == id )
                return static_cast<uint32>( index );
        }
        return kNoOrdinal;
    }

    WidgetID UIPreviewLogic::findWidgetByOrdinal( const WidgetTree& tree, uint32 ordinal )
    {
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        return ordinal < listWidget.size() ? listWidget[ordinal]->getID() : kInvalidWidgetID;
    }

    const PropertyInfo* UIPreviewLogic::findWidgetProperty( const Widget& widget, string_view name )
    {
        const TypeInfo* pType = widget.getTypeInfo();
        return pType != nullptr ? pType->findPropertyInHierarchy( hashed_string( string( name ).c_str() ) ) : nullptr;
    }

    string UIPreviewLogic::formatWidgetProperty( const Widget& widget, const PropertyInfo& prop )
    {
        return SerializerUtil::formatPropertyText( prop, &widget, SerializeContext::getDefault() );
    }

    bool UIPreviewLogic::applyWidgetProperty( Widget& widget, const PropertyInfo& prop, string_view text )
    {
        if ( SerializerUtil::applyPropertyText( prop, &widget, text, SerializeContext::getDefault() ) == false )
            return false;
        widget.onBoundPropertyChanged( prop );
        return true;
    }

    bool UIPreviewLogic::makeDocumentText( UISystem& ui, const UIScreen& screen, string_view documentPath, string& outText, string& outError )
    {
        const Widget* pRoot = screen.getTree().getRoot();
        if ( pRoot == nullptr )
        {
            outError = "the preview has no widget tree";
            return false;
        }
        const shared_ptr<const UIDocumentAsset> document = ui.getDocumentCache().findOrLoad( documentPath, outError );
        if ( document == nullptr )
            return false;
        outText = UIDocumentWriter::write( screen.getDesc(), document->_listStyleSheet, *pRoot, screen.getBindings(), screen.getAnimationPlayer().getAnimations() );
        return true;
    }

    string UIPreviewLogic::makeTimeLabel( float32 time, float32 duration )
    {
        fixed_string<constant::kMaxBuffer64> label;
        formatstring( label.data(), label.capacity(), "%.2f / %.2f s", static_cast<float64>( time ), static_cast<float64>( duration ) );
        return string( label.c_str() );
    }
} // namespace sw::editor
