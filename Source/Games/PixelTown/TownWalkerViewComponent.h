/**
 * @file TownWalkerViewComponent.h
 * @brief 주인공의 모습 — 디렉터의 주인공 자리를 따라 자기 오브젝트를 옮기고, 바라보는 방향과 걸음에 맞는 스프라이트 애니메이션을 고릅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class TownWalkerViewComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 자리와 같은 오브젝트의 `SpriteAnimatorComponent` 에만 씁니다.
     * @details 애니메이션 이름은 `<idle|walk>_<down|left|right|up>` 이고, 스프라이트 클립(`.sprite.json`)의 구간 이름이 그 정본이다 — 클립에 없는 이름은
     *          애니메이터가 알린다. 같은 이름이면 다시 틀지 않아(처음부터 다시 재생하지 않아) 칸을 이어 걸어도 걸음 동작이 끊기지 않는다.
     *          앞뒤 정렬은 깊이(Z)다: 직교 카메라의 투명 정렬(`engine/data/render2d.xml` 의 Auto = 시선 축)이 Z 가 큰 것을 먼저 그리므로,
     *          화면 위쪽(월드 Y 가 큰) 캐릭터가 뒤에 그려지도록 Z = 발 Y × `_depthPerUnitY` 로 둔다(유니티 Custom Axis (0, 1, 0) 와 같은 결과).
     */
    REFLECT( Category = "Overworld", DisplayName = "Town Walker View", Tooltip = "Follows the town director hero and picks the walk or idle animation for its facing" )
    class TownWalkerViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        TownWalkerViewComponent();
        virtual ~TownWalkerViewComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        PROPERTY( Category = "Walker", DisplayName = "Director", Tooltip = "Object with the TownDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Walker", DisplayName = "Sprite Offset", Tooltip = "Sprite center from the feet (a 16x32 hero stands half a tile above its tile center)", Units = m )
        float2 _spriteOffset;
        PROPERTY( Category = "Walker", DisplayName = "Depth Per Unit Y", Tooltip = "Z per world unit of feet Y; positive draws higher characters behind lower ones" )
        float32 _depthPerUnitY;

        string _playingAnimation; ///< 마지막으로 튼 애니메이션 이름(같으면 다시 틀지 않는다)
    };
} // namespace sw
