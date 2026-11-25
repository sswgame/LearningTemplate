#include "pch.h"

#include "GameFramework/Client/Marker/ObjectiveMarkerComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/UIEngineDocuments.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct ObjectiveMarkerComponentInternal
        {
            static constexpr int32       kNoMeters     = -1; ///< 아직 거리를 쓰지 않았다
            static constexpr const utf8* kArrowName    = "Arrow";
            static constexpr const utf8* kLabelName    = "Label";
            static constexpr const utf8* kDistanceName = "Distance";

            static void setFontSize( TextWidget& widget, float32 fontSize )
            {
                TextLayoutStyle style = widget.getTextStyle();
                style._fontSize       = fontSize;
                widget.setTextStyle( style );
            }
        };
    } // namespace

    ObjectiveMarkerComponent::ObjectiveMarkerComponent()
        : WidgetComponent{}
        , _label{}
        , _arrowColor{ 1.0f, 0.82f, 0.25f, 1.0f }
        , _arrowSize{ 6.0f, 20.0f }
        , _fontSize{ 16.0f }
        , _arrowWidget{ kInvalidWidgetID }
        , _distanceWidget{ kInvalidWidgetID }
        , _shownMeters{ ObjectiveMarkerComponentInternal::kNoMeters }
    {
        setClampToScreenEdge( true );
        setPivot( float2{ 0.5f, 1.0f } );
    }

    ObjectiveMarkerComponent::~ObjectiveMarkerComponent() = default;

    string_view ObjectiveMarkerComponent::getDefaultDocument( const UIEngineDocuments& documents ) const
    {
        return documents._objectiveMarker;
    }

    bool ObjectiveMarkerComponent::onDocumentCreated( Widget& content )
    {
        using Internal         = ObjectiveMarkerComponentInternal;
        BorderPanel* pArrow    = castTo<BorderPanel>( content.findDescendant( hashed_string( Internal::kArrowName ) ) );
        TextWidget*  pLabel    = castTo<TextWidget>( content.findDescendant( hashed_string( Internal::kLabelName ) ) );
        TextWidget*  pDistance = castTo<TextWidget>( content.findDescendant( hashed_string( Internal::kDistanceName ) ) );
        if ( pArrow == nullptr || pLabel == nullptr || pDistance == nullptr )
        {
            SW_LOG_ERROR_ID( "ObjectiveMarkerComponent.DocumentNeeds", "Objective marker document needs a BorderPanel '%#' and TextWidgets '%#' and '%#' - the marker is not shown", Internal::kArrowName, Internal::kLabelName,
                             Internal::kDistanceName );
            return false;
        }
        UIBrush brush = pArrow->getBackground();
        brush._color  = _arrowColor;
        pArrow->setBackground( brush );
        WidgetLayoutSlot arrowSlot = pArrow->getLayoutSlot();
        arrowSlot._widthOverride   = _arrowSize._x;
        arrowSlot._heightOverride  = _arrowSize._y;
        pArrow->setLayoutSlot( arrowSlot );
        pLabel->setText( _label );
        Internal::setFontSize( *pLabel, _fontSize );
        Internal::setFontSize( *pDistance, _fontSize );
        _arrowWidget    = pArrow->getID();
        _distanceWidget = pDistance->getID();
        _shownMeters    = Internal::kNoMeters;
        return true;
    }

    bool ObjectiveMarkerComponent::needsMarkerPlacement() const
    {
        return true;
    }

    void ObjectiveMarkerComponent::onMarkerPlaced( Widget& marker, const WidgetMarkerPlacement& placement )
    {
        const WidgetTree* pTree  = marker.getTree();
        Widget*           pArrow = pTree != nullptr ? pTree->findWidgetByID( _arrowWidget ) : nullptr;
        if ( pArrow == nullptr )
            return; // 코드가 다른 내용을 넘겼다
        // 방향 막대 — 가장자리에 붙었을 때만, 화면 가운데에서 목표 쪽으로 돈다.
        pArrow->setVisibility( placement._bClamped == SW_TRUE ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Hidden );
        WidgetRenderTransform transform = pArrow->getRenderTransform();
        transform._angle                = Radians::fromRadians( placement._edgeAngle );
        transform._pivot                = float2{ 0.5f, 0.5f };
        pArrow->setRenderTransform( transform );
        // 거리 — 1 m 단위가 바뀔 때만 글을 쓴다(글 배치는 비싸다).
        const int32 meters = static_cast<int32>( MathUtil::round( placement._distance ) );
        if ( meters == _shownMeters )
            return;
        _shownMeters = meters;
        if ( TextWidget* pDistance = castTo<TextWidget>( pTree->findWidgetByID( _distanceWidget ) ); pDistance != nullptr )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%# m", meters );
            pDistance->setText( string( textBuilder.view() ) );
        }
    }
} // namespace sw
