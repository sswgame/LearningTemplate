#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Animation/Component/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Overworld/Component/OverworldGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class OverworldWalkerViewComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 자리와 같은 오브젝트의 `SpriteAnimatorComponent` 에만 씁니다.
     * @details 애니메이션 이름은 `<idle|walk>_<down|left|right|up>` 이고, 스프라이트 클립(`.sprite.xml`)의 구간 이름이 그 기준이다 — 클립에 없는 이름은
     *          애니메이터가 알린다. 같은 이름이면 다시 틀지 않아(처음부터 다시 재생하지 않아) 칸을 이어 걸어도 걸음 동작이 끊기지 않는다.
     *          앞뒤 정렬은 깊이(Z)다: 직교 카메라의 투명 정렬(`engine/data/render2d.xml` 의 Auto = 시선 축)이 Z 가 큰 것을 먼저 그리므로,
     *          화면 위쪽(월드 Y 가 큰) 캐릭터가 뒤에 그려지도록 Z = 발 Y × `_depthPerUnitY` 로 둔다(유니티 Custom Axis (0, 1, 0) 와 같은 결과).
     */
    REFLECT( Category = "Overworld", DisplayName = "Overworld Walker View", Tooltip = "Follows the overworld game mode hero and picks the walk or idle animation for its facing" )
    class OverworldWalkerViewComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 디렉터(PrePhysics)가 이 프레임의 걸음을 끝낸 뒤에 읽는다.

        OverworldWalkerViewComponent();
        virtual ~OverworldWalkerViewComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Category = "Walker", DisplayName = "Sprite Offset", Tooltip = "Sprite center from the feet (a 16x32 hero stands half a tile above its tile center)", Units = m )
        float2 _spriteOffset;
        PROPERTY( Category = "Walker", DisplayName = "Depth Per Unit Y", Tooltip = "Z per world unit of feet Y; positive draws higher characters behind lower ones" )
        float32 _depthPerUnitY;

        SW_TRANSIENT string _playingAnimation; ///< 마지막으로 튼 애니메이션 이름(같으면 다시 틀지 않는다)
    };

    namespace
    {
        /** @brief 애니메이션 이름의 방향 꼬리입니다(`FacingDirection` 순서). */
        const utf8* toFacingSuffix( FacingDirection facing )
        {
            switch ( facing )
            {
                case FacingDirection::Down:
                    return "down";
                case FacingDirection::Left:
                    return "left";
                case FacingDirection::Right:
                    return "right";
                case FacingDirection::Up:
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

#include "OverworldWalkerViewComponent.cpp.gen.inl"
