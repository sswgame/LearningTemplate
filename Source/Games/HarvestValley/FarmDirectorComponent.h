/**
 * @file FarmDirectorComponent.h
 * @brief HarvestValley 의 규칙을 돌리는 컴포넌트입니다. 농부 이동, 도구, 체력, 시간, 가게, 출하, 수면, 날씨, 자동 농부, 로그를 맡고 밭 타일(프리팹) 스폰을 지시합니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 밭, 달력, 인벤토리, 작물 카탈로그의 규칙은 키트(`GF_Farming`)가 맡는다.
 *          이 컴포넌트는 "농부가 어디 서서 무엇을 하는가"의 규칙과 타일의 모습(프리팹)을 어디에 스폰하는지를 안다. 농부가 **무엇을 누르는가는 농부 폰의 의도**
 *          (`PawnComponent`. 플레이어 조종자 또는 `FarmAutoFarmerAIComponent` 가 낸다)이고, 디렉터는 입력 맵을 읽지 않는다. 모습을 매 프레임 맞추는 일은 뷰 컴포넌트
 *          (`FarmSoilComponent`, `FarmCropComponent`, `FarmerComponent`, `FarmTargetComponent`, `FarmSunComponent`)가 이 컴포넌트를 **읽기만** 해서 처리한다.
 *
 *          틱 규칙은 다음과 같다. 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고 카메라 리그의 초점을 넣으며, 뷰와 리그는 `PostUpdate` 에서 읽는다.
 *          틱이 끝난 뒤의 스폰, 상태 바이트 보류, 정리, 효과음은 베이스 `GameDirectorComponent` 가 맡는다.
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

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmShippingBin.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class PawnComponent;

    /**
     * @struct FarmCropModel
     * @brief 작물 하나의 모델 표 한 줄입니다(디렉터의 PROPERTY — 씬 데이터). 표에 없는 작물은 기본 자라는 모델(잎)을 쓰고, 다 자라면 작물 색을 입는다.
     */
    REFLECT()
    struct FarmCropModel
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Crop catalog id" )
        hashed_string _cropID{};
        PROPERTY( AssetPath, Tooltip = "Model of the ready crop; empty keeps the growing model tinted with the crop colour" )
        string _readyModel{};
        PROPERTY( Tooltip = "Growing models by stage: the first for stages 0 and 1, then one per stage, the last repeats; empty uses the default growing models" )
        vector<string> _listGrowingModel{};
    };
} // namespace sw

namespace sw
{
    /** @brief 손에 든 도구입니다. */
    enum class FarmTool : uint8
    {
        Hoe = 0,     ///< 갈기.
        WateringCan, ///< 물 주기.
        Seeds,       ///< 고른 씨앗 심기.
        Hand         ///< 거두기.
    };

    /** @brief 농부 폰의 버튼입니다. 폰 스키마(씬의 `PawnComponent`)의 이름은 `FarmDirectorComponent::getButtonName` 이고, 입력 맵(`data/farm.input.xml`)의 액션 이름과 같습니다. */
    enum class FarmButton : uint8
    {
        Tool1 = 0, ///< 괭이
        Tool2,     ///< 물뿌리개
        Tool3,     ///< 씨앗
        Tool4,     ///< 손
        SeedPrev,
        SeedNext,
        Use,    ///< 바라보는 타일에 도구를 쓴다.
        Ship,   ///< 출하함에 수확물을 모두 넣는다.
        Buy,    ///< 가게에서 고른 씨앗을 하나 산다.
        Sleep,  ///< 하루를 끝낸다.
        Status, ///< 상태 로그
        Count
    };

    /**
     * @class FarmDirectorComponent
     * @brief 농장 한 판을 진행합니다. 플레이가 시작되면 작물 카탈로그를 읽고 밭, 달력, 인벤토리를 새로 만듭니다(봄 1일, 첫 씨앗 10개).
     * @details 농장 상태(달력, 밭, 인벤토리, 농부)는 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드와
     *          세이브를 넘긴다(`HarvestValleyGame`). 스폰한 타일 오브젝트는 핸들로 보관하고, 상태를 저장하기 전에 정리한다.
     *          **빙의**: 농부(씬의 `_farmer` 오브젝트의 폰)는 자동 빙의(`Player0`)로 플레이어 조종자가 조종하다가, 자동 플레이 스위치가 켜지면 틱이 끝난 뒤 플러시에서
     *          자동 농부 AI(`_autoFarmerPrefab`)가, 꺼지면 다시 플레이어 조종자가 조종한다. 디렉터 안에 자동 플레이 분기는 없다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Director", Tooltip = "Runs the farm rules, the farmer, time and weather, and spawns the field tiles" )
    class FarmDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kFieldWidth   = 12;
        static constexpr int32 kFieldHeight  = 8;
        static constexpr int32 kBagSlotCount = 24; ///< 플레이어 가방 슬롯 수(공유 상태).
        static constexpr int32 kButtonCount  = static_cast<int32>( FarmButton::Count );

        FarmDirectorComponent();
        virtual ~FarmDirectorComponent() override;

        /** @brief 농장 상태(밭, 출하함 구간, 농부, 날씨)를 씁니다. 달력, 가방, 돈은 공유 상태(`SessionStateComponent`)가 싣습니다. `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;

        // ---- 뷰가 읽는 것(PostUpdate. 디렉터가 쓰지 않는 그룹) ----
        /** @brief 공유 시계의 시각(0~24)입니다. 디렉터가 틱마다 옮겨 적으며, 뷰는 PostUpdate 에서 이것만 읽습니다. */
        float32          getHourOfDay() const { return _hourOfDay; }
        const FarmField& getField() const { return _field; }
        const float3&    getPlayerPosition() const { return _playerPosition; }
        bool             isRaining() const { return _bRaining == SW_TRUE; }

        // ---- 자동 농부(조종자)가 읽는 것. 틱 밖(조종 시스템의 판단)에서 읽는다 ----
        /** @brief 농부가 바라보는 방향(네 방향 단위 벡터)입니다. */
        const float3&                getFacing() const { return _facing; }
        FarmTool                     getTool() const { return _tool; }
        int32                        getStamina() const { return _stamina; }
        int32                        getDayStarted() const { return _dayStarted; }
        int32                        getSelectedSeedIndex() const { return _selectedSeedIndex; }
        const vector<hashed_string>& getSeeds() const { return _listSeed; }
        const CropCatalog&           getCropCatalog() const { return _cropCatalog; }
        const hashed_string&         getCurrency() const { return _shipment.getCurrency(); }
        const float3&                getShippingBinPosition() const { return _shippingBinPosition; }
        const float3&                getShopPosition() const { return _shopPosition; }
        bool                         isNearShippingBin() const;
        bool                         isNearShop() const;
        /** @brief 농부 폰을 지금 자동 농부 AI 가 조종하고 있으면 true 를 반환합니다(시나리오 프로브). */
        bool isFarmerDrivenByAI() const;
        /** @brief 폰 버튼의 이름을 반환합니다(입력 맵 액션 이름과 같습니다). */
        static const utf8* getButtonName( FarmButton button );

        /** @brief 농부가 바라보는 타일을 반환합니다. 밭 밖이면 false 입니다. */
        bool findTargetTile( int32& outX, int32& outY ) const;
        /** @brief 흙 상태(0 풀, 1 갈았음, 2 물 줌)의 모습을 반환합니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& getSoilLook( int32 soilState ) const;
        /** @brief 작물 모습을 반환합니다. 시든 것은 갈색이고, @p bCropColored 면 작물 색(제 모델이 없는 다 자란 작물), 아니면 정점 색 그대로(흰색)입니다. */
        const shared_ptr<MaterialInstance>& findCropLook( const hashed_string& cropID, bool bWithered, bool bCropColored ) const;
        /** @brief 타일 중심(땅 높이)을 반환합니다. 밭은 원점에서 +X, +Z 방향으로 1m 타일입니다. */
        static float3 computeTileCenter( int32 x, int32 y );
        /** @brief 다 자란 작물이 따로 모델을 가졌으면 그 경로, 아니면 nullptr 입니다(자라는 모델에 작물 색을 입힌다). 표는 `_listCropModel` 이다. */
        const string* findReadyModel( const hashed_string& cropID ) const;
        /** @brief 자라는 중인 작물의 단계 @p stage 모델 경로입니다(작물 줄의 자라는 모델, 없으면 기본 자라는 모델). 판을 열 때 기본 목록이 비지 않았음을 확인했다. */
        const string& findGrowingModel( const hashed_string& cropID, int32 stage ) const;

    protected:
        /** @brief 작물 카탈로그를 읽고 밭과 출하함을 새로 만들며, 같은 오브젝트의 공유 상태를 엽니다(새 판일 때만 시작 돈과 씨앗을 줍니다). */
        [[nodiscard]] bool startGame() override;
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bPossessionDirty == SW_TRUE; }

    private:
        /** @brief 작물 하나의 다 자란 색 모습입니다. */
        struct CropLook
        {
            hashed_string                _cropID{};
            shared_ptr<MaterialInstance> _instance{};
        };

    private:
        void spawnField( GameObjectManager& manager );
        /** @brief 흙과 작물 모습 인스턴스, 작물 메시를 미리 로드합니다(뷰가 워커에서 파일을 읽지 않게 합니다). */
        void prepareLooks( Material* pSoilMaterial, Material* pCropMaterial );

        /** @brief 농부 폰의 이번 틱 의도를 처리합니다. 이동 축으로 걷고, 발동한 버튼(도구, 씨앗, 사용, 출하, 구매, 수면, 상태)을 규칙대로 실행합니다. */
        void applyFarmerIntent( float32 deltaTime, const PawnComponent& pawn );
        /** @brief 농부를 @p direction 방향으로 움직이고 바라보는 방향을 네 방향으로 맞춥니다. 벡터 길이 1 이 걷는 속도의 최대치이며, 길이가 곧 속도 비율입니다. */
        void movePlayer( const float3& direction, float32 deltaTime );
        /** @brief 농부 폰을 반환합니다(`_farmer` 오브젝트). 없으면 nullptr 입니다. */
        PawnComponent* findFarmerPawn() const;
        /** @brief 농부 폰을 자동 플레이 스위치에 맞는 조종자(자동 농부 AI 또는 플레이어 조종자)가 조종하게 합니다. 틱 밖(플러시)에서 부릅니다. */
        void syncFarmerPossession( GameObjectManager& manager );
        /** @brief 바라보는 타일에 현재 도구를 씁니다. 체력이 모자라면 아무것도 하지 않습니다. */
        void useTool();
        /** @brief 출하함 옆이면 수확물을 모두 넣고, 넣은 개수를 반환합니다. */
        int32 shipAllProduce();
        /** @brief 가게 옆이면 고른 씨앗을 하나 삽니다. */
        bool buySelectedSeed();
        /** @brief 하루를 끝냅니다. 정산, 다음 날, 날씨, 밭, 체력 순서로 처리하며, @p bPassedOut 이면 체력을 반만 채웁니다. */
        void endDay( bool bPassedOut );
        void selectSeed( int32 offset );
        /** @brief 카메라 리그의 초점을 밭 중심과 농부 사이에 넣습니다(리그는 PostUpdate 에서 읽습니다). */
        void updateCameraFocus();
        void logStatus( bool bForce );

        const hashed_string& getSelectedSeed() const;
        /** @brief 핸들의 오브젝트 위치를 @p outPosition 에 담습니다. 대상을 찾지 못하면 false 이고 @p outPosition 은 그대로입니다. */
        [[nodiscard]] bool tryGetObjectPosition( GameObjectHandle handle, float3& outPosition ) const;
        /** @brief 도구를 한 번 쓸 때 드는 기력입니다(PROPERTY). */
        int32 getToolStaminaCost( FarmTool tool ) const;

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
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent that follows the farmer" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Shipping Bin", Tooltip = "Object whose position is the shipping bin (required - the farm does not start without it)" )
        GameObjectHandle _shippingBin;
        PROPERTY( Category = "Scene", DisplayName = "Shop", Tooltip = "Object whose position is the seed shop (required - the farm does not start without it)" )
        GameObjectHandle _shop;
        PROPERTY( Category = "Scene", DisplayName = "Farmer", Tooltip = "Object with the farmer pawn whose intent drives the farmer" )
        GameObjectHandle _farmer;
        PROPERTY( Category = "Prefabs", DisplayName = "Auto Farmer Prefab", AssetPath, AssetType = "Prefab", Tooltip = "AI controller that possesses the farmer while auto play is on" )
        string _autoFarmerPrefab;
        PROPERTY( Category = "Farmer", DisplayName = "Player Start", Tooltip = "Where the farmer wakes up", Units = m )
        float3 _playerStart;
        PROPERTY( Category = "Farmer", DisplayName = "Walk Speed", Min = 0.0, Units = "m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Farmer", DisplayName = "Reach", Tooltip = "Distance in front of the farmer that picks the faced tile", Min = 0.0, Units = m )
        float32 _reach;
        PROPERTY( Category = "Farmer", DisplayName = "Near Distance", Tooltip = "Distance that counts as standing next to the shipping bin or the shop", Min = 0.0, Units = m )
        float32 _nearDistance;
        PROPERTY( Category = "Weather", DisplayName = "Rain Chance", Tooltip = "Chance a new day outside winter is rainy", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _rainChance;
        PROPERTY( Category = "Camera", DisplayName = "Camera Follow", Tooltip = "How far the camera focus moves from the field centre towards the farmer", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _cameraFollow;
        PROPERTY( Category = "Farmer", DisplayName = "Max Stamina", Min = 1 )
        int32 _maxStamina;
        PROPERTY( Category = "Economy", DisplayName = "Starting Gold", Tooltip = "Gold a new game starts with", Min = 0 )
        int32 _startingGold;
        PROPERTY( Category = "Time", DisplayName = "Seconds Per Day", Tooltip = "Real seconds in one game day (1 s = 10 game minutes)", Min = 1.0, Units = s )
        float32 _secondsPerDay;
        PROPERTY( Category = "Time", DisplayName = "Days Per Season", Min = 1 )
        int32 _daysPerSeason;
        PROPERTY( Category = "Time", DisplayName = "Wake Hour", Tooltip = "Hour of day a new game and every morning after sleep start at", Min = 0.0, Max = 24.0 )
        float32 _wakeHour;
        PROPERTY( Category = "Time", DisplayName = "Pass Out Hour", Tooltip = "Hour past midnight at which a farmer still awake passes out", Min = 0.0, Max = 24.0 )
        float32 _passOutHour;
        PROPERTY( Category = "Farmer", DisplayName = "Hoe Stamina Cost", Min = 0 )
        int32 _hoeStaminaCost;
        PROPERTY( Category = "Farmer", DisplayName = "Watering Stamina Cost", Min = 0 )
        int32 _wateringStaminaCost;
        PROPERTY( Category = "Farmer", DisplayName = "Hand Stamina Cost", Tooltip = "Stamina for planting seeds or using bare hands", Min = 0 )
        int32 _handStaminaCost;
        PROPERTY( Category = "Economy", DisplayName = "Starting Seeds", Tooltip = "Seeds of the first crop a new game starts with", Min = 0 )
        int32 _startingSeedCount;
        PROPERTY( Category = "Economy", DisplayName = "Item Stack Size", Tooltip = "Most seeds or produce one bag slot holds", Min = 1 )
        int32 _itemStackSize;
        PROPERTY( Category = "Crop Models", DisplayName = "Crop Models", Tooltip = "Ready and growing models per crop id" )
        vector<FarmCropModel> _listCropModel;
        PROPERTY( Category = "Crop Models", DisplayName = "Default Growing Models", AssetPath,
                  Tooltip = "Growing models of crops without their own (the first for stages 0 and 1, then one per stage); required" )
        vector<string> _listDefaultGrowingModel;

        CropCatalog                  _cropCatalog;
        ItemCatalog                  _itemCatalog; ///< 작물 카탈로그가 채운 씨앗과 수확물(공유 가방의 겹침 수).
        FarmField                    _field;
        FarmShippingBin              _shipment;  ///< 출하함에 든 것(하루 끝에 팔린다).
        vector<hashed_string>        _listSeed;  ///< 카탈로그 순서의 씨앗 아이템(가게 진열).
        MaterialTintCache            _tintCache; ///< 흙과 작물 색(같은 색은 나눠 쓴다).
        vector<CropLook>             _listCropLook;
        vector<shared_ptr<Mesh>>     _listCropMesh; ///< 작물 모델을 계속 참조한다(단계가 바뀔 때 뷰가 워커에서 읽지 않게 한다).
        shared_ptr<MaterialInstance> _arrSoilLook[3];
        shared_ptr<MaterialInstance> _plainCropLook;
        shared_ptr<MaterialInstance> _witheredCropLook;
        float3                       _playerPosition;
        float3                       _facing;
        float3                       _shippingBinPosition;
        float3                       _shopPosition;
        GameRandom                   _random;           ///< 날씨
        GameObjectHandle             _autoFarmerObject; ///< 자동 농부 AI 조종자의 오브젝트(처음 켤 때 스폰한다).
        int32                        _stamina;
        int32                        _selectedSeedIndex;
        int32                        _lastLoggedHour;
        int32                        _dayStarted; ///< 농부의 하루가 시작된 공유 시계의 날(쓰러짐 판정에 쓴다).
        float32                      _hourOfDay;  ///< 뷰가 읽는 시각.
        FarmTool                     _tool;
        uint8                        _bRaining         : 1;
        uint8                        _bPossessionDirty : 1; ///< 농부 폰을 조종하는 조종자가 자동 플레이 스위치와 맞지 않는다(틱이 끝난 뒤 플러시가 바꾼다).
        uint8                        _reserved         : 6;
    };
} // namespace sw
