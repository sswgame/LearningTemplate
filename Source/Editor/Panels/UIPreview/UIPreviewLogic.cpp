#include "pch.h"

#include "Editor/Panels/UIPreview/UIPreviewLogic.h"

#include "Core/Common/Defines.h"
#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
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

            /// 앵커 프리셋 표 한 줄입니다(`UIPreviewAnchorPreset` 순서).
            struct AnchorPresetRow
            {
                const utf8* _pMark;
                float32     _minX;
                float32     _minY;
                float32     _maxX;
                float32     _maxY;
            };
            static constexpr AnchorPresetRow kArrAnchorPreset[] = {
                {"topleft",           0.0f, 0.0f, 0.0f, 0.0f},
                {"topcenter",         0.5f, 0.0f, 0.5f, 0.0f},
                {"topright",          1.0f, 0.0f, 1.0f, 0.0f},
                {"centerleft",        0.0f, 0.5f, 0.0f, 0.5f},
                {"center",            0.5f, 0.5f, 0.5f, 0.5f},
                {"centerright",       1.0f, 0.5f, 1.0f, 0.5f},
                {"bottomleft",        0.0f, 1.0f, 0.0f, 1.0f},
                {"bottomcenter",      0.5f, 1.0f, 0.5f, 1.0f},
                {"bottomright",       1.0f, 1.0f, 1.0f, 1.0f},
                {"stretchhorizontal", 0.0f, 0.5f, 1.0f, 0.5f},
                {"stretchvertical",   0.5f, 0.0f, 0.5f, 1.0f},
                {"stretchall",        0.0f, 0.0f, 1.0f, 1.0f},
            };
            static_assert( sizeof( kArrAnchorPreset ) / sizeof( kArrAnchorPreset[0] ) == static_cast<size_t>( UIPreviewAnchorPreset::Count ), "anchor preset table size" );

            /** @brief 프리셋 @p preset 의 표 줄입니다. 범위 밖이면 첫 줄(좌상)입니다. */
            static const AnchorPresetRow& findAnchorPresetRow( UIPreviewAnchorPreset preset )
            {
                const uint32 index = static_cast<uint32>( preset );
                return kArrAnchorPreset[index < static_cast<uint32>( UIPreviewAnchorPreset::Count ) ? index : 0];
            }
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

    PanelWidget* UIPreviewLogic::findPanelByOrdinal( WidgetTree& tree, uint32 ordinal )
    {
        Widget* pWidget = tree.findWidgetByID( findWidgetByOrdinal( tree, ordinal ) );
        return pWidget != nullptr ? pWidget->asPanel() : nullptr;
    }

    void UIPreviewLogic::collectPaletteTypes( const TypeRegistry& registry, vector<const TypeInfo*>& outListType )
    {
        outListType.clear();
        for ( const TypeInfo* pType : registry.getDerivedTypes<Widget>() )
        {
            if ( pType != nullptr && pType->canConstruct() )
                outListType.push_back( pType );
        }
        std::sort( outListType.begin(), outListType.end(), []( const TypeInfo* pLeft, const TypeInfo* pRight )
        { return string_view{ pLeft->_name.c_str() } < string_view{ pRight->_name.c_str() }; } );
    }

    PanelWidget* UIPreviewLogic::findDropContainer( WidgetTree& tree, Widget* pTarget, uint32& outIndex )
    {
        PanelWidget* pContainer = pTarget != nullptr ? pTarget->asPanel() : nullptr;
        if ( pContainer != nullptr )
        {
            outIndex = pContainer->getChildCount(); // 패널에 놓으면 자식 끝에 붙인다
            return pContainer;
        }
        pContainer = pTarget != nullptr ? pTarget->getParent() : nullptr;
        if ( pContainer != nullptr )
        {
            const uint32 targetIndex = pContainer->findChildIndex( pTarget );
            outIndex                 = targetIndex == invalid_index::kUint32 ? pContainer->getChildCount() : targetIndex + 1; // 패널이 아닌 위젯에 놓으면 그 바로 뒤에 끼운다
            return pContainer;
        }
        Widget* pRoot = tree.getRoot();
        pContainer    = pRoot != nullptr ? pRoot->asPanel() : nullptr;
        outIndex      = pContainer != nullptr ? pContainer->getChildCount() : 0;
        return pContainer;
    }

    Widget* UIPreviewLogic::insertWidget( WidgetTree& tree, const TypeInfo& type, uint32 parentOrdinal, uint32 index )
    {
        PanelWidget* pParent = findPanelByOrdinal( tree, parentOrdinal );
        if ( pParent == nullptr )
            return nullptr;
        unique_ptr<Widget> widget = UIDocumentLoader::createWidget( type );
        if ( widget == nullptr )
            return nullptr;
        return pParent->insertChild( index, std::move( widget ) );
    }

    unique_ptr<Widget> UIPreviewLogic::detachWidget( WidgetTree& tree, uint32 parentOrdinal, uint32 index )
    {
        PanelWidget* pParent = findPanelByOrdinal( tree, parentOrdinal );
        if ( pParent == nullptr || index >= pParent->getChildCount() )
            return {};
        return pParent->removeChild( pParent->getChild( index ) );
    }

    Widget* UIPreviewLogic::attachWidget( WidgetTree& tree, unique_ptr<Widget> widget, uint32 parentOrdinal, uint32 index )
    {
        PanelWidget* pParent = findPanelByOrdinal( tree, parentOrdinal );
        if ( pParent == nullptr || widget == nullptr )
            return nullptr;
        return pParent->insertChild( index, std::move( widget ) );
    }

    bool UIPreviewLogic::canEditSlotByHandle( const Widget& widget )
    {
        return castTo<const CanvasPanel>( widget.getParent() ) != nullptr;
    }

    UIPreviewSlotFields UIPreviewLogic::findSlotFields( const Widget& widget )
    {
        UIPreviewSlotFields fields{};
        const auto*         pParent = widget.getParent();
        if ( pParent == nullptr )
            return fields;
        fields._bSizeOverride = true;
        if ( castTo<const CanvasPanel>( pParent ) != nullptr )
        {
            fields._bCanvas = true;
            return fields;
        }
        fields._bPaddingAlignment = true;
        fields._bBoxSize          = castTo<const BoxPanel>( pParent ) != nullptr;
        fields._bGrid             = castTo<const GridPanel>( pParent ) != nullptr;
        return fields;
    }

    bool UIPreviewLogic::applySlotDrag( WidgetLayoutSlot& inoutSlot, UIPreviewSlotHandle handle, const float2& deltaUI )
    {
        if ( deltaUI._x == 0.0f && deltaUI._y == 0.0f )
            return false;
        switch ( handle )
        {
            case UIPreviewSlotHandle::Move:
            {
                inoutSlot._offsetMin._x += deltaUI._x;
                inoutSlot._offsetMin._y += deltaUI._y;
                inoutSlot._offsetMax._x += deltaUI._x;
                inoutSlot._offsetMax._y += deltaUI._y;
                return true;
            }
            case UIPreviewSlotHandle::Left:
            {
                inoutSlot._offsetMin._x += deltaUI._x;
                return deltaUI._x != 0.0f;
            }
            case UIPreviewSlotHandle::Top:
            {
                inoutSlot._offsetMin._y += deltaUI._y;
                return deltaUI._y != 0.0f;
            }
            case UIPreviewSlotHandle::Right:
            {
                if ( inoutSlot._bAutoSize )
                    return false;
                inoutSlot._offsetMax._x += deltaUI._x;
                return deltaUI._x != 0.0f;
            }
            case UIPreviewSlotHandle::Bottom:
            {
                if ( inoutSlot._bAutoSize )
                    return false;
                inoutSlot._offsetMax._y += deltaUI._y;
                return deltaUI._y != 0.0f;
            }
        }
        return false;
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

    UIPreviewAnchorValues UIPreviewLogic::getAnchorPreset( UIPreviewAnchorPreset preset )
    {
        const auto&           row = UIPreviewLogicInternal::findAnchorPresetRow( preset );
        UIPreviewAnchorValues values{};
        values._anchorMin = float2{ row._minX, row._minY };
        values._anchorMax = float2{ row._maxX, row._maxY };
        return values;
    }

    const utf8* UIPreviewLogic::getAnchorPresetMark( UIPreviewAnchorPreset preset )
    {
        return UIPreviewLogicInternal::findAnchorPresetRow( preset )._pMark;
    }

    bool UIPreviewLogic::applyAnchorPreset( WidgetLayoutSlot& inoutSlot, UIPreviewAnchorPreset preset, const UIRect& localRect, const float2& parentSize )
    {
        // 사각형 = 앵커 × 부모 크기 + 오프셋(CanvasPanel::arrangeChildren) 이므로, 새 앵커에서 같은 사각형이 나오도록 오프셋을 거꾸로 구한다.
        const UIPreviewAnchorValues values = getAnchorPreset( preset );
        WidgetLayoutSlot            next   = inoutSlot;
        next._anchorMin                    = values._anchorMin;
        next._anchorMax                    = values._anchorMax;
        next._offsetMin._x                 = localRect._left - values._anchorMin._x * parentSize._x;
        next._offsetMin._y                 = localRect._top - values._anchorMin._y * parentSize._y;
        next._offsetMax._x                 = localRect._right - values._anchorMax._x * parentSize._x;
        next._offsetMax._y                 = localRect._bottom - values._anchorMax._y * parentSize._y;
        const bool bChanged = next._anchorMin._x != inoutSlot._anchorMin._x || next._anchorMin._y != inoutSlot._anchorMin._y || next._anchorMax._x != inoutSlot._anchorMax._x
                              || next._anchorMax._y != inoutSlot._anchorMax._y || next._offsetMin._x != inoutSlot._offsetMin._x || next._offsetMin._y != inoutSlot._offsetMin._y
                              || next._offsetMax._x != inoutSlot._offsetMax._x || next._offsetMax._y != inoutSlot._offsetMax._y;
        inoutSlot = next;
        return bChanged;
    }

    uint32 UIPreviewLogic::findBoxInsertIndex( const vector<float32>& listChildCenter, float32 point )
    {
        uint32 index{ 0 };
        for ( const float32 center : listChildCenter )
        {
            if ( center < point )
                ++index;
        }
        return index;
    }

    uint32 UIPreviewLogic::findReorderIndex( uint32 fromIndex, uint32 insertIndex )
    {
        return insertIndex > fromIndex ? insertIndex - 1 : insertIndex;
    }

    uint32 UIPreviewLogic::findGridTrack( const vector<float32>& listTrackStart, float32 trackEnd, float32 point )
    {
        const uint32 count = static_cast<uint32>( listTrackStart.size() );
        if ( count == 0 )
            return 0;
        if ( point >= trackEnd )
            return count;
        uint32 track{ 0 };
        for ( uint32 i = 0; i < count; ++i )
        {
            if ( listTrackStart[i] <= point )
                track = i;
        }
        return track;
    }

    void UIPreviewLogic::collectGridTracks( const PanelWidget& grid, bool bRows, vector<float32>& outListTrackStart, float32& outTrackEnd )
    {
        outListTrackStart.clear();
        outTrackEnd = 0.0f;
        vector<uint8> listSeen;
        for ( uint32 i = 0; i < grid.getChildCount(); ++i )
        {
            const Widget*           pChild = grid.getChild( i );
            const WidgetLayoutSlot& slot   = pChild->getLayoutSlot();
            const UIRect            bounds = pChild->getGeometry().computeScreenBounds();
            const uint32            track  = bRows ? slot._row : slot._column;
            const float32           start  = bRows ? bounds._top : bounds._left;
            const float32           end    = bRows ? bounds._bottom : bounds._right;
            while ( outListTrackStart.size() <= track )
            {
                outListTrackStart.push_back( 0.0f );
                listSeen.push_back( 0 );
            }
            if ( listSeen[track] == 0 || start < outListTrackStart[track] )
                outListTrackStart[track] = start;
            listSeen[track] = 1;
            if ( i == 0 || end > outTrackEnd )
                outTrackEnd = end;
        }
        // 자식이 없는 줄은 폭이 0 인 줄로 둔다(뒤 줄의 시작을 잇는다) — 그 칸으로는 끌어 놓을 수 없지만 번호는 밀리지 않는다. 마지막 줄에는 늘 자식이 있다.
        for ( uint32 i = static_cast<uint32>( outListTrackStart.size() ); i-- > 0; )
        {
            if ( listSeen[i] == 0 )
                outListTrackStart[i] = i + 1 < static_cast<uint32>( outListTrackStart.size() ) ? outListTrackStart[i + 1] : outTrackEnd;
        }
    }

    string UIPreviewLogic::makeTimeLabel( float32 time, float32 duration )
    {
        fixed_string<constant::kMaxBuffer64> label;
        formatstring( label.data(), label.capacity(), "%.2f / %.2f s", static_cast<float64>( time ), static_cast<float64>( duration ) );
        return string( label.c_str() );
    }
} // namespace sw::editor
