#include "pch.h"

#include "Games/PixelTown/TownWalkerViewComponent.h"

#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/PixelTown/TownDirectorComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 애니메이션 이름의 방향 꼬리입니다(`FacingDir` 순서). */
        const utf8* toFacingSuffix( FacingDir facing )
        {
            switch ( facing )
            {
                case FacingDir::Down:
                    return "down";
                case FacingDir::Left:
                    return "left";
                case FacingDir::Right:
                    return "right";
                case FacingDir::Up:
                    return "up";
            }
            return "down";
        }
    } // namespace

    TownWalkerViewComponent::TownWalkerViewComponent()
        : _director{}
        , _spriteOffset{ 0.0f, 0.0f }
        , _depthPerUnitY{ 0.01f }
        , _playingAnimation{}
    {
        setCanEverTick( true );
    }

    void TownWalkerViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 걸음을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        _playingAnimation.clear();
    }

    void TownWalkerViewComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SceneComponent*    pScene   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pScene == nullptr )
            return;
        const TownDirectorComponent* pDirector = GameDirectorComponent::resolve<TownDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const float3& feet = pDirector->getHeroPosition();
        pScene->setLocalPosition( float3{ feet._x + _spriteOffset._x, feet._y + _spriteOffset._y, feet._y * _depthPerUnitY } );

        SpriteAnimatorComponent* pAnimator = pOwner->getComponent<SpriteAnimatorComponent>();
        if ( pAnimator == nullptr )
            return;
        string animation = pDirector->isHeroWalking() ? "walk_" : "idle_";
        animation += toFacingSuffix( pDirector->getHeroFacing() );
        if ( animation == _playingAnimation )
            return;
        pAnimator->play( animation );
        _playingAnimation = std::move( animation );
    }
} // namespace sw
