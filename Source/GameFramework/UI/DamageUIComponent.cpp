#include "pch.h"

#include "GameFramework/UI/DamageUIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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

        _currentLife = 0.0f;
        _alpha       = 1.0f;

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
        {
            pOwner->addTag( "UI"_tag );
            // 클립을 못 읽으면 로더가 이유를 남겼다. 숫자 없이 수명만 돈다(오브젝트는 그대로 지워진다).
            _digitClip                  = SpriteClipAsset::acquireShared( _digitClipPath );
            GameObjectManager* pManager = pOwner->getManager();
            if ( _digitClip != nullptr && pManager != nullptr && _spriteBatch.initialize( *pManager, _digitClip->_atlasPath, kMaxGlyphCount ) == false )
                SW_LOG_WARNING( "Damage number sprites could not be created" );
        }
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
            _alpha             = MathUtil::saturate( 1.0f - ( _currentLife / _lifeTime ) );
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
        if ( hasBegunPlay() )
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
