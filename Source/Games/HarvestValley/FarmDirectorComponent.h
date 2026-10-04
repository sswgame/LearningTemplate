/**
 * @file FarmDirectorComponent.h
 * @brief HarvestValley 의 규칙을 돌리는 컴포넌트 — 농부 이동 · 도구 · 체력 · 시간 · 가게 · 출하 · 잠 · 날씨 · 자동 농부 · 로그, 그리고 밭 칸(프리팹) 스폰 지시입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 밭 · 달력 · 인벤토리 · 작물 카탈로그의 규칙은 키트(`GF_Farming`)가 맡고, 여기는
 *          "농부가 어디 서서 무엇을 누르는가" 와 칸의 모습(프리팹)을 어디에 세우는지를 압니다. 모습을 매 프레임 맞추는 일은 뷰 컴포넌트
 *          (`FarmSoilComponent` · `FarmCropComponent` · `FarmerComponent` · `FarmTargetComponent` · `FarmSunComponent`)가 이 컴포넌트를 **읽기만** 해서 합니다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고 카메라 리그의 초점을 넣으며, 뷰 · 리그는 `PostUpdate` 에서 읽습니다.
 *          틱 뒤 스폰 · 상태 바이트 보류 · 걷기 · 효과음은 베이스 `GameDirectorComponent` 가 맡습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Framework/GameDirectorComponent.h"
#include "GameFramework/Framework/MaterialTintCache.h"
#include "GameFramework/Kits/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Simulation/Farming/FarmCalendar.h"
#include "GameFramework/Kits/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Simulation/Farming/FarmInventory.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class Material;
    class MaterialInstance;
    class Mesh;

    /** @brief 손에 든 도구입니다. */
    enum class FarmTool : uint8
    {
        Hoe = 0,     ///< 갈기
        WateringCan, ///< 물 주기
        Seeds,       ///< 고른 씨앗 심기
        Hand         ///< 거두기
    };

    /**
     * @class FarmDirectorComponent
     * @brief 농장 한 판입니다. 플레이가 시작되면 작물 카탈로그를 읽고 밭 · 달력 · 인벤토리를 새로 둡니다(봄 1 일, 첫 씨앗 10 개).
     * @details 농장 상태(달력 · 밭 · 인벤토리 · 농부)는 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드 ·
     *          세이브를 넘깁니다(`HarvestValleyGame`). 세운 칸 오브젝트는 핸들로 들고, 상태 저장 전에 걷습니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Director", Tooltip = "Runs the farm rules, the farmer, time and weather, and spawns the field tiles" )
    class FarmDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32   kFieldWidth       = 12;
        static constexpr int32   kFieldHeight      = 8;
        static constexpr int32   kMaxStamina       = 100;
        static constexpr int32   kStartingGold     = 500;
        static constexpr float32 kMinutesPerSecond = 10.0f; ///< 실제 1 초 = 게임 10 분(하루 20 시간 = 2 분)

        FarmDirectorComponent();
        virtual ~FarmDirectorComponent() override;

        /** @brief 농장 상태(달력 · 밭 · 인벤토리 · 농부 · 날씨)를 씁니다 — `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;

        // ---- 뷰가 읽는 것(PostUpdate — 디렉터가 쓰지 않는 그룹) ----
        const FarmCalendar& getCalendar() const { return _calendar; }
        const FarmField&    getField() const { return _field; }
        const float3&       getPlayerPosition() const { return _playerPosition; }
        bool                isRaining() const { return _bRaining == SW_TRUE; }
        /** @brief 농부가 바라보는 칸입니다. 밭 밖이면 false 입니다. */
        bool findTargetTile( int32& outX, int32& outY ) const;
        /** @brief 흙 상태(0 풀 · 1 갈았음 · 2 물 줌)의 모습입니다. 아직 없으면 비어 있다. */
        const shared_ptr<MaterialInstance>& getSoilLook( int32 soilState ) const;
        /** @brief 작물 모습 — 시든 것은 갈색, @p bCropColored 면 작물 색(제 모델이 없는 다 자란 작물), 아니면 정점 색 그대로(흰색)입니다. */
        const shared_ptr<MaterialInstance>& findCropLook( const hashed_string& cropId, bool bWithered, bool bCropColored ) const;
        /** @brief 칸 가운데(땅 높이)입니다. 밭은 원점에서 +X · +Z 로 1 m 칸입니다. */
        static float3 computeTileCenter( int32 x, int32 y );

    protected:
        /** @brief 작물 카탈로그를 읽고 밭 · 달력 · 인벤토리를 새로 둡니다. */
        [[nodiscard]] bool startGame() override;
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 작물 하나의 다 자란 색 모습입니다. */
        struct CropLook
        {
            hashed_string                _cropId{};
            shared_ptr<MaterialInstance> _instance{};
        };

    private:
        void spawnField( GameObjectManager& manager );
        /** @brief 흙 · 작물 모습 인스턴스와 작물 메시를 미리 잡습니다(뷰가 워커에서 파일을 읽지 않게). */
        void prepareLooks( Material* pSoilMaterial, Material* pCropMaterial );

        void updatePlayerInput( float32 deltaTime, const InputManager& input );
        void updateAutoFarmer( float32 deltaTime );
        /** @brief 농부를 @p direction 으로 움직이고 바라보는 쪽을 네 방향으로 맞춥니다. */
        void movePlayer( const float3& direction, float32 deltaTime );
        /** @brief 바라보는 칸에 지금 도구를 씁니다. 체력이 모자라면 아무것도 하지 않습니다. */
        void useTool();
        /** @brief 출하함 옆이면 수확물을 모두 넣습니다. 넣은 개수입니다. */
        int32 shipAllProduce();
        /** @brief 가게 옆이면 고른 씨앗을 하나 삽니다. */
        bool buySelectedSeed();
        /** @brief 하루를 끝냅니다 — 정산 → 다음 날 → 날씨 → 밭 → 체력. @p bPassedOut 이면 체력을 반만 채운다. */
        void endDay( bool bPassedOut );
        void selectSeed( int32 offset );
        /** @brief 카메라 리그의 초점을 밭 가운데와 농부 사이에 넣습니다(리그는 PostUpdate 에서 읽는다). */
        void updateCameraFocus();
        void logStatus( bool bForce );

        bool                 isNearShippingBin() const;
        bool                 isNearShop() const;
        const hashed_string& getSelectedSeed() const;
        /** @brief 핸들의 오브젝트 자리입니다. 풀리지 않으면 @p fallback 입니다. */
        float3 findObjectPosition( GameObjectHandle handle, const float3& fallback ) const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Crop Data", AssetPath, Tooltip = "Crop catalog XML" )
        string _cropDataPath;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _soilPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _cropPrefab;
        PROPERTY( Category = "Look", DisplayName = "Grass Soil", Tooltip = "Tile colour before tilling" )
        float4 _grassSoilColor;
        PROPERTY( Category = "Look", DisplayName = "Tilled Soil", Tooltip = "Tile colour once tilled" )
        float4 _tilledSoilColor;
        PROPERTY( Category = "Look", DisplayName = "Watered Soil", Tooltip = "Tile colour once watered today" )
        float4 _wateredSoilColor;
        PROPERTY( Category = "Look", DisplayName = "Withered Crop", Tooltip = "Crop colour once withered" )
        float4 _witheredCropColor;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the OrthoCameraRigComponent that follows the farmer" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Shipping Bin", Tooltip = "Object whose position is the shipping bin" )
        GameObjectHandle _shippingBin;
        PROPERTY( Category = "Scene", DisplayName = "Shop", Tooltip = "Object whose position is the seed shop" )
        GameObjectHandle _shop;
        PROPERTY( Category = "Farmer", DisplayName = "Player Start", Tooltip = "Where the farmer wakes up", Meta = "Units=m" )
        float3 _playerStart;

        CropCatalog                  _cropCatalog;
        FarmCalendar                 _calendar;
        FarmField                    _field;
        FarmInventory                _inventory;
        vector<hashed_string>        _listSeed;  ///< 카탈로그 순서의 씨앗 아이템(가게 진열)
        MaterialTintCache            _tintCache; ///< 흙 · 작물 색(같은 색은 나눠 쓴다)
        vector<CropLook>             _listCropLook;
        vector<shared_ptr<Mesh>>     _listCropMesh; ///< 작물 모델을 쥐고 있는다(단계가 바뀔 때 뷰가 워커에서 읽지 않게)
        shared_ptr<MaterialInstance> _arrSoilLook[3];
        shared_ptr<MaterialInstance> _plainCropLook;
        shared_ptr<MaterialInstance> _witheredCropLook;
        float3                       _playerPosition;
        float3                       _facing;
        float3                       _shippingBinPosition;
        float3                       _shopPosition;
        GameRandom                   _random; ///< 날씨
        float32                      _autoTimer;
        int32                        _stamina;
        int32                        _selectedSeedIndex;
        int32                        _lastLoggedHour;
        FarmTool                     _tool;
        uint8                        _bRaining : 1;
        uint8                        _reserved : 7;
    };
} // namespace sw
