#include "pch.h"

#include "GameFramework/Base/UI/DamageNumberComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "GameFramework/Base/Utility/LifeSpanUtil.h"

namespace sw
{
    DamageNumberComponent::DamageNumberComponent()
        : _damageValue{ 0 }
        , _lifeTime{ 0.0f }
        , _currentLife{ 0.0f }
        , _floatSpeed{ 0.0f }
        , _alpha{ 0.0f }
        , _color{ 1.0f, 0.85f, 0.25f, 1.0f }
        , _bWarnedNoWidgetComponent{ false }
    {
    }

    void DamageNumberComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );

        // 흐른 수명은 처음으로 되돌리지 않는다 — 떠 있는 동안 상태를 다시 읽은 숫자(플레이 중 되돌리기 · 핫 리로드)가 수명을 다시 시작하지 않게.
        // 새로 만든 숫자는 0 이다. 알파는 흐른 수명에서 다시 구한다.
        _currentLife = MathUtil::max( _currentLife, 0.0f );
        _alpha       = computeAlpha();

        // 글 위젯을 화면 마커에 넣는다. 끝날 때는 마커가 위젯을 지운다 — 다시 시작하면 다시 짓는다.
        WidgetComponent* pWidget = findWidgetComponent();
        if ( pWidget != nullptr && pWidget->getContent() == nullptr )
        {
            unique_ptr<TextWidget> text = sw::make_unique<TextWidget>();
            text->setStyleClass( kStyleClass );
            text->setVisibility( WidgetVisibility::HitTestInvisible );
            pWidget->setContent( std::move( text ) );
        }
        else if ( pWidget == nullptr && getOwner() != nullptr && _bWarnedNoWidgetComponent == false )
        {
            _bWarnedNoWidgetComponent = true;
            SW_LOG_WARNING( "Damage number on '%#' has no WidgetComponent on its object - the number is not drawn", getOwner()->getName().c_str() );
        }
        refreshWidget();
    }

    void DamageNumberComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        const bool bExpired = LifeSpanUtil::advance( _currentLife, _lifeTime, deltaTime );
        if ( _lifeTime > 0.0f )
        {
            _alpha             = computeAlpha();
            GameObject* pOwner = getOwner();
            if ( pOwner == nullptr )
                return;

            if ( bExpired )
            {
                // 표시만 하면 파괴 목록에 들어가지 않아 오브젝트가 풀로 돌아오지 않는다.
                pOwner->destroy();
                return;
            }

            SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
            if ( pSceneComp != nullptr )
            {
                // 위로 떠오른다 — 월드 위다(돌아가거나 커진 부모 아래에서도). 마커가 이 점을 따라간다.
                float3 pos = pSceneComp->getWorldPosition();
                pos._y += _floatSpeed * deltaTime;
                pSceneComp->setWorldPosition( pos );
            }
        }
        scheduleRefresh();
    }

    void DamageNumberComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( hasBegunPlay() )
            refreshWidget();
    }

    void DamageNumberComponent::onOwnerActiveInHierarchyChanged()
    {
        if ( hasBegunPlay() )
            refreshWidget();
    }

    void DamageNumberComponent::setDamageValue( int32 value )
    {
        _damageValue = value;
        if ( hasBegunPlay() )
            scheduleRefresh();
    }

    void DamageNumberComponent::spawnNumber( GameObjectManager& manager, const float3& position, int32 value )
    {
        GameObjectManager* pManager = &manager;
        manager.executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, position, value]()
        {
            GameObject* pNumber = pManager->createGameObject( hashed_string( "DamageNumber" ) );
            if ( pNumber == nullptr )
                return;
            SceneComponent* pNumberRoot = pNumber->addComponent<SceneComponent>();
            if ( pNumberRoot != nullptr )
                pNumberRoot->setWorldPosition( position );
            // 숫자 가운데가 그 점에 온다.
            WidgetComponent* pMarker = pNumber->addComponent<WidgetComponent>();
            if ( pMarker != nullptr )
                pMarker->setPivot( float2{ 0.5f, 0.5f } );
            DamageNumberComponent* pNumberUi = pNumber->addComponent<DamageNumberComponent>();
            if ( pNumberUi == nullptr )
                return;
            pNumberUi->setLifeTime( kSpawnedLifeTime );
            pNumberUi->setFloatSpeed( kSpawnedFloatSpeed );
            pNumberUi->setDamageValue( value );
        } ) );
    }

    void DamageNumberComponent::setColor( const float4& color )
    {
        _color = color;
        if ( hasBegunPlay() )
            scheduleRefresh();
    }

    WidgetComponent* DamageNumberComponent::findWidgetComponent() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<WidgetComponent>() : nullptr;
    }

    const TextWidget* DamageNumberComponent::findTextWidget() const
    {
        const WidgetComponent* pWidget = findWidgetComponent();
        return pWidget != nullptr ? castTo<TextWidget>( pWidget->getContent() ) : nullptr;
    }

    float32 DamageNumberComponent::computeAlpha() const
    {
        return LifeSpanUtil::computeFade( _currentLife, _lifeTime );
    }

    void DamageNumberComponent::scheduleRefresh()
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
                static_cast<DamageNumberComponent*>( pComponent )->refreshWidget();
        } );
    }

    void DamageNumberComponent::refreshWidget()
    {
        WidgetComponent* pWidget = findWidgetComponent();
        if ( pWidget == nullptr )
            return;
        pWidget->setHidden( isActive() == false );
        TextWidget* pText = castTo<TextWidget>( pWidget->getContent() );
        if ( pText == nullptr )
            return;
        pText->setText( to_string( _damageValue ) );
        pText->setColor( float4{ _color._x, _color._y, _color._z, 1.0f } );
        pText->setOpacity( MathUtil::saturate( _color._w * _alpha ) );
    }
} // namespace sw
