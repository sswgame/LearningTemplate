/**
 * @file ScavengerAvatarComponent.h
 * @brief 협동 수집 공포 키트의 클라이언트 쪽 — 로컬 사용자마다 입력을 같은 번호의 승무원 명령으로 바꾸고, 위성의 방 · 고철 · 승무원 · 위협을 원시 도형으로 그리며 HUD 를 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CoopScavenger`, 전용 서버는 짓지 않는다). 디렉터(`ScavengerGameModeComponent`)의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 그리기는 틱 뒤 게임 스레드에서 한다.
 *          `PostUpdate` 라 넘긴 입력은 다음 걸음이 쓰고 그리기는 이번 걸음의 결과를 보인다. 모습은 원시 도형 표식(`PrefabViewBatch`)이라 저장에 싣지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ScavengerGameModeComponent;
    class ScavengerHUDViewModel;
    struct ScavengerCommand;
    class GameObjectManager;

    /**
     * @class ScavengerAvatarComponent
     * @brief 입력 맵 액션을 프레임마다 디렉터 명령으로 넘기고 판을 그리며 HUD 를 채웁니다.
     */
    REFLECT( Category = "CoopScavenger", DisplayName = "Scavenger Avatar",
             Tooltip = "Client side of a coop scavenger expedition: sends commands to the Scavenger Game Mode on the same object, draws the game and fills the HUD" )
    class SW_KIT_CLIENT_API ScavengerAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ScavengerAvatarComponent();
        virtual ~ScavengerAvatarComponent() override;

        /** @brief 세운 표식을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 판을 그리며 HUD 를 채웁니다. */
        void updateAvatar( GameObjectManager& manager, ScavengerGameModeComponent& director );
        /** @brief 입력 맵을 읽어 디렉터 명령으로 넘깁니다. */
        void submitInput( const GameModeInput& input, ScavengerGameModeComponent& director );
        /** @brief 궤도의 터미널 — 걷기 축의 좌우로 물건을 고르고 쓰기로 고른 물건의 id 를 @p outCommand 에 적습니다. 사용자 0 만 쓴다. */
        void updateTerminal( const ScavengerGameModeComponent& director, float32 axis, bool bBuy, ScavengerCommand& outCommand );
        /** @brief 판을 표식으로 그립니다. */
        void drawGame( GameObjectManager& manager, const ScavengerGameModeComponent& director );
        /** @brief HUD 값을 채웁니다. */
        void fillHUD( ScavengerHUDViewModel& outViewModel, const ScavengerGameModeComponent& director ) const;

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that steps to the neighbouring area (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action: pick up, load, sell or unlock (required)" )
        hashed_string _useAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Lever Action", Tooltip = "InputMap action: land from orbit, take off from the ship (required)" )
        hashed_string _leverAction;
        PROPERTY( Required, Category = "Look", DisplayName = "Marker Prefab", Tooltip = "Prefab with one mesh, spawned for everything the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch      _markers;
        unique_ptr<ScavengerHUDViewModel> _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        uint32                            _terminalIndex;     ///< 터미널에서 고른 진열 칸(화면의 상태라 싣지 않는다)
        uint8                             _bTerminalAxis : 1; ///< 고르는 축이 눌려 있다(눌린 동안 한 칸만 옮긴다)
        [[maybe_unused]] uint8            _reserved      : 7;
    };
} // namespace sw
