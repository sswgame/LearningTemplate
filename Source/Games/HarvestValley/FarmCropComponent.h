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

        /** @brief 이 뷰가 고를 수 있는 모델 경로를 모두 모읍니다(디렉터가 미리 잡는다). */
        static void collectModelPaths( vector<string>& outListPath );
        /** @brief 다 자란 작물이 따로 모델을 가졌으면 그 이름, 아니면 nullptr 입니다(잎 모델에 작물 색을 입힌다). */
        static const utf8* findReadyModel( const hashed_string& cropID );
        /** @brief 자라는 중인 작물의 모델 이름입니다. */
        static const utf8* findGrowingModel( const hashed_string& cropID, int32 stage );
        static string      makeModelPath( const utf8* pName );

    private:
        PROPERTY( Category = "Tile", DisplayName = "Director", Tooltip = "Object with the FarmDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Tile", DisplayName = "Tile Index", Tooltip = "Row-major tile of the director field" )
        int32 _tileIndex;
        PROPERTY( Category = "Tile", DisplayName = "Model Scale", Tooltip = "Scale of the grown crop model", Min = 0.0 )
        float32 _modelScale;

        int32 _cropState; ///< 지금 그린 작물 상태(−1 이면 아직 없다)
    };
} // namespace sw
