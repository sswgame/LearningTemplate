/**
 * @file FarmFieldViewComponent.h
 * @brief 농장 생활 키트의 클라이언트 쪽 — 밭 칸마다 흙 · 작물 오브젝트(프리팹)를 세우고 그 모습(머티리얼 · 작물 모델 표)을 들며, 카메라 리그의 초점을 밭과 농부 사이에 둡니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_Farming`, 전용 서버는 짓지 않는다). 디렉터(`FarmGameModeComponent`)의 읽기 API(`getField` · `getPlayerPosition` · `getCropCatalog`)만 쓰고
 *          규칙을 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 칸 뷰(`FarmSoilComponent` · `FarmCropComponent`)가 같은 오브젝트의 디렉터와 이것을 읽는다.
 *          하는 일은 모두 틱 뒤 게임 스레드에서 한다. 디렉터와 같은 `PrePhysics` 그룹이라 카메라 디렉터(`CameraDirectorComponent`, `PostPhysics` 틱 뒤)가 이번 프레임의 초점을 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class FarmGameModeComponent;
    class GameObject;
    class GameObjectManager;
    class Material;
    class MaterialInstance;

    /**
     * @struct FarmCropModel
     * @brief 작물 하나의 모델 표 한 줄입니다(밭 뷰의 PROPERTY — 씬 데이터). 표에 없는 작물은 기본 자라는 모델(잎)을 쓰고, 다 자라면 작물 색을 입는다.
     */
    REFLECT()
    struct FarmCropModel
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Crop catalog id" )
        CatalogKey<CropDef> _cropID{};
        PROPERTY( Tooltip = "Model of the ready crop; empty keeps the growing model tinted with the crop colour" )
        AssetRef<MeshAssetFormat> _readyModel{};
        PROPERTY( Tooltip = "Growing models by stage: the first for stages 0 and 1, then one per stage, the last repeats; empty uses the default growing models" )
        vector<AssetRef<MeshAssetFormat>> _listGrowingModel{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class FarmFieldViewComponent
     * @brief 밭의 모습입니다. 판이 열리면 칸마다 흙 · 작물 오브젝트를 세우고, 상태를 저장하기 전에 걷으며(`despawnViews` — 상태에서 다시 세우는 것이라 싣지 않는다) 다음 틱에 다시 세웁니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Field View",
             Tooltip = "Client side of a farm: spawns the soil and crop views of the field tiles and keeps the camera rig between the field and the farmer of the Farm Game Mode on the same object" )
    class SW_KIT_CLIENT_API FarmFieldViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics; ///< 디렉터와 같은 그룹 — 카메라 디렉터(PostPhysics 틱 뒤)가 이번 프레임의 초점을 읽는다.

        /** @brief 칸 뷰(병렬 틱)가 남 읽기 길로 읽습니다 — 모습은 틱 뒤 게임 스레드(`updateView`)에서만 바뀌고 틱은 일을 미루기만 한다. */
        static constexpr bool kTickReadSafe = true;

        FarmFieldViewComponent();
        virtual ~FarmFieldViewComponent() override;

        /** @brief 세운 칸 오브젝트를 걷습니다. */
        void onEndPlay() override;

        /** @brief 흙 상태(0 풀, 1 갈았음, 2 물 줌)의 모습을 반환합니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& getSoilLook( int32 soilState ) const;
        /** @brief 작물 모습을 반환합니다. 시든 것은 갈색이고, @p bCropColored 면 작물 색(제 모델이 없는 다 자란 작물), 아니면 정점 색 그대로(흰색)입니다. */
        const shared_ptr<MaterialInstance>& findCropLook( const hashed_string& cropID, bool bWithered, bool bCropColored ) const;
        /** @brief 다 자란 작물이 따로 모델을 가졌으면 그 경로, 아니면 nullptr 입니다(자라는 모델에 작물 색을 입힌다). 표는 `_listCropModel` 이다. */
        const string* findReadyModel( const hashed_string& cropID ) const;
        /** @brief 자라는 중인 작물의 단계 @p stage 모델 경로입니다(작물 줄의 자라는 모델, 없으면 기본 자라는 모델). 칸을 세우기 전에 기본 목록이 비지 않았음을 확인했다. */
        const string& findGrowingModel( const hashed_string& cropID, int32 stage ) const;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 작물 하나의 다 자란 색 모습입니다. */
        struct CropLook
        {
            hashed_string                _cropID{};
            shared_ptr<MaterialInstance> _instance{};
        };

    private:
        /** @brief 틱 뒤 게임 스레드: 판이 열렸으면 칸을 세우고(처음 · 걷은 뒤) 카메라 초점을 옮깁니다. */
        void updateView( GameObjectManager& manager, const FarmGameModeComponent& director );
        /** @brief 작물 모델 표(씬 데이터)가 디렉터의 카탈로그와 맞는지 봅니다. 다르면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateModels( const FarmGameModeComponent& director ) const;
        void               spawnField( GameObjectManager& manager, const FarmGameModeComponent& director );
        /** @brief 프리팹 하나를 스폰하고 걷을 목록에 넣습니다. 프리팹을 읽지 못하면 nullptr 입니다. */
        GameObject* spawnTile( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName );
        /** @brief 흙과 작물 모습 인스턴스, 작물 메시를 미리 로드합니다(뷰가 워커에서 파일을 읽지 않게 합니다). */
        void prepareLooks( const FarmGameModeComponent& director, Material* pSoilMaterial, Material* pCropMaterial );
        /** @brief 카메라 리그의 초점을 밭 중심과 농부 사이에 넣습니다. */
        void updateCameraFocus( GameObjectManager& manager, const FarmGameModeComponent& director ) const;

    private:
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _soilPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _cropPrefab;
        PROPERTY( Category = "Look", DisplayName = "Grass Soil", Tooltip = "Tile colour before tilling" )
        float4 _grassSoilColor;
        PROPERTY( Category = "Look", DisplayName = "Tilled Soil", Tooltip = "Tile colour once tilled" )
        float4 _tilledSoilColor;
        PROPERTY( Category = "Look", DisplayName = "Watered Soil", Tooltip = "Tile colour once watered today" )
        float4 _wateredSoilColor;
        PROPERTY( Category = "Look", DisplayName = "Withered Crop", Tooltip = "Crop colour once withered" )
        float4 _witheredCropColor;
        PROPERTY( Category = "Camera", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent that follows the farmer" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Camera", DisplayName = "Camera Follow", Tooltip = "How far the camera focus moves from the field centre towards the farmer", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _cameraFollow;
        PROPERTY( Category = "Crop Models", DisplayName = "Crop Models", Tooltip = "Ready and growing models per crop id" )
        vector<FarmCropModel> _listCropModel;
        PROPERTY( Category = "Crop Models", DisplayName = "Default Growing Models",
                  Tooltip = "Growing models of crops without their own (the first for stages 0 and 1, then one per stage); required" )
        vector<AssetRef<MeshAssetFormat>> _listDefaultGrowingModel;

        vector<GameObjectHandle>     _listTileObject; ///< 세운 칸 오브젝트(흙 · 작물)
        MaterialTintCache            _tintCache;      ///< 흙과 작물 색(같은 색은 나눠 쓴다).
        vector<CropLook>             _listCropLook;
        vector<MeshHandle>           _listCropMesh; ///< 작물 모델 로드를 계속 쥔다(준비되면 메시를 참조한다 — 단계가 바뀔 때 뷰가 다시 읽지 않게 한다).
        shared_ptr<MaterialInstance> _arrSoilLook[3];
        shared_ptr<MaterialInstance> _plainCropLook;
        shared_ptr<MaterialInstance> _witheredCropLook;
        uint8                        _bSpawned  : 1; ///< 칸 오브젝트가 서 있다
        uint8                        _bRejected : 1; ///< 작물 모델 표가 틀려 세우지 않는다(`validateModels`)
        [[maybe_unused]] uint8       _reserved  : 6;
    };
} // namespace sw
