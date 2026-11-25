/**
 * @file TagGateComponent.h
 * @brief 요구 태그를 모두 가진 것이 가까이 오면 열리는 문입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class TagGateComponent
     * @brief 막는 몸(콜라이더와 그래픽)을 가진 문입니다. `_requiredTags`(Ability.DoubleJump, Ability.Bomb)를 모두 가진 것이 `_openRadius` 안에 오면 몸을 끕니다.
     *        `_bStaysOpen` 이 켜져 있으면 한 번 열린 뒤 그대로 유지하고(진행 저장은 PROPERTY `_bOpen`), 아니면 떠날 때 닫힙니다. 2D 는 `_bPlanar` 로 Z 를 뺀 거리를 씁니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Ability Gate", Tooltip = "Opens for objects carrying the required ability tags" )
    class SW_GF_API TagGateComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kTickInterval = constant::time::kDefaultFixedStepSeconds; ///< 고정 걸음마다 한 번 — 지연 · 간격을 걸음 수로 센다(프레임 시간과 무관하게 같은 걸음)

        TagGateComponent();
        virtual ~TagGateComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        void stepOnce();
        bool isOpen() const { return _bOpen; }

    private:
        PROPERTY( Category = "Gate", DisplayName = "Required Tags", Tooltip = "Ability tags that open the gate" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Gate", DisplayName = "Open Radius", Min = 0.0, Units = m )
        float32 _openRadius;
        PROPERTY( Category = "Gate", DisplayName = "Stays Open" )
        bool _bStaysOpen;
        PROPERTY( Category = "Gate", DisplayName = "Planar", Tooltip = "2D distance (ignore Z)" )
        bool _bPlanar;
        PROPERTY( RuntimeState, Category = "Gate", DisplayName = "Open", Tooltip = "Runtime / progress" )
        bool _bOpen; ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다

        SW_TRANSIENT vector<GameObject*> _listTagged; ///< 스텝마다 태그 색인의 답을 받는 버퍼(용량 재사용, `TagIndex::findObjectsWithTags`)
    };
} // namespace sw
