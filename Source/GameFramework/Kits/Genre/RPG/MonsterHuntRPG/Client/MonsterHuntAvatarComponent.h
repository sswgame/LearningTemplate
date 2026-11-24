/**
 * @file MonsterHuntAvatarComponent.h
 * @brief 몬스터 사냥 RPG 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`MonsterHuntGameModeComponent`)의 명령으로 넘기고, 단서 · 사냥꾼 · 괴물을 프리팹 인스턴스(`PrefabViewBatch`)로 그리며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_MonsterHuntRPG`, 전용 서버는 짓지 않는다). 디렉터의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 는 같은 오브젝트의 HUD 컨트롤러(`HUDControllerComponent`)다.
 *          흥정 중에는 빠른 공격 액션이 제안을 받고 강한 공격 액션이 보상을 올린다. 명령 · 그리기 · HUD 는 틱 뒤 게임 스레드에서 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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
    struct MonsterHuntClueDef;

    class GameInputView;
    class GameObjectManager;
    class MonsterHuntGameModeComponent;
    class MonsterHuntHUDViewModel;

    /**
     * @class MonsterHuntAvatarComponent
     * @brief 흥정 · 걷기 · 조사 · 공격 · 회피 · 표식 · 오일 · 물약 액션을 디렉터 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "MonsterHuntRPG", DisplayName = "Monster Hunt Avatar",
             Tooltip = "Client side of a MonsterHuntRPG contract: sends commands to the Monster Hunt Game Mode on the same object, draws clues and the monster and fills the HUD" )
    class SW_KIT_CLIENT_API MonsterHuntAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        MonsterHuntAvatarComponent();
        virtual ~MonsterHuntAvatarComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager );
        void submitInput( MonsterHuntGameModeComponent& director, const GameInputView& input ) const;
        void updateHUD( const MonsterHuntGameModeComponent& director, bool bClueNearby );
        /** @brief 감각 안의 단서(@p listClue) · 사냥꾼 · 괴물을 그립니다. */
        void draw( const MonsterHuntGameModeComponent& director, const vector<const MonsterHuntClueDef*>& listClue, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the hunter (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Fast Action", Tooltip = "InputMap action of the fast attack; accepts the offer while haggling (required)" )
        hashed_string _fastAction;
        PROPERTY( Category = "Input", DisplayName = "Strong Action", Tooltip = "InputMap action of the strong attack; asks for more while haggling (required)" )
        hashed_string _strongAction;
        PROPERTY( Category = "Input", DisplayName = "Dodge Action", Tooltip = "InputMap action that dodges (required)" )
        hashed_string _dodgeAction;
        PROPERTY( Category = "Input", DisplayName = "Sign Action", Tooltip = "InputMap action that casts the sign (required)" )
        hashed_string _signAction;
        PROPERTY( Category = "Input", DisplayName = "Oil Action", Tooltip = "InputMap action that oils the blade (required)" )
        hashed_string _oilAction;
        PROPERTY( Category = "Input", DisplayName = "Potion Action", Tooltip = "InputMap action that drinks the potion (required)" )
        hashed_string _potionAction;
        PROPERTY( Category = "Input", DisplayName = "Investigate Action", Tooltip = "InputMap action that investigates the nearest open clue (required)" )
        hashed_string _investigateAction;
        PROPERTY( Category = "Input", DisplayName = "Alternate Sign Action", Tooltip = "InputMap action that casts the alternate sign (required)" )
        hashed_string _alternateSignAction;
        PROPERTY( Category = "Input", DisplayName = "Learn Action", Tooltip = "InputMap action that learns the next skill (required)" )
        hashed_string _learnAction;
        PROPERTY( Category = "Input", DisplayName = "Brew Action", Tooltip = "InputMap action that brews from the bag (required)" )
        hashed_string _brewAction;
        PROPERTY( Category = "Input", DisplayName = "Mutagen Action", Tooltip = "InputMap action that slots the next mutagen (required)" )
        hashed_string _mutagenAction;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        TComponentHandle<MonsterHuntGameModeComponent> _director;
        PrefabViewBatch                                _bodies;
        PrefabViewBatch                                _markers;
        unique_ptr<MonsterHuntHUDViewModel>            _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        uint8                                          _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 읽지 않고 그리기만 한다)
        uint8                                          _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                         _reserved      : 6;
    };
} // namespace sw
