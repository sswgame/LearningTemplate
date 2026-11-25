/**
 * @file FarmGameModeComponent.h
 * @brief 농장 생활 키트(`GF_Farming`)의 디렉터 — 농부 이동, 도구, 체력, 시간, 가게, 출하, 수면, 날씨, 자동 농부, 로그를 맡습니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트 — 밭 · 출하함 · 작물 카탈로그)을 들고 씬과 잇는다. 밭 타일의 모습 · 농부 몸 · 카메라 초점은 클라이언트 모듈(`GF_Client_Farming` 의
 *          `FarmFieldViewComponent` · `FarmSoilComponent` · `FarmCropComponent` · `FarmerComponent`)이 이 디렉터의 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          농부가 **무엇을 누르는가는 농부 폰의 의도**(`PawnComponent`. 플레이어 조종자 또는 `FarmAutoFarmerAIComponent` 가 낸다)이고 디렉터는 입력 맵을 읽지 않는다.
 *          해는 Runtime `DaylightComponent` 가 `readHourOfDay` 로 읽는다. 씬에 하나만 둔다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고 뷰는 `PostUpdate` 에서 읽는다.
 *          틱이 끝난 뒤의 스폰, 상태 바이트 보류, 정리, 효과음은 베이스 `GameModeComponent` 가 맡는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmShippingBin.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    class GameObjectManager;
    class PawnComponent;
} // namespace sw

namespace sw
{
    /** @brief 손에 든 도구입니다. */
    ENUM()
    enum class FarmTool : uint8
    {
        Hoe = 0,     ///< 갈기.
        WateringCan, ///< 물 주기.
        Seeds,       ///< 고른 씨앗 심기.
        Hand         ///< 거두기.
    };

    /** @brief 농부 폰의 버튼입니다. 폰 스키마(씬의 `PawnComponent`)의 이름은 `FarmGameModeComponent::getButtonName` 이고, 입력 맵 묶음(팩 `data/gamesettings.xml` 의 `<inputMap path>`)의 액션 이름과 같습니다. */
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
     * @class FarmGameModeComponent
     * @brief 농장 한 판을 진행합니다. 플레이가 시작되면 작물 카탈로그를 읽고 밭, 달력, 인벤토리를 새로 만듭니다(봄 1일, 첫 씨앗 10개).
     * @details 농장 상태(밭, 출하함, 농부)는 `serializeGameState` 와 상태 칸으로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드와 세이브를 넘긴다.
     *          **빙의**: 농부(씬의 `_farmer` 오브젝트의 폰)는 자동 빙의(`Player0`)로 플레이어 조종자가 조종하다가, 자동 플레이 스위치가 켜지면 틱이 끝난 뒤 플러시에서
     *          자동 농부 AI(`_autoFarmerPrefab`)가, 꺼지면 다시 플레이어 조종자가 조종한다. 디렉터 안에 자동 플레이 분기는 없다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Game Mode", Tooltip = "Runs the farm rules, the farmer, time and weather" )
    class SW_KIT_API FarmGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kBagSlotCount = 24; ///< 플레이어 가방 슬롯 수(공유 상태).
        static constexpr int32 kButtonCount  = static_cast<int32>( FarmButton::Count );

        FarmGameModeComponent();
        virtual ~FarmGameModeComponent() override;

        /** @brief 농장 상태(밭, 출하함 구간, 농부, 날씨)를 씁니다. 달력은 월드 상태(`GameStateComponent`), 가방 · 돈은 플레이어 상태(`InventoryComponent` · `WalletComponent`)가 싣습니다. `ComponentStateCodec::captureObject` 가 부릅니다. */
        void serializeGameState( StateIO& io ) override;

        // ---- 뷰가 읽는 것(PostUpdate. 디렉터가 쓰지 않는 그룹) ----
        /** @brief 공유 시계의 시각(0~24)입니다. 디렉터가 틱마다 옮겨 적으며, 뷰는 PostUpdate 에서 이것만 읽습니다. */
        float32          getHourOfDay() const { return _hourOfDay; }
        const FarmField& getField() const { return _field; }
        /** @brief 농부 자리입니다(절대 — 뷰 · AI 는 `WorldOrigin::toLocal` 로 옮겨 쓴다). */
        AbsolutePosition getPlayerPosition() const { return AbsolutePosition{ _playerPosition }; }
        bool             isRaining() const { return _bRaining == SW_TRUE; }
        /** @brief 지난 틱에 농부가 걸었으면(이동 의도가 있었으면) true 입니다. 농부는 물리 없이 위치를 옮겨 `Object.Speed` 로는 볼 수 없다. */
        bool               isFarmerWalking() const { return _bFarmerWalking == SW_TRUE; }
        [[nodiscard]] bool readHourOfDay( float32& outHourOfDay ) const override
        {
            outHourOfDay = _hourOfDay;
            return true;
        }

        // ---- 자동 농부(조종자)가 읽는 것. 틱 밖(조종 시스템의 판단)에서 읽는다 ----
        /** @brief 농부가 바라보는 방향(네 방향 단위 벡터)입니다. */
        const float3& getFacing() const { return _facing; }
        FarmTool      getTool() const { return _tool; }
        /** @brief 끝낸(밭이 바뀐) 도구 사용 수입니다. 오를 때마다 농부 몸이 그 도구의 동작을 한 번 한다(`FarmerComponent`). 저장하지 않는 화면용 값이다. */
        uint32 getToolUseCount() const { return _toolUseCount; }
        /** @brief 마지막으로 끝낸 도구 사용의 도구입니다. */
        FarmTool                     getLastUsedTool() const { return _lastUsedTool; }
        int32                        getStamina() const { return _stamina; }
        int32                        getDayStarted() const { return _dayStarted; }
        int32                        getSelectedSeedIndex() const { return _selectedSeedIndex; }
        const vector<hashed_string>& getSeeds() const { return _listSeed; }
        const CropCatalog&           getCropCatalog() const { return *_pCropCatalog; }
        const hashed_string&         getCurrency() const { return _shipment.getCurrency(); }
        AbsolutePosition             getShippingBinPosition() const { return AbsolutePosition{ _shippingBinPosition }; }
        AbsolutePosition             getShopPosition() const { return AbsolutePosition{ _shopPosition }; }
        bool                         isNearShippingBin() const;
        bool                         isNearShop() const;
        /** @brief 농부 폰을 지금 자동 농부 AI 가 조종하고 있으면 true 를 반환합니다(시나리오 프로브). */
        bool isFarmerDrivenByAI() const;
        /** @brief 폰 버튼의 이름을 반환합니다(입력 맵 액션 이름과 같습니다). */
        static const utf8* getButtonName( FarmButton button );

        /** @brief 농부가 바라보는 타일을 반환합니다. 밭 밖이면 false 입니다. */
        [[nodiscard]] bool findTargetTile( int32& outX, int32& outY ) const;
        /** @brief 타일 중심(땅 높이)을 반환합니다. 밭은 원점에서 +X, +Z 방향으로 1m 타일입니다. */
        static AbsolutePosition computeTileCenter( int32 x, int32 y );

    protected:
        /** @brief 작물 카탈로그를 읽고 밭과 출하함을 새로 만들며, 같은 오브젝트의 판 상태와 플레이어 상태(조종자)의 가방 · 지갑을 엽니다(새 판일 때만 시작 돈과 씨앗을 줍니다). */
        [[nodiscard]] bool startGame() override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bPossessionDirty == SW_TRUE; }

    private:
        /** @brief 지금 작물 카탈로그로 가방의 아이템 목록과 가게 씨앗 목록을 채웁니다(시작 · 다시 읽힌 카탈로그). */
        void applyCropCatalog();
        /** @brief 농부 폰의 이번 틱 의도를 처리합니다. 이동 축으로 걷고, 발동한 버튼(도구, 씨앗, 사용, 출하, 구매, 수면, 상태)을 규칙대로 실행합니다. */
        void applyFarmerIntent( float32 deltaTime, const PawnComponent& pawn );
        /** @brief 농부를 @p direction 방향으로 움직이고 바라보는 방향을 네 방향으로 맞춥니다. 벡터 길이 1 이 걷는 속도의 최대치이며, 길이가 곧 속도 비율입니다. */
        /** @brief 농부를 @p direction 으로 한 걸음 옮깁니다. 씬의 정적 바디에 막히면 미끄러지고(`WalkSweep`) 걷는 여백 밖으로 나가지 않는다. */
        void movePlayer( const float3& direction, float32 deltaTime );
        /** @brief 농부 폰을 반환합니다(`_farmer` 오브젝트). 없으면 nullptr 입니다. */
        const PawnComponent* findFarmerPawn() const;
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
        void logFarmStatus( bool bForce );

        const hashed_string& getSelectedSeed() const;
        /** @brief 핸들의 오브젝트 위치를 @p outPosition 에 담습니다. 대상을 찾지 못하면 false 이고 @p outPosition 은 그대로입니다. */
        [[nodiscard]] bool tryGetObjectPosition( GameObjectHandle handle, float3& outPosition ) const;
        /** @brief 도구를 한 번 쓸 때 드는 기력입니다(PROPERTY). */
        int32 getToolStaminaCost( FarmTool tool ) const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Crop Data", Tooltip = "Crop catalog XML" )
        AssetRef<CropCatalog> _cropDataPath;
        PROPERTY( Category = "Field", DisplayName = "Field Width", Tooltip = "Field tiles along X", Min = 1 )
        int32 _fieldWidth;
        PROPERTY( Category = "Field", DisplayName = "Field Height", Tooltip = "Field tiles along Z", Min = 1 )
        int32 _fieldHeight;
        PROPERTY( Category = "Scene", DisplayName = "Shipping Bin", Tooltip = "Object whose position is the shipping bin (required - the farm does not start without it)" )
        GameObjectHandle _shippingBin;
        PROPERTY( Category = "Scene", DisplayName = "Shop", Tooltip = "Object whose position is the seed shop (required - the farm does not start without it)" )
        GameObjectHandle _shop;
        PROPERTY( Category = "Scene", DisplayName = "Farmer", Tooltip = "Object with the farmer pawn whose intent drives the farmer" )
        GameObjectHandle _farmer;
        PROPERTY( Category = "Prefabs", DisplayName = "Auto Farmer Prefab", Tooltip = "AI controller that possesses the farmer while auto play is on" )
        AssetRef<PrefabAsset> _autoFarmerPrefab;
        PROPERTY( Category = "Farmer", DisplayName = "Player Start", Tooltip = "Where the farmer wakes up", Units = m )
        float3 _playerStart;
        PROPERTY( Category = "Farmer", DisplayName = "Walk Speed", Min = 0.0, Units = "m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Farmer", DisplayName = "Body Radius", Tooltip = "Radius of the walking body that static bodies (house, shop, fences, trees) stop", Min = 0.05, Units = m )
        float32 _bodyRadius;
        PROPERTY( Category = "Farmer", DisplayName = "Reach", Tooltip = "Distance in front of the farmer that picks the faced tile", Min = 0.0, Units = m )
        float32 _reach;
        PROPERTY( Category = "Farmer", DisplayName = "Near Distance", Tooltip = "Distance that counts as standing next to the shipping bin or the shop", Min = 0.0, Units = m )
        float32 _nearDistance;
        PROPERTY( Category = "Farmer", DisplayName = "Walk Margin Min", Tooltip = "How far the farmer may walk past the field on the low X and Z sides", Min = 0.0, Units = m )
        float32 _walkMarginMin;
        PROPERTY( Category = "Farmer", DisplayName = "Walk Margin Max", Tooltip = "How far the farmer may walk past the field on the high X and Z sides", Min = 0.0, Units = m )
        float32 _walkMarginMax;
        /** @brief 판 상태 날씨의 카탈로그입니다(`GameStateComponent` 의 `WeatherSystem` — 계절 가중치 · 하루 길이). 비는 그 날씨의 `wetness` 로 정한다. */
        PROPERTY( Category = "Weather", DisplayName = "Weather", Tooltip = "Weather catalog of the shared session state (season weights, a day long); rain is its wetness" )
        AssetRef<WeatherCatalog> _weatherPath;
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

        BoundAssetRef<CropCatalog> _pCropCatalog;
        ItemCatalog                _itemCatalog; ///< 작물 카탈로그가 채운 씨앗과 수확물(공유 가방의 겹침 수).
        FarmField                  _field;
        FarmShippingBin            _shipment; ///< 출하함에 든 것(하루 끝에 팔린다).
        vector<hashed_string>      _listSeed; ///< 카탈로그 순서의 씨앗 아이템(가게 진열).
        PROPERTY( RuntimeState )
        float3 _playerPosition;
        PROPERTY( RuntimeState )
        float3           _facing;
        float3           _shippingBinPosition;
        float3           _shopPosition;
        GameObjectHandle _autoFarmerObject; ///< 자동 농부 AI 조종자의 오브젝트(처음 켤 때 스폰한다).
        PROPERTY( RuntimeState )
        int32 _stamina;
        PROPERTY( RuntimeState )
        int32 _selectedSeedIndex;
        int32 _lastLoggedHour;
        PROPERTY( RuntimeState )
        int32   _dayStarted; ///< 농부의 하루가 시작된 공유 시계의 날(쓰러짐 판정에 쓴다).
        float32 _hourOfDay;  ///< 뷰가 읽는 시각.
        PROPERTY( RuntimeState )
        FarmTool               _tool;
        FarmTool               _lastUsedTool;
        uint32                 _toolUseCount;
        uint8                  _bRaining         : 1; ///< 판 상태 날씨의 비를 디렉터가 틱마다 옮겨 적은 것(뷰가 읽는다 — 저장하지 않는다, 날씨는 판 상태가 싣는다)
        uint8                  _bPossessionDirty : 1; ///< 농부 폰을 조종하는 조종자가 자동 플레이 스위치와 맞지 않는다(틱이 끝난 뒤 플러시가 바꾼다).
        uint8                  _bFarmerWalking   : 1; ///< 지난 틱에 이동 의도가 있었다(`isFarmerWalking` — 저장하지 않는다)
        [[maybe_unused]] uint8 _reserved         : 5;
    };
} // namespace sw
