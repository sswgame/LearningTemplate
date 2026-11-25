/**
 * @file FarmPlotGameModeComponent.h
 * @brief 농장 생활 키트(`GF_Farming`)의 스스로 가꾸는 밭 디렉터 — 월드 상태(`GameStateComponent`)를 여는 첫 디렉터. 하루가 넘어가면 밭이 자라고, 다 자란 칸을 거둬 플레이어 지갑에 팔고 다시 심습니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트의 `FarmField`)을 들고 씬과 잇는다. 빨리 감기 입력과 밭 칸 모습은 클라이언트 모듈
 *          (`GF_Client_Farming` 의 `FarmPlotViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          월드 상태의 설정(시계 · 계절 · 공유 땅)은 이 디렉터의 씬 데이터 하나다 — 뒤에 붙은 다른 키트의 디렉터(마을 · 집 부지)가 같은 판을 쓴다. 농부가 손으로 짓는 농장은 `FarmGameModeComponent` 다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmField.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameSpeedControl.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    class WalletComponent;

    /**
     * @class FarmPlotGameModeComponent
     * @brief 밭 한 판입니다. 같은 오브젝트의 맨 앞 `GameStateComponent` 를 열고(새 판이면 시작 돈), 공유 시계의 하루 넘김에 밭을 돌립니다.
     * @details 상태는 밭 하나(구간 'FFLD')이고, 돈 · 시계 · 수확 수(`_harvestedFlag`)는 공유 상태에 있다. 빨리 감기 명령(`submitFastForward`)을 받은 동안, 또는 자동 플레이면 시계가
     *          `_fastTimeScale` 배로 흐른다. 시계를 미는 것은 이 디렉터 하나다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Plot Game Mode", Tooltip = "Runs a self-tending field on the shared game state: grows, harvests, sells to the player's wallet and replants every day" )
    class SW_KIT_API FarmPlotGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr FormatDef kStateFormat{ "MVFM" };

        FarmPlotGameModeComponent();
        virtual ~FarmPlotGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /** @brief 빨리 감기를 누르고 있는지 넘깁니다. 다음 걸음이 씁니다. 판이 열리지 않았으면 받지 않고 false 입니다. */
        [[nodiscard]] bool submitFastForward( bool bHeld );

        // ---- 읽기(클라이언트 모듈) ----
        const FarmField& getField() const { return _field; }
        int32            getFieldWidth() const { return _fieldWidth; }
        int32            getFieldHeight() const { return _fieldHeight; }
        /** @brief 밭이 바뀔 때마다 오르는 번호입니다(열린 판은 1 부터) — 뷰가 견주어 바뀐 때만 맞춥니다. */
        uint32 getFieldRevision() const { return _fieldRevision; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 하루 넘김 — 밭이 자라고, 다 자란 칸을 거둬 팔고 다시 심고, 나머지는 물 준다. */
        void startNextDay( GameStateComponent& state, WalletComponent& wallet );
        void replant( int32 x, int32 y, const hashed_string& season );

    private:
        PROPERTY( Category = "Data", DisplayName = "Crop Data", Tooltip = "Crop catalog XML" )
        AssetRef<CropCatalog> _cropDataPath;
        PROPERTY( Required, Category = "Data", DisplayName = "Crop", Tooltip = "Crop id planted on every field tile" )
        CatalogKey<CropDef> _crop;
        PROPERTY( Required, Category = "Data", DisplayName = "Harvested Flag", Tooltip = "Shared flag that counts what the field harvested" )
        hashed_string _harvestedFlag;
        PROPERTY( Category = "World", DisplayName = "State Settings", Tooltip = "Settings the shared state opens with (clock, seasons, land); the land cell size must match the cell size of the housing part catalog" )
        GameStateSettings _stateSettings;
        PROPERTY( Category = "Field", DisplayName = "Field Width", Min = 1, Tooltip = "Tiles along X" )
        int32 _fieldWidth;
        PROPERTY( Category = "Field", DisplayName = "Field Height", Min = 1, Tooltip = "Tiles along Z" )
        int32 _fieldHeight;
        PROPERTY( Category = "Economy", DisplayName = "Starting Gold", Min = 0 )
        int32 _startingGold;
        PROPERTY( Category = "Time", DisplayName = "Fast Time Scale", Tooltip = "Clock speed while fast forward is held (up to the game speed limit)", Min = 1.0, Max = 8.0 )
        float32 _fastTimeScale;

        SharedAssetRef<CropCatalog> _pCropCatalog;
        FarmField                   _field;
        GameSpeedControl            _speed; ///< 배속(빨리 감기를 누르는 동안 `_fastTimeScale`) — 공유 시계와 시뮬레이션이 같은 값을 받는다
        uint32                      _fieldRevision;
        uint8                       _bFastForward : 1; ///< 마지막 빨리 감기 명령 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        [[maybe_unused]] uint8      _reserved     : 7;
    };
} // namespace sw
