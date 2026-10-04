/**
 * @file NileWalkerComponent.h
 * @brief 일꾼 하나의 모습 — 디렉터 도시의 같은 자리 일꾼을 따라 자리 · 보임 · 종류 색을 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class NileWalkerComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 일꾼 수가 줄면 남는 자리는 숨깁니다(오브젝트를 만들고 지우지 않는 풀). 목록 자리는 다른 일꾼에게 넘어갈 수 있어 종류가 바뀐 때만 색을 다시 입힙니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "Nile Walker View", Tooltip = "Follows one walker of the Nile director city" )
    class NileWalkerComponent : public Component
    {
    public:
        REFLECT_BODY();

        NileWalkerComponent();
        virtual ~NileWalkerComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터와 일꾼 자리를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignWalker( GameObjectHandle director, int32 walkerIndex );

    private:
        PROPERTY( Category = "Walker", DisplayName = "Director", Tooltip = "Object with the NileDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Walker", DisplayName = "Walker Index", Tooltip = "Slot in the simulation walker list" )
        int32 _walkerIndex;
        PROPERTY( Category = "Walker", DisplayName = "Height", Tooltip = "Capsule centre above the ground", Meta = "Units=m" )
        float32 _height;

        const void* _pShownLook; ///< 지금 입은 모습(인스턴스 주소 — 비교만 한다)
    };
} // namespace sw
