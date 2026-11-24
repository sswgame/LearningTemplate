/**
 * @file KartRaceViewComponent.h
 * @brief 카트 레이싱 키트의 클라이언트 쪽 — 입력 맵 액션을 운전 명령으로 바꿔 디렉터(`KartGameModeComponent`)에 내고, 트랙 · 차 · 아이템을 프리팹으로 보이며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_KartRacing`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitDriverInput`)과 읽기 API(`getRace` · `getTrack`)만 쓰고 경기(규칙)를 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 모습은 틱 뒤 게임 스레드에서 낸다(디렉터 규칙도 틱 뒤에 돈다).
 *          세운 모습은 상태에서 다시 세우는 파생 오브젝트라 저장하지 않는다(`PrefabViewBatch`). 자리는 절대 자리를 원점 기준으로 옮긴다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class KartGameModeComponent;
    class KartHUDViewModel;

    /**
     * @class KartRaceViewComponent
     * @brief `Move` 축의 앞뒤는 페달, 좌우는 조향이고, 드리프트 · 부스트 · 아이템 액션을 더해 한 프레임의 운전 입력으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "KartRacing", DisplayName = "Kart Race View", Tooltip = "Client side of a kart race: drives the player kart from input, shows the track, karts and items and fills the HUD" )
    class SW_KIT_CLIENT_API KartRaceViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        KartRaceViewComponent();
        virtual ~KartRaceViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 모습을 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager );
        /** @brief 이번 프레임의 운전 입력을 디렉터에 냅니다. */
        void submitInput( KartGameModeComponent& director ) const;
        /** @brief 트랙 조각 · 결승선을 보입니다. */
        void showTrack( GameObjectManager& manager, const KartGameModeComponent& director );
        /** @brief 차 · 아이템 상자 · 날아가는 아이템을 지금 자리에 보입니다. */
        void showRace( GameObjectManager& manager, const KartGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const KartGameModeComponent& director );
        /** @brief 세운 모습을 모두 지웁니다. */
        void destroyViews();

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action: up/down pedal, left/right steering (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Drift Action", Tooltip = "InputMap action held to drift (required)" )
        hashed_string _driftAction;
        PROPERTY( Category = "Input", DisplayName = "Boost Action", Tooltip = "InputMap action that fires a nitro (required)" )
        hashed_string _boostAction;
        PROPERTY( Category = "Input", DisplayName = "Item Action", Tooltip = "InputMap action that uses the held item (required)" )
        hashed_string _itemAction;
        PROPERTY( Category = "View", DisplayName = "Track Prefab", Required, Tooltip = "Unit piece stretched along the track and across the finish line" )
        AssetRef<PrefabAsset> _trackPrefab;
        PROPERTY( Category = "View", DisplayName = "Kart Prefab", Required )
        AssetRef<PrefabAsset> _kartPrefab;
        PROPERTY( Category = "View", DisplayName = "Item Prefab", Required, Tooltip = "Item boxes and flying items" )
        AssetRef<PrefabAsset> _itemPrefab;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Opponent Color" )
        float4 _opponentColor;
        PROPERTY( Category = "View", DisplayName = "Track Color" )
        float4 _trackColor;
        PROPERTY( Category = "View", DisplayName = "Finish Color" )
        float4 _finishColor;
        PROPERTY( Category = "View", DisplayName = "Item Color" )
        float4 _itemColor;

        TComponentHandle<KartGameModeComponent> _director;
        PrefabViewBatch                         _trackViews;
        PrefabViewBatch                         _kartViews;
        PrefabViewBatch                         _itemViews;
        unique_ptr<KartHUDViewModel>            _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        uint8                                   _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 읽지 않는다)
        uint8                                   _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                  _reserved      : 6;
    };
} // namespace sw
