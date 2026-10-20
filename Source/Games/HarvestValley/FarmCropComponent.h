/**
 * @file FarmCropComponent.h
 * @brief 밭 칸 하나의 작물 모습 — 디렉터 밭의 같은 칸을 따라 단계 모델 · 크기 · 색 · 보임을 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class FarmCropComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 단계(0..4) · 다 자람 · 시듦 · 작물이 바뀔 때만 다시 칠합니다. 모델은 Kenney Nature Kit — 옥수수는 제 단계 모델, 나머지는 잎 두 단계,
     *          다 자라면 제 모델(무 · 당근 · 호박 · 옥수수)이고, 제 모델이 없는 작물은 잎에 작물 색을 입힙니다. 모델은 디렉터가 미리 쥐고 있어 바꿀 때 읽지 않습니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Crop View", Tooltip = "Shows the crop of one field tile of the farm director" )
    class FarmCropComponent : public Component
    {
    public:
        REFLECT_BODY();

        FarmCropComponent();
        virtual ~FarmCropComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터와 칸 번호(행 우선)를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignTile( GameObjectHandle director, int32 tileIndex );

        // 작물 → 모델은 디렉터의 작물 모델 표(씬 데이터, `FarmDirectorComponent::findReadyModel` · `findGrowingModel`)가 정한다.

    private:
        PROPERTY( Category = "Tile", DisplayName = "Director", Tooltip = "Object with the FarmDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Tile", DisplayName = "Tile Index", Tooltip = "Row-major tile of the director field" )
        int32 _tileIndex;
        /**
         * @brief Kenney Nature Kit 모델은 키트 1 칸이 밭 1 칸이지만, 비스듬히 내려다보는 직교 카메라에서는 작물이 작아 보여 기본 1.6 배로 키운다(작물 폭 0.35 → 0.56 m).
         * @details 키트 모델의 바닥은 원점보다 0.05 아래다(Model Floor). 흙 윗면(Soil Top)에 얹을 때 배율만큼 올린다.
         */
        PROPERTY( Category = "Tile", DisplayName = "Model Scale", Tooltip = "Scale of the grown crop model", Min = 0.0 )
        float32 _modelScale;
        PROPERTY( Category = "Tile", DisplayName = "Model Floor", Tooltip = "How far the kit model's base sits below its origin, per unit of scale", Min = 0.0, Units = m )
        float32 _modelFloor;
        PROPERTY( Category = "Tile", DisplayName = "Soil Top", Tooltip = "Height of the soil block's top the crop stands on", Min = 0.0, Units = m )
        float32 _soilTop;

        int32 _cropState; ///< 지금 그린 작물 상태(−1 이면 아직 없다)
    };
} // namespace sw
