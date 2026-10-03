#include "pch.h"

#include "GameFramework/UI/DamageUIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/SpriteClipCache.h"

namespace sw
{
    DamageUIComponent::DamageUIComponent()
        : _damageValue{ 0 }
        , _lifeTime{ 0.0f }
        , _currentLife{ 0.0f }
        , _floatSpeed{ 0.0f }
        , _alpha{ 0.0f }
        , _glyphSize{ 0.3f, 0.4f }
        , _color{ 1.0f, 0.85f, 0.25f, 1.0f }
        , _digitClipPath{ "engine/textures/ui/digits.sprite.json" }
        , _digitClip{}
        , _spriteBatch{}
    {
    }

    void DamageUIComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );

        // 흐른 수명은 처음으로 되돌리지 않는다 — 떠 있는 동안 상태를 다시 읽은 숫자(플레이 중 되돌리기 · 핫 리로드)가 수명을 다시 시작하지 않게.
        // 새로 만든 숫자는 0 이다. 알파는 흐른 수명에서 다시 구한다.
        _currentLife = MathUtil::max( _currentLife, 0.0f );
        _alpha       = computeAlpha();
        acquireGlyphSprites();
        layoutSprites();
    }

    void DamageUIComponent::onEndPlay()
    {
        _spriteBatch.shutdown();
        _digitClip.reset();
        Component::onEndPlay();
    }

    void DamageUIComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        _currentLife += deltaTime;
        if ( _lifeTime > 0.0f )
        {
            _alpha             = computeAlpha();
            GameObject* pOwner = getOwner();
            if ( pOwner == nullptr )
                return;

            if ( _currentLife >= _lifeTime )
            {
                // 표시만 하면 파괴 목록에 들어가지 않아 오브젝트가 풀로 돌아오지 않는다.
                pOwner->destroy();
                return;
            }

            SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
            if ( pSceneComp != nullptr )
            {
                // 위로 떠오른다 — 월드 위다(돌아가거나 커진 부모 아래에서도).
                float3 pos = pSceneComp->getWorldPosition();
                pos._y += _floatSpeed * deltaTime;
                pSceneComp->setWorldPosition( pos );
            }
        }
        scheduleLayout();
    }

    void DamageUIComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( hasBegunPlay() == false )
            return;
        // 글리프 클립 경로를 바꾸면(인스펙터 · 에셋 핫 리로드 알림 — 값이 같아도) 클립을 다시 잡고 스프라이트를 새 아틀라스로 다시 만든다.
        // 스프라이트를 만드는 것은 구조 변경이라 틱 밖에서 한다.
        static const hashed_string s_digitClipPathName( "_digitClipPath" );
        if ( propertyName == s_digitClipPathName )
        {
            GameObject*        pOwner   = getOwner();
            GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
            if ( pManager != nullptr )
            {
                const ComponentHandle handle = getHandle();
                pManager->executeOrDeferPostTick( [pManager, handle]()
                {
                    Component* pComponent = pManager->resolveComponent( handle );
                    if ( pComponent == nullptr )
                        return;
                    DamageUIComponent* pDamage = static_cast<DamageUIComponent*>( pComponent );
                    pDamage->acquireGlyphSprites();
                    pDamage->layoutSprites();
                } );
                return;
            }
        }
        layoutSprites();
    }

    void DamageUIComponent::onOwnerActiveInHierarchyChanged()
    {
        if ( hasBegunPlay() )
            layoutSprites();
    }

    void DamageUIComponent::setDamageValue( int32 value )
    {
        _damageValue = value;
        if ( hasBegunPlay() )
            scheduleLayout();
    }

    void DamageUIComponent::spawnNumber( GameObjectManager& manager, const float3& position, int32 value )
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
            DamageUIComponent* pNumberUi = pNumber->addComponent<DamageUIComponent>();
            if ( pNumberUi == nullptr )
                return;
            pNumberUi->setLifeTime( kSpawnedLifeTime );
            pNumberUi->setFloatSpeed( kSpawnedFloatSpeed );
            pNumberUi->setDamageValue( value );
        } ) );
    }

    void DamageUIComponent::setColor( const float4& color )
    {
        _color = color;
        if ( hasBegunPlay() )
            scheduleLayout();
    }

    uint32 DamageUIComponent::makeGlyphFrames( int32 value, int32 ( &outArrFrame )[kMaxGlyphCount] )
    {
        // 오른쪽 자리부터 뽑아 뒤집는다. 자리마다 절댓값을 뽑으므로 INT32_MIN 의 부호를 뒤집다 넘치는 일이 없다.
        int32  arrReversed[kMaxGlyphCount] = {};
        uint32 digitCount                  = 0;
        int32  remaining                   = value;
        do
        {
            const int32 digit         = remaining % 10;
            arrReversed[digitCount++] = ( digit < 0 ) ? -digit : digit;
            remaining /= 10;
        } while ( remaining != 0 && digitCount < kMaxGlyphCount );

        uint32 glyphCount = 0;
        if ( value < 0 )
            outArrFrame[glyphCount++] = kMinusGlyphFrame;
        for ( uint32 digitIndex = digitCount; digitIndex > 0 && glyphCount < kMaxGlyphCount; --digitIndex )
            outArrFrame[glyphCount++] = arrReversed[digitIndex - 1];
        return glyphCount;
    }

    float32 DamageUIComponent::computeAlpha() const
    {
        return ( _lifeTime > 0.0f ) ? MathUtil::saturate( 1.0f - ( _currentLife / _lifeTime ) ) : 1.0f;
    }

    void DamageUIComponent::acquireGlyphSprites()
    {
        _spriteBatch.shutdown();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        // 클립을 못 읽으면 로더가 이유를 남겼다. 숫자 없이 수명만 돈다(오브젝트는 그대로 지워진다).
        _digitClip = SpriteClipCache::acquire( _digitClipPath );
        if ( _digitClip != nullptr && pManager != nullptr && _spriteBatch.initialize( *pManager, _digitClip->_atlasPath, kMaxGlyphCount ) == false )
            SW_LOG_WARNING( "Damage number sprites could not be created" );
    }

    void DamageUIComponent::scheduleLayout()
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
                static_cast<DamageUIComponent*>( pComponent )->layoutSprites();
        } );
    }

    void DamageUIComponent::layoutSprites()
    {
        if ( _spriteBatch.isInitialized() == false || _digitClip == nullptr )
            return;
        const GameObject*     pOwner  = getOwner();
        const SceneComponent* pAnchor = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bShown  = isActive() && pAnchor != nullptr;
        _spriteBatch.setVisible( bShown );
        if ( bShown == false )
            return;

        int32         arrFrame[kMaxGlyphCount] = {};
        const uint32  glyphCount               = makeGlyphFrames( _damageValue, arrFrame );
        const float3  center                   = pAnchor->getWorldPosition();
        const float32 advance                  = _glyphSize._x;
        const float32 firstX                   = center._x - advance * 0.5f * static_cast<float32>( glyphCount - 1 );
        const float4  tint{ _color._x, _color._y, _color._z, _color._w * MathUtil::saturate( _alpha ) };
        for ( uint32 glyphIndex = 0; glyphIndex < kMaxGlyphCount; ++glyphIndex )
        {
            const SpriteClipFrame* pFrame = ( glyphIndex < glyphCount ) ? _digitClip->findFrame( arrFrame[glyphIndex] ) : nullptr;
            if ( pFrame == nullptr )
            {
                _spriteBatch.setEntryVisible( glyphIndex, false );
                continue;
            }
            const float3 glyphCenter{ firstX + advance * static_cast<float32>( glyphIndex ), center._y, center._z };
            _spriteBatch.setEntry( glyphIndex, SpriteInstanceBatch::makeQuadWorld( glyphCenter, _glyphSize._x, _glyphSize._y ), pFrame->_uvRect, tint );
        }
    }
} // namespace sw
