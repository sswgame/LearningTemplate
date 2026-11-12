#include "pch.h"

#include "Editor/Panels/UIPreview/Model/UIPreviewLogic.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/Document/UIDocumentWriter.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/UIResolutionPresetTable.h"
#include "Engine/UI/Layout/UIScale.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw::editor
{
    namespace
    {
        struct UIPreviewLogicInternal
        {
            /// 의사 문화권 코드(정규 표기)입니다. 엔진 문화권 표(`LocalizationManager::normalizeLanguageCode`)가 쓰는 이름과 같다.
            static constexpr string_view kPseudoAccented = "qps_ploc";
            static constexpr string_view kPseudoMirrored = "qps_plocm";

            /// 앵커 프리셋 테이블 한 줄입니다(`UIPreviewAnchorPreset` 순서).
            struct AnchorPresetRow
            {
                const utf8* _pMark;
                float32     _minX;
                float32     _minY;
                float32     _maxX;
                float32     _maxY;
            };
            static constexpr AnchorPresetRow kArrAnchorPreset[] = {
                {          "topleft", 0.0f, 0.0f, 0.0f, 0.0f},
                {        "topcenter", 0.5f, 0.0f, 0.5f, 0.0f},
                {         "topright", 1.0f, 0.0f, 1.0f, 0.0f},
                {       "centerleft", 0.0f, 0.5f, 0.0f, 0.5f},
                {           "center", 0.5f, 0.5f, 0.5f, 0.5f},
                {      "centerright", 1.0f, 0.5f, 1.0f, 0.5f},
                {       "bottomleft", 0.0f, 1.0f, 0.0f, 1.0f},
                {     "bottomcenter", 0.5f, 1.0f, 0.5f, 1.0f},
                {      "bottomright", 1.0f, 1.0f, 1.0f, 1.0f},
                {"stretchhorizontal", 0.0f, 0.5f, 1.0f, 0.5f},
                {  "stretchvertical", 0.5f, 0.0f, 0.5f, 1.0f},
                {       "stretchall", 0.0f, 0.0f, 1.0f, 1.0f},
            };
            static_assert( sizeof( kArrAnchorPreset ) / sizeof( kArrAnchorPreset[0] ) == static_cast<size_t>( UIPreviewAnchorPreset::Count ), "anchor preset table size" );

            /** @brief 프리셋 @p preset 의 테이블 줄입니다. 범위 밖이면 첫 줄(좌상)입니다. */
            static const AnchorPresetRow& findAnchorPresetRow( UIPreviewAnchorPreset preset )
            {
                const uint32 index = static_cast<uint32>( preset );
                return kArrAnchorPreset[index < static_cast<uint32>( UIPreviewAnchorPreset::Count ) ? index : 0];
            }
        };
    } // namespace

    UIPreviewPointerTracker::UIPreviewPointerTracker()
        : _lastPosition{}
        , _pressedMask{ 0 }
        , _bInside{ SW_FALSE }
    {
    }

    void UIPreviewPointerTracker::reset()
    {
        _pressedMask = 0;
        _bInside     = SW_FALSE;
    }

    UIPreviewPointerPlan UIPreviewPointerTracker::step( const UIPreviewPointerInput& input )
    {
        static constexpr MouseButton kArrButton[UIPreviewPointerInput::kButtonCount] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };

        UIPreviewPointerPlan plan{};
        const bool           bWasCaptured = _pressedMask != 0;
        UIPointerEvent       event{};
        event._position = input._position;

        if ( input._bHovered || bWasCaptured )
        {
            const float2 delta{ input._position._x - _lastPosition._x, input._position._y - _lastPosition._y };
            const bool   bEntered = input._bHovered && _bInside == false;
            if ( bEntered || delta._x != 0.0f || delta._y != 0.0f )
            {
                UIPointerEvent move = event;
                move._kind          = UIPointerEventKind::Move;
                move._delta         = delta;
                plan._listEvent.push_back( move );
                plan._bMoved  = true;
                _lastPosition = input._position;
            }
        }
        if ( input._bHovered )
        {
            _bInside = SW_TRUE;
            if ( input._wheel != 0.0f )
            {
                UIPointerEvent wheel = event;
                wheel._kind          = UIPointerEventKind::Wheel;
                wheel._wheel         = input._wheel;
                plan._listEvent.push_back( wheel );
                plan._bMutates = true;
            }
        }
        else if ( _bInside )
        {
            _bInside = SW_FALSE;
            if ( bWasCaptured == false )
                plan._bClearHover = true;
        }

        for ( uint32 buttonIndex = 0; buttonIndex < UIPreviewPointerInput::kButtonCount; ++buttonIndex )
        {
            const uint8 buttonBit = static_cast<uint8>( 1u << buttonIndex );
            event._button         = kArrButton[buttonIndex];
            if ( input._bHovered && input._arrPressed[buttonIndex] )
            {
                event._kind = UIPointerEventKind::Down;
                plan._listEvent.push_back( event );
                _pressedMask   = static_cast<uint8>( _pressedMask | buttonBit );
                plan._bMutates = true;
            }
            if ( input._arrReleased[buttonIndex] && ( _pressedMask & buttonBit ) != 0 )
            {
                event._kind = UIPointerEventKind::Up;
                plan._listEvent.push_back( event );
                _pressedMask   = static_cast<uint8>( _pressedMask & ~buttonBit );
                plan._bMutates = true;
            }
        }
        if ( bWasCaptured && _pressedMask == 0 && input._bHovered == false )
            plan._bClearHover = true; // 밖에서 놓았다 — 잡은 동안 남겨 둔 호버를 비운다
        return plan;
    }

    UIViewport UIPreviewLogic::makeViewport( const UIScaleSettings& settings, const UIResolutionPreset& resolution, float32 userScale, float32 safeZoneRatio )
    {
        const float2 physicalSize{ static_cast<float32>( resolution._width ), static_cast<float32>( resolution._height ) };
        // 창 배율은 견본 기기의 것이다(데스크톱 1 · 휴대 기기 density) — 배율 설정이 창 배율을 곱할 때만 쓰인다. 안전 여백도 견본 기기의 노치 · 시스템 바다.
        return UIScaleSettings::makeViewport( settings, physicalSize, userScale, resolution._contentScale, resolution._safeInsets, safeZoneRatio );
    }

    UIResolutionPreset UIPreviewLogic::rotatePreset( const UIResolutionPreset& resolution )
    {
        UIResolutionPreset rotated = resolution;
        rotated._width             = resolution._height;
        rotated._height            = resolution._width;
        // 시계 방향 90 도: 위 → 오른쪽, 오른쪽 → 아래, 아래 → 왼쪽, 왼쪽 → 위(float4 는 왼쪽 · 위 · 오른쪽 · 아래).
        const float4& insets = resolution._safeInsets;
        rotated._safeInsets  = float4{ insets._w, insets._x, insets._y, insets._z };
        return rotated;
    }

    float2 UIPreviewLogic::fitImage( const float2& imageSize, const float2& available )
    {
        const bool bEmptyFitInputs = imageSize._x <= 0.0f || imageSize._y <= 0.0f || available._x <= 0.0f || available._y <= 0.0f;
        if ( bEmptyFitInputs )
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

    void UIPreviewLogic::filterWidgetRows( const vector<UIPreviewWidgetRow>& listRow, const EditorListFilter& filter, vector<uint32>& outListOrdinal )
    {
        // 아래에서 위로 걷는다 — 지금 남긴 줄보다 얕은 첫 줄이 곧 그 부모다.
        vector<uint8>           listKeep( listRow.size(), uint8{ 0 } );
        static constexpr uint32 kNoDepth      = 0xFFFFFFFFu;
        uint32                  ancestorDepth = kNoDepth;
        for ( size_t index = listRow.size(); index > 0; --index )
        {
            const UIPreviewWidgetRow& row       = listRow[index - 1];
            const bool                bAncestor = ancestorDepth != kNoDepth && row._depth < ancestorDepth;
            if ( bAncestor || filter.matches( row._label ) )
            {
                listKeep[index - 1] = 1;
                ancestorDepth       = row._depth;
            }
        }
        outListOrdinal.clear();
        for ( uint32 ordinal = 0; ordinal < static_cast<uint32>( listRow.size() ); ++ordinal )
        {
            if ( listKeep[ordinal] != 0 )
                outListOrdinal.push_back( ordinal );
        }
    }

    bool UIPreviewLogic::isVisibleInTree( const Widget& widget )
    {
        bool bVisible = widget.isVisible();
        for ( const PanelWidget* pParent = widget.getParent(); pParent != nullptr && bVisible; pParent = pParent->getParent() )
        {
            bVisible = pParent->isVisible();
        }
        return bVisible;
    }

    void UIPreviewLogic::collectVisibleBounds( const WidgetTree& tree, vector<UIRect>& outListBounds )
    {
        outListBounds.clear();
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( const Widget* pWidget : listWidget )
        {
            if ( isVisibleInTree( *pWidget ) )
                outListBounds.push_back( pWidget->getGeometry().computeScreenBounds() );
        }
    }

    bool UIPreviewLogic::findSafeZoneRect( const UIViewport& viewport, UIRect& outRect )
    {
        const float4& insets = viewport._safeInsets; // 왼쪽, 위, 오른쪽, 아래
        if ( insets._x <= 0.0f && insets._y <= 0.0f && insets._z <= 0.0f && insets._w <= 0.0f )
            return false;
        outRect = UIRect{ insets._x, insets._y, viewport._size._x - insets._z, viewport._size._y - insets._w };
        return true;
    }

    WidgetID UIPreviewLogic::findWidgetAt( const WidgetTree& tree, const float2& point )
    {
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( size_t index = listWidget.size(); index > 0; --index )
        {
            const Widget* pWidget = listWidget[index - 1];
            if ( isVisibleInTree( *pWidget ) == false )
                continue;
            const UIRect bounds             = pWidget->getGeometry().computeScreenBounds();
            const bool   bPointInsideBounds = bounds._left <= point._x && point._x < bounds._right && bounds._top <= point._y && point._y < bounds._bottom;
            if ( bPointInsideBounds )
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
        VectorUtil::sortUnique( listReal );
        for ( const string& code : listReal )
        {
            outListCode.push_back( code );
        }
        outListCode.push_back( string( UIPreviewLogicInternal::kPseudoAccented ) );
        outListCode.push_back( string( UIPreviewLogicInternal::kPseudoMirrored ) );
    }

    optional<uint32> UIPreviewLogic::findWidgetOrdinal( const WidgetTree& tree, WidgetID id )
    {
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( size_t index = 0; index < listWidget.size(); ++index )
        {
            if ( listWidget[index]->getID() == id )
                return static_cast<uint32>( index );
        }
        return nullopt;
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
        return SerializerUtil::formatPropertyText( prop, ObjectAddress{ &widget }, SerializeContext::getDefault( SerializePurpose::Authoring ) );
    }

    bool UIPreviewLogic::applyWidgetProperty( Widget& widget, const PropertyInfo& prop, string_view text )
    {
        if ( SerializerUtil::applyPropertyText( prop, MutableObjectAddress{ &widget }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
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
        VectorUtil::sort( outListType, []( const TypeInfo* pLeft, const TypeInfo* pRight )
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
            outIndex = pContainer->findChildIndex( pTarget ).value() + 1; // 패널이 아닌 위젯에 놓으면 그 바로 뒤에 끼운다(부모의 자식이라 늘 있다)
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
        return pParent->removeChild( pParent->getChild( index ) ).release(); // 되돌리기 기록이 미리보기 트리보다 오래 맡는다(편집은 이벤트 전달 밖이다)
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

    UIPreviewSlotParams UIPreviewLogic::findSlotProperties( const Widget& widget )
    {
        UIPreviewSlotParams params{};
        const auto*         pParent = widget.getParent();
        if ( pParent == nullptr )
            return params;
        params._bSizeOverride = true;
        if ( castTo<const CanvasPanel>( pParent ) != nullptr )
        {
            params._bCanvas = true;
            return params;
        }
        params._bPaddingAlignment = true;
        params._bBoxSize          = castTo<const BoxPanel>( pParent ) != nullptr;
        params._bGrid             = castTo<const GridPanel>( pParent ) != nullptr;
        return params;
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

    float2 UIPreviewLogic::getSlotHandleCenter( UIPreviewSlotHandle handle, const UIRect& box )
    {
        const float2 center = box.getCenter();
        switch ( handle )
        {
            case UIPreviewSlotHandle::Move:
                return center;
            case UIPreviewSlotHandle::Left:
                return float2{ box._left, center._y };
            case UIPreviewSlotHandle::Top:
                return float2{ center._x, box._top };
            case UIPreviewSlotHandle::Right:
                return float2{ box._right, center._y };
            case UIPreviewSlotHandle::Bottom:
                return float2{ center._x, box._bottom };
        }
        return center;
    }

    UIPreviewAnchorPreset UIPreviewLogic::findAnchorPreset( const WidgetLayoutSlot& slot )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( UIPreviewAnchorPreset::Count ); ++index )
        {
            const UIPreviewAnchorPreset preset = static_cast<UIPreviewAnchorPreset>( index );
            const UIPreviewAnchorValues values = getAnchorPreset( preset );
            const bool                  bSame  = values._anchorMin._x == slot._anchorMin._x && values._anchorMin._y == slot._anchorMin._y && values._anchorMax._x == slot._anchorMax._x && values._anchorMax._y == slot._anchorMax._y;
            if ( bSame )
                return preset;
        }
        return UIPreviewAnchorPreset::Count;
    }

    UIRect UIPreviewLogic::makeParentLocalRect( const Widget& widget )
    {
        const WidgetGeometry& geometry       = widget.getGeometry();
        const WidgetGeometry& parentGeometry = widget.getParent()->getGeometry();
        return UIRect::makeFromPositionSize( geometry._position._x - parentGeometry._position._x, geometry._position._y - parentGeometry._position._y, geometry._size._x, geometry._size._y );
    }

    bool UIPreviewLogic::canEditLayoutByHandle( const Widget& widget )
    {
        const PanelWidget* pParent = widget.getParent();
        return castTo<const BoxPanel>( pParent ) != nullptr || castTo<const GridPanel>( pParent ) != nullptr;
    }

    bool UIPreviewLogic::findBoxReorder( const BoxPanel& box, const Widget& child, const float2& point, uint32& outFromIndex, uint32& outToIndex )
    {
        const optional<uint32> fromIndex = box.findChildIndex( &child );
        if ( fromIndex.has_value() == false )
            return false;
        outFromIndex              = *fromIndex;
        const bool      bVertical = box.getOrientation() == UIOrientation::Vertical;
        vector<float32> listCenter;
        for ( uint32 childIndex = 0; childIndex < box.getChildCount(); ++childIndex )
        {
            const float2 childCenter = box.getChild( childIndex )->getGeometry().computeScreenBounds().getCenter();
            listCenter.push_back( bVertical ? childCenter._y : childCenter._x );
        }
        outToIndex = findReorderIndex( outFromIndex, findBoxInsertIndex( listCenter, bVertical ? point._y : point._x ) );
        return outToIndex != outFromIndex;
    }

    bool UIPreviewLogic::applyGridDrop( const GridPanel& grid, WidgetLayoutSlot& inoutSlot, const float2& point )
    {
        uint32 gridRow{ 0 };
        uint32 gridColumn{ 0 };
        if ( findGridCell( grid, point, gridRow, gridColumn ) == false )
            return false;
        const uint16 row    = static_cast<uint16>( MathUtil::min( gridRow, 0xFFFFu ) );
        const uint16 column = static_cast<uint16>( MathUtil::min( gridColumn, 0xFFFFu ) );
        if ( inoutSlot._row == row && inoutSlot._column == column )
            return false;
        inoutSlot._row    = row;
        inoutSlot._column = column;
        return true;
    }

    bool UIPreviewLogic::findSiblingMove( const Widget& widget, int32 delta, uint32& outFromIndex, uint32& outToIndex )
    {
        const PanelWidget* pParent = widget.getParent();
        if ( pParent == nullptr || delta == 0 )
            return false;
        outFromIndex       = pParent->findChildIndex( &widget ).value(); // 부모의 자식이라 늘 있다
        const int64 target = static_cast<int64>( outFromIndex ) + delta;
        if ( target < 0 || target >= static_cast<int64>( pParent->getChildCount() ) )
            return false;
        outToIndex = static_cast<uint32>( target );
        return true;
    }

    expected<string> UIPreviewLogic::makeDocumentText( UIDocumentCache& documents, const UIScreen& screen, string_view documentPath )
    {
        const Widget* pRoot = screen.getTree().getRoot();
        if ( pRoot == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "the preview has no widget tree" } );
        const expected<shared_ptr<const UIDocumentAsset>> document = documents.findOrLoad( documentPath, screen.getResourceTagOverride() );
        if ( document.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "the document could not be loaded", document.error().message() }.appendDetail( document.error().detail() ) );
        return UIDocumentWriter::write( screen.getDesc(), ( *document )->_listStyleSheet, *pRoot, screen.getBindings(), screen.getAnimationPlayer().getAnimations() );
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
        // 슬롯 사각형 = 앵커 × 부모 크기 + 오프셋(CanvasPanel::arrangeChildren)이고, 위젯은 그 안쪽 여백(`_padding`)만큼 들어간 곳에 놓인다(`UILayoutPass::applySlot`).
        // 그래서 위젯의 지금 사각형(@p localRect)에 여백을 도로 더한 것이 슬롯 사각형이다 — 여백을 빼먹으면 프리셋을 고른 직후 위젯이 여백만큼 밀리고 줄어든다.
        // 새 앵커에서 같은 슬롯 사각형이 나오도록 오프셋을 거꾸로 구한다.
        const UIPreviewAnchorValues values = getAnchorPreset( preset );
        const float4&               pad    = inoutSlot._padding; // 왼쪽, 위, 오른쪽, 아래
        const float32               left   = localRect._left - pad._x;
        const float32               top    = localRect._top - pad._y;
        const float32               right  = localRect._right + pad._z;
        const float32               bottom = localRect._bottom + pad._w;
        WidgetLayoutSlot            next   = inoutSlot;
        next._anchorMin                    = values._anchorMin;
        next._anchorMax                    = values._anchorMax;
        next._offsetMin._x                 = left - values._anchorMin._x * parentSize._x;
        next._offsetMin._y                 = top - values._anchorMin._y * parentSize._y;
        next._offsetMax._x                 = right - values._anchorMax._x * parentSize._x;
        next._offsetMax._y                 = bottom - values._anchorMax._y * parentSize._y;
        const bool bChanged                = next._anchorMin._x != inoutSlot._anchorMin._x || next._anchorMin._y != inoutSlot._anchorMin._y || next._anchorMax._x != inoutSlot._anchorMax._x || next._anchorMax._y != inoutSlot._anchorMax._y || next._offsetMin._x != inoutSlot._offsetMin._x || next._offsetMin._y != inoutSlot._offsetMin._y || next._offsetMax._x != inoutSlot._offsetMax._x || next._offsetMax._y != inoutSlot._offsetMax._y;
        inoutSlot                          = next;
        return bChanged;
    }

    uint32 UIPreviewLogic::findBoxInsertIndex( const vector<float32>& listChildCenter, float32 point )
    {
        // 방향은 배치 결과에서 읽는다 — 오른쪽에서 왼쪽(RTL 거울)이나 자식 크기보다 큰 음수 간격이면 번호가 커질수록 가운데가 작아진다.
        const bool bDescending = listChildCenter.size() >= 2 && listChildCenter.back() < listChildCenter.front();
        uint32     index{ 0 };
        for ( const float32 center : listChildCenter )
        {
            if ( bDescending ? center > point : center < point )
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
        for ( uint32 trackIndex = 0; trackIndex < count; ++trackIndex )
        {
            if ( listTrackStart[trackIndex] <= point )
                track = trackIndex;
        }
        return track;
    }

    void UIPreviewLogic::makeGridTrackStarts( const vector<float32>& listTrackSize, float32 spacing, vector<float32>& outListTrackStart, float32& outTrackEnd )
    {
        outListTrackStart.clear();
        outTrackEnd = 0.0f;
        for ( uint32 trackIndex = 0; trackIndex < static_cast<uint32>( listTrackSize.size() ); ++trackIndex )
        {
            const float32 start = trackIndex == 0 ? 0.0f : outTrackEnd + spacing;
            outListTrackStart.push_back( start );
            outTrackEnd = start + listTrackSize[trackIndex];
        }
    }

    bool UIPreviewLogic::findGridCell( const GridPanel& grid, const float2& screenPoint, uint32& outRow, uint32& outColumn )
    {
        float2 local{};
        if ( grid.getGeometry().inverseTransformPoint( screenPoint, local ) == false )
            return false;
        // 격자는 RTL 이면 늘 거울로 놓는다(`shouldMirrorChildrenInRightToLeft` 를 바꾸지 않는다) — 배치는 거울 전 좌표로 줄을 정한다(`PanelWidget::arrangeChild`).
        if ( grid.isRightToLeft() )
            local._x = grid.getGeometry()._size._x - local._x;
        vector<float32> listStart;
        float32         trackEnd{ 0.0f };
        makeGridTrackStarts( grid.getArrangedRowSizes(), grid.getCellSpacing()._y, listStart, trackEnd );
        outRow = findGridTrack( listStart, trackEnd, local._y );
        makeGridTrackStarts( grid.getArrangedColumnSizes(), grid.getCellSpacing()._x, listStart, trackEnd );
        outColumn = findGridTrack( listStart, trackEnd, local._x );
        return true;
    }

    string UIPreviewLogic::makeTimeLabel( float32 time, float32 duration )
    {
        fixed_string<constant::buffer::kMax64> label;
        formatstring( label.data(), label.capacity(), "%.2f / %.2f s", static_cast<float64>( time ), static_cast<float64>( duration ) );
        return string( label.c_str() );
    }
} // namespace sw::editor
