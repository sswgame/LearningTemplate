/**
 * @file NileBuildingComponent.h
 * @brief 건물 하나의 모습 — 디렉터 도시의 같은 번호 건물을 따라 모델(집은 단계마다 다른 집) · 높이 · 자리를 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct CityBuildingDef;

    /**
     * @class NileBuildingComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 모델은 Kenney City Kit (Suburban)입니다. 집은 단계(0..7)마다 더 큰 집 모델로 바뀌고 빈 땅은 낮은 울타리입니다. 밭 · 조각상은 색 상자이고
     *          집이 아니면 단계가 모습을 바꾸지 않습니다. 건물 종류가 바뀌면(허물고 다른 것을 지었다) 디렉터가 오브젝트를 바꿔 세웁니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "Nile Building View", Tooltip = "Shows one building of the Nile director city" )
    class NileBuildingComponent : public Component
    {
    public:
        REFLECT_BODY();

        NileBuildingComponent();
        virtual ~NileBuildingComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터와 건물 번호를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignBuilding( GameObjectHandle director, int32 buildingIndex );

        /** @brief 이 건물 종류가 키트 모델로 그려지면 true(밭 · 조각상은 색 상자) 입니다. */
        static bool hasModel( const CityBuildingDef& def );
        /** @brief 건물의 모델 이름입니다. 색 상자로 남는 것은 nullptr 입니다. */
        static const utf8* findModel( const CityBuildingDef& def, int32 level, bool bInhabited );
        /** @brief 이 뷰가 고를 수 있는 모델 경로를 모두 모읍니다(디렉터가 미리 잡는다). */
        static void   collectModelPaths( vector<string>& outListPath );
        static string makeModelPath( const utf8* pName );

    private:
        PROPERTY( Category = "Building", DisplayName = "Director", Tooltip = "Object with the NileDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Building", DisplayName = "Building Index", Tooltip = "Slot in the simulation building list" )
        int32 _buildingIndex;

        int32 _shownKey; ///< 지금 그린 단계 × 2 + 사람(−1 이면 아직 없다)
    };
} // namespace sw
