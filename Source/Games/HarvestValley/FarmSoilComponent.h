/**
 * @file FarmSoilComponent.h
 * @brief 밭 칸 하나의 흙 모습 — 디렉터 밭의 같은 칸을 따라 풀 · 갈았음 · 물 줌 색을 입힙니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class FarmSoilComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 상태가 바뀔 때만 모습(디렉터가 나눠 주는 머티리얼 인스턴스)을 바꿉니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Soil View", Tooltip = "Colours one field tile by the farm director soil state" )
    class FarmSoilComponent : public Component
    {
    public:
        REFLECT_BODY();

        FarmSoilComponent();
        virtual ~FarmSoilComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터와 칸 번호(행 우선)를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignTile( GameObjectHandle director, int32 tileIndex );

    private:
        PROPERTY( Category = "Tile", DisplayName = "Director", Tooltip = "Object with the FarmDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Tile", DisplayName = "Tile Index", Tooltip = "Row-major tile of the director's field" )
        int32 _tileIndex;

        int32 _soilState; ///< 지금 입은 흙 상태(−1 이면 아직 없다)
    };
} // namespace sw
