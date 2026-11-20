#include "pch.h"

#include "GameFramework/Kits/Feature/World/Overworld/Component/OverworldWalkerViewComponent.h"

#include "Engine/Animation/Component/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Feature/World/Overworld/Component/OverworldGameModeComponent.h"

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

    OverworldWalkerViewComponent::OverworldWalkerViewComponent()
        : _spriteOffset{ 0.0f, 0.0f }
        , _depthPerUnitY{ 0.01f }
        , _playingAnimation{}
    {
        setCanEverTick( true );
    }

    void OverworldWalkerViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 걸음을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        _playingAnimation.clear();
    }

    void OverworldWalkerViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return;
        const OverworldGameModeComponent* pGameMode = findGameMode<OverworldGameModeComponent>();
        if ( pGameMode == nullptr || pGameMode->isStarted() == false )
            return;
        const float3& feet = pGameMode->getHeroPosition();
        pScene->setLocalPosition( float3{ feet._x + _spriteOffset._x, feet._y + _spriteOffset._y, feet._y * _depthPerUnitY } );

        SpriteAnimatorComponent* pAnimator = pOwner->getComponent<SpriteAnimatorComponent>();
        if ( pAnimator == nullptr )
            return;
        string animation = pGameMode->isHeroWalking() ? "walk_" : "idle_";
        animation += toFacingSuffix( pGameMode->getHeroFacing() );
        if ( animation == _playingAnimation )
            return;
        pAnimator->play( animation );
        _playingAnimation = std::move( animation );
    }
} // namespace sw
