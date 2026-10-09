#include "pch.h"

#include "GameFramework/Base/UI/Marker/HealthBarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "GameFramework/Base/Actor/Combat/Health/HealthSourceComponent.h"

namespace sw
{
    namespace
    {
        struct HealthBarComponentInternal
        {
            /** @brief @p current 를 @p target 쪽으로 옮깁니다. 속도가 0 이하면 바로 갑니다(지수 접근 — 프레임 속도에 덜 민감합니다). */
            static float32 approach( float32 current, float32 target, float32 speed, float32 deltaTime )
            {
                if ( speed <= 0.0f )
                    return target;
                return MathUtil::lerp( current, target, MathUtil::saturate( speed * deltaTime ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HealthBarComponent::HealthBarComponent()
        : _hpRatio{ 0.0f }
        , _remainRatio{ 0.0f }
        , _targetRatio{ 0.0f }
        , _lerpSpeed{ 5.0f }
        , _fillColor{ 0.25f, 0.85f, 0.3f, 1.0f }
        , _trailColor{ 0.95f, 0.8f, 0.25f, 1.0f }
        , _backgroundColor{ 0.08f, 0.08f, 0.1f, 0.8f }
        , _bVisible{ false }
        , _bShowWhenHurt{ false }
        , _bHideWhenDead{ false }
        , _bWarnedNoWidgetComponent{ false }
    {
    }

    void HealthBarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );

        // 같은 오브젝트의 체력 원천이 있으면 그 비율에서 시작한다 — 원천보다 먼저 시작하든, 맞은 뒤에 붙든 같다. 원천이 없으면 저장된 칸이다.
        GameObject*                  pOwner  = getOwner();
        const HealthSourceComponent* pSource = ( pOwner != nullptr ) ? pOwner->getComponent<HealthSourceComponent>() : nullptr;
        if ( pSource != nullptr && pSource->getHealthReading()._bHasHealth == SW_TRUE )
            _hpRatio = pSource->getHealthRatio();
        _hpRatio     = MathUtil::saturate( _hpRatio );
        _remainRatio = _hpRatio;
        _targetRatio = _hpRatio;

        // 막대 위젯을 화면 마커에 넣는다(마커가 아직 UI 시스템에 묶이지 않았으면 묶일 때 붙는다). 끝날 때는 마커가 위젯을 지운다 — 다시 시작하면 다시 짓는다.
        WidgetComponent* pWidget = findWidgetComponent();
        if ( pWidget != nullptr && pWidget->getContent() == nullptr )
            pWidget->setContent( createBarWidget() );
        else if ( pWidget == nullptr && pOwner != nullptr && _bWarnedNoWidgetComponent == false )
        {
            _bWarnedNoWidgetComponent = true;
            SW_LOG_WARNING( "HP bar on '%#' has no WidgetComponent on its object - the bar is not drawn", pOwner->getName().c_str() );
        }
        refreshWidgets();
    }

    void HealthBarComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        // 채움: 줄 때는 맞은 순간 줄고, 늘 때는 차오른다. 흔적: 채움보다 길면 채움까지 줄고, 짧아질 일은 없다(채움을 따라간다).
        if ( _targetRatio < _hpRatio )
            _hpRatio = _targetRatio;
        else
            _hpRatio = HealthBarComponentInternal::approach( _hpRatio, _targetRatio, _lerpSpeed, deltaTime );
        if ( _remainRatio < _hpRatio )
            _remainRatio = _hpRatio;
        else
            _remainRatio = HealthBarComponentInternal::approach( _remainRatio, _hpRatio, _lerpSpeed, deltaTime );

        scheduleRefresh();
    }

    void HealthBarComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( hasBegunPlay() )
            refreshWidgets();
    }

    void HealthBarComponent::onOwnerActiveInHierarchyChanged()
    {
        if ( hasBegunPlay() )
            refreshWidgets();
    }

    void HealthBarComponent::onHealthChanged( const HealthChangedEvent& event )
    {
        switch ( event._kind )
        {
            case HealthChangeKind::Reset:
            {
                resetRatio( event._ratio );
                break;
            }
            case HealthChangeKind::Changed:
            {
                if ( _bShowWhenHurt && event._ratio < _targetRatio )
                    setVisible( true );
                setTargetRatio( event._ratio );
                break;
            }
            case HealthChangeKind::Died:
            {
                // 쓰러짐도 줄어든 것이다 — 숨기지 않는 바는 한 방에 쓰러진 적에서도 보인다.
                setTargetRatio( event._ratio );
                if ( _bHideWhenDead )
                    setVisible( false );
                else if ( _bShowWhenHurt )
                    setVisible( true );
                break;
            }
        }
    }

    void HealthBarComponent::setTargetRatio( float32 ratio )
    {
        _targetRatio = MathUtil::saturate( ratio );
    }

    void HealthBarComponent::resetRatio( float32 ratio )
    {
        _targetRatio = MathUtil::saturate( ratio );
        _hpRatio     = _targetRatio;
        _remainRatio = _targetRatio;
        if ( hasBegunPlay() )
            scheduleRefresh();
    }

    void HealthBarComponent::setVisible( bool bVisible )
    {
        _bVisible = bVisible;
        if ( hasBegunPlay() )
            scheduleRefresh();
    }

    WidgetComponent* HealthBarComponent::findWidgetComponent() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<WidgetComponent>() : nullptr;
    }

    unique_ptr<Widget> HealthBarComponent::createBarWidget()
    {
        // 겹침 패널 — 자식이 패널 전체를 채운다. 아래가 흔적(바탕 포함), 위가 채움(바탕 없음). 마커는 클릭을 막지 않는다.
        unique_ptr<OverlayPanel> panel = sw::make_unique<OverlayPanel>();
        panel->setVisibility( WidgetVisibility::HitTestInvisible );
        for ( uint32 index = 0; index < 2; ++index )
        {
            unique_ptr<ProgressBarWidget> bar = sw::make_unique<ProgressBarWidget>();
            bar->setVisibility( WidgetVisibility::HitTestInvisible );
            (void)panel->addChild( std::move( bar ) );
        }
        return panel;
    }

    ProgressBarWidget* HealthBarComponent::findBar( uint32 index ) const
    {
        const WidgetComponent* pWidget = findWidgetComponent();
        const PanelWidget*     pPanel  = pWidget != nullptr ? castTo<PanelWidget>( pWidget->getContent() ) : nullptr;
        if ( pPanel == nullptr || index >= pPanel->getChildCount() )
            return nullptr;
        return castTo<ProgressBarWidget>( pPanel->getChild( index ) );
    }

    void HealthBarComponent::scheduleRefresh()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 핸들로 다시 찾는다 — 그 사이 지워졌으면(구조 변경 큐가 먼저 돈다) 아무것도 하지 않는다.
        const ComponentHandle handle = getHandle();
        pManager->executeOrDeferPostTick( [pManager, handle]()
        {
            Component* pComponent = pManager->resolveComponent( handle );
            if ( pComponent != nullptr )
                static_cast<HealthBarComponent*>( pComponent )->refreshWidgets();
        } );
    }

    void HealthBarComponent::refreshWidgets()
    {
        WidgetComponent* pWidget = findWidgetComponent();
        if ( pWidget == nullptr )
            return;
        pWidget->setHidden( ( _bVisible && isActive() ) == false );
        ProgressBarWidget* pTrail = findBar( kTrailBarIndex );
        ProgressBarWidget* pFill  = findBar( kFillBarIndex );
        if ( pTrail == nullptr || pFill == nullptr )
            return;
        const float32 fillEnd = MathUtil::saturate( _hpRatio );
        pTrail->setPercent( MathUtil::max( fillEnd, MathUtil::saturate( _remainRatio ) ) );
        pTrail->setFillColor( _trailColor );
        pTrail->setBackgroundColor( _backgroundColor );
        pFill->setPercent( fillEnd );
        pFill->setFillColor( _fillColor );
        pFill->setBackgroundColor( float4{} );
    }
} // namespace sw
