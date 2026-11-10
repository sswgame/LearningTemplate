/**
 * @file ActivatorVolumeComponent.h
 * @brief 활성자가 들어오면 한 번 일을 하는 볼륨의 바탕입니다(튜토리얼 힌트 · 자동 저장). 트리거 콜라이더의 겹침과, 물리 몸이 없는 활성자를 위한 거리 판정을 한 벌로 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/TagID.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ActivatorVolumeComponent
     * @brief 활성자가 들어오는 길은 둘입니다. 같은 오브젝트의 트리거 콜라이더에 겹치거나(`onOverlapBegin`), `_activator` 가 `_radius` 안에 들어옵니다.
     * @details 거리 판정을 함께 두는 까닭: 말에 탄 기수(말은 물리 몸이 없다)나 격자로 걷는 농부처럼 물리 몸이 없는 활성자는 겹침을 내지 못한다. 언리얼 `ATriggerSphere` 와 유니티 `OnTriggerEnter` 는 몸을 요구하고
     *          그런 게임은 스크립트로 거리를 재는데(기억에 의존한 비교), 볼륨마다 되풀이되어 바탕에 둔다. `_radius` 가 0 이면 거리는 재지 않는다.
     *          거리는 틱(워커)에서 읽고, 일은 틱 뒤 게임 스레드에서 한다 — UI · 저장은 틱 안에서 부르지 않는다. 안에 있는 동안 다시 하지 않는다(들어오는 순간만).
     */
    REFLECT( Abstract, Category = "Gameplay", DisplayName = "Activator Volume", Tooltip = "Base of the volumes that do something when an activator enters (trigger overlap or distance)" )
    class SW_GF_API ActivatorVolumeComponent : public Component
    {
    public:
        REFLECT_BODY();

        ActivatorVolumeComponent();
        ~ActivatorVolumeComponent() override = default;

        void onTick( TickContext& context ) override;
        void onOverlapBegin( const OverlapInfo& overlap ) override;

        /** @brief 활성자가 들어온 것으로 칩니다(겹침 · 거리 판정 · 테스트). 한 번만 하는 볼륨이 이미 했거나 태그가 맞지 않으면 false 이고, 일을 했으면 true 입니다. */
        bool activate( const GameObject* pActivator );
        bool hasFired() const { return _fireCount > 0; }
        /** @brief 거리 판정의 활성자와 반지름을 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setProximity( GameObjectHandle activator, float32 radius );

    protected:
        /** @brief 활성자가 들어왔을 때의 일입니다. 일을 했으면 true 입니다(false 면 한 번도 안 한 것으로 남아 다음에 다시 시도한다). 게임 스레드입니다. */
        [[nodiscard]] virtual bool onVolumeActivated( const GameObject& activator ) = 0;
        /** @brief 태그를 정하고 발동 기록을 지웁니다. 파생의 `configure` 가 부릅니다. */
        void resetActivation( TagID activatorTag );

    private:
        PROPERTY( Category = "Volume", DisplayName = "Activator Tag", Tooltip = "Only objects with this tag activate it (empty: any object)" )
        TagID _activatorTag;
        PROPERTY( Category = "Volume", DisplayName = "Activator", Tooltip = "Object whose distance is measured (for activators without a physics body); None: only trigger overlaps" )
        GameObjectHandle _activator;
        PROPERTY( Category = "Volume", DisplayName = "Radius", Min = 0.0, Tooltip = "Activator inside this distance counts as entering (0: distance is not measured)", Units = m )
        float32 _radius;
        PROPERTY( RuntimeState, Category = "Volume", DisplayName = "Fire Count", Tooltip = "Runtime" )
        int32 _fireCount;
        PROPERTY( Category = "Volume", DisplayName = "Once", Tooltip = "Fire only the first time" )
        bool _bOnce;
        bool _bActivatorInside; ///< 지난 틱에 활성자가 반지름 안에 있었다(들어오는 순간만 일을 한다)
    };
} // namespace sw
