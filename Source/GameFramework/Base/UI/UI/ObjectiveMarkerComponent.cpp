#include "pch.h"

#include "GameFramework/Base/UI/UI/ObjectiveMarkerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct ObjectiveMarkerComponentInternal
        {
            static constexpr uint32  kArrowIndex    = 0;
            static constexpr uint32  kDistanceIndex = 2;
            static constexpr float32 kArrowWidth    = 6.0f;
            static constexpr float32 kArrowLength   = 20.0f;
            static constexpr float32 kFontSize      = 16.0f;

            static unique_ptr<TextWidget> makeText( string_view text, bool bLocalized )
            {
                unique_ptr<TextWidget> widget = make_unique<TextWidget>();
                TextLayoutStyle        style  = widget->getTextStyle();
                style._fontSize               = kFontSize;
                style._alignment              = TextAlignment::Center;
                widget->setTextStyle( style );
                widget->setLocalized( bLocalized );
                widget->setText( text );
                return widget;
            }
        };
    } // namespace

    ObjectiveMarkerComponent::ObjectiveMarkerComponent()
        : WidgetComponent{}
        , _label{}
        , _shownMeters{ -1 }
    {
        setClampToScreenEdge( true );
        setPivot( float2{ 0.5f, 1.0f } );
    }

    ObjectiveMarkerComponent::~ObjectiveMarkerComponent() = default;

    void ObjectiveMarkerComponent::onBeginPlay()
    {
        if ( getContent() == nullptr )
            setContent( buildContent() );
        WidgetComponent::onBeginPlay();
    }

    void ObjectiveMarkerComponent::onMarkerPlaced( Widget& marker, const WidgetMarkerPlacement& placement )
    {
        using Internal      = ObjectiveMarkerComponentInternal;
        PanelWidget* pPanel = castTo<PanelWidget>( &marker );
        if ( pPanel == nullptr || pPanel->getChildCount() <= Internal::kDistanceIndex )
            return;
        // 방향 막대 — 가장자리에 붙었을 때만, 화면 가운데에서 목표 쪽으로 돈다.
        Widget& arrow = *pPanel->getChild( Internal::kArrowIndex );
        arrow.setVisibility( placement._bClamped == SW_TRUE ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Hidden );
        WidgetRenderTransform transform = arrow.getRenderTransform();
        transform._angle                = placement._edgeAngle;
        transform._pivot                = float2{ 0.5f, 0.5f };
        arrow.setRenderTransform( transform );
        // 거리 — 1 m 단위가 바뀔 때만 글을 쓴다(글 배치는 비싸다).
        const int32 meters = static_cast<int32>( MathUtil::round( placement._distance ) );
        if ( meters == _shownMeters )
            return;
        _shownMeters = meters;
        if ( TextWidget* pDistance = castTo<TextWidget>( pPanel->getChild( Internal::kDistanceIndex ) ); pDistance != nullptr )
            pDistance->setText( to_string( meters ) + " m" );
    }

    unique_ptr<Widget> ObjectiveMarkerComponent::buildContent() const
    {
        using Internal              = ObjectiveMarkerComponentInternal;
        unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
        column->setOrientation( UiOrientation::Vertical );
        column->setSpacing( 2.0f );
        unique_ptr<BorderPanel> arrow = make_unique<BorderPanel>();
        arrow->setBackground( UiBrush::makeSolid( float4{ 1.0f, 0.82f, 0.25f, 1.0f }, 2.0f ) );
        WidgetLayoutSlot arrowSlot     = arrow->getLayoutSlot();
        arrowSlot._widthOverride       = Internal::kArrowWidth;
        arrowSlot._heightOverride      = Internal::kArrowLength;
        arrowSlot._horizontalAlignment = UiAlignment::Center;
        arrow->setLayoutSlot( arrowSlot );
        arrow->setVisibility( WidgetVisibility::Hidden );
        (void)column->addChild( std::move( arrow ) );
        (void)column->addChild( Internal::makeText( _label, true ) );
        (void)column->addChild( Internal::makeText( "", false ) );
        return column;
    }
} // namespace sw
