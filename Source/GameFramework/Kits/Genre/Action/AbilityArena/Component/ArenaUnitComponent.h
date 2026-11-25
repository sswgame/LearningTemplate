/**
 * @file ArenaUnitComponent.h
 * @brief 아레나 유닛 하나의 폰 쪽 — 의도(`ControlIntent`)로 움직이고 어빌리티 입력 번호를 누릅니다. 조종자(플레이어 · AI)는 따로 산다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaUnitView.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameFrameworkComponent.h"

namespace sw
{
    class AbilitySystemComponent;
    class ArenaGameModeComponent;
    class PawnComponent;

    /** @brief 폰 버튼 하나가 누르는 어빌리티 입력 번호입니다(유닛 프리팹의 데이터 — 번호는 abilities.xml 의 세트가 어빌리티에 준 `input` 이다). */
    REFLECT()
    struct ArenaAbilityButton
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Pawn button name (the pawn schema's button)" )
        hashed_string _button{};
        PROPERTY( Required, Min = 1, Tooltip = "Ability input id the button presses (abilities.xml)" )
        int32 _inputID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ArenaUnitComponent
     * @brief 유닛 오브젝트(메시 · 어빌리티 시스템 · HP 바 · 폰과 같은 오브젝트)에 붙어 그 유닛만 움직입니다. **의도만 읽습니다** — 입력 맵도, 판단도 없다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돕니다 — 디렉터(`PrePhysics`)가 이번 프레임의 유닛 모습을 다 적은 뒤이고, 의도는 조종 시스템이 틱 전에 채웠다.
     *          한 틱의 차례: (1) 바라보는 쪽 — `_facingMode` 가 `MoveDirection` 이면 움직이는 쪽, `ControlYaw` 면 조종 요(AI 가 초점으로 돌린다)
     *          (2) 폰 버튼(`_listAbilityButton` — 프리팹 데이터)의 발동 · 해제을 어빌리티 입력 번호로 — 같은 오브젝트의 어빌리티 시스템과 같은 워커라 그 자리에서 발동한다
     *          (3) 이동 — 이동 속도는 `MoveSpeed` current, 대시 중(`State.Dashing`)에는 의도와 상관없이 바라보는 쪽으로 (4) 적끼리 겹치지 않게 밀기 (5) 아레나 안으로.
     *          자기 오브젝트에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다. 어빌리티가 디렉터를 찾는 길도 이 핸들입니다(`ArenaGameModeComponent::findForUnit`).
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Unit", Tooltip = "Pawn side of an arena unit: moves and presses ability inputs from the control intent" )
    class SW_KIT_API ArenaUnitComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        ArenaUnitComponent();
        virtual ~ArenaUnitComponent() override = default;

        /** @brief 폰 버튼 이름의 인덱스를 다시 찾게 합니다(폰 스키마는 시작 전에 정해진다). */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 풀 반환: 쓰러짐 시계 · 지난 버튼을 비웁니다. 디렉터 · 바라보는 쪽은 `assignGameMode` 가 정합니다. */
        [[nodiscard]] SpawnStateReset resetToSpawnState() override;

        /** @brief 따를 디렉터와 처음 바라볼 쪽을 정합니다(디렉터가 스폰한 뒤 부른다). */
        void          assignGameMode( GameObjectHandle gameMode, const float3& facing );
        ArenaUnitKind getKind() const { return _kind; }
        /** @brief 바라보는 방향(XZ 평면의 단위 벡터)입니다. 어빌리티가 투사체 방향으로 쓴다(같은 오브젝트의 워커에서). */
        const float3& getFacing() const { return _facing; }
        /** @brief 쓰러진 뒤 걷기까지 남은 비율입니다. 서 있으면 1, 걷을 때 0 입니다. 쓰러짐 연출(`ArenaDeathSquashComponent`)이 같은 오브젝트의 틱에서 읽습니다. */
        float32 getDeathRemainRatio() const { return _deathRemainRatio; }

        /** @brief XZ 평면 성분만 남긴 단위 벡터입니다. 길이가 거의 0 이면 @p fallback 입니다. */
        static float3 flattenDirection( const float3& direction, const float3& fallback );

    private:
        /** @brief 폰 버튼의 발동 · 뗌을 어빌리티 입력 번호의 눌림 · 뗌으로 넘깁니다. */
        void pressAbilityButtons( const PawnComponent& pawn, AbilitySystemComponent& abilitySystem );
        /** @brief 다른 적과 겹친 만큼의 절반을 자기 쪽으로 밀어냅니다(상대도 같은 일을 한다 — 물리 없이, 유닛 수가 적다). */
        void separateFromEnemies( const ArenaGameModeComponent& gameMode, float3& inoutPosition ) const;

    private:
        PROPERTY( Category = "Arena", DisplayName = "Kind", Tooltip = "Player units are not pushed apart; enemies are" )
        ArenaUnitKind _kind;
        PROPERTY( Category = "Arena", DisplayName = "Facing", Tooltip = "Face the move direction (player) or the control yaw the AI turns to its focus (enemies)" )
        PawnFacingMode _facingMode;
        PROPERTY( Category = "Arena", DisplayName = "Unit Radius", Tooltip = "Body radius used to push overlapping enemies apart", Min = 0.0, Units = m )
        float32 _unitRadius;
        PROPERTY( Category = "Arena", DisplayName = "Ability Buttons", Tooltip = "Pawn buttons and the ability input each presses; a unit without rows presses no ability" )
        vector<ArenaAbilityButton> _listAbilityButton;

        float3                  _facing;             ///< XZ 단위 벡터
        float32                 _deathRemainRatio;   ///< 쓰러진 뒤 걷기까지 남은 비율(서 있으면 1 — 디렉터의 Death Linger 동안 0 으로 준다)
        vector<optional<int32>> _listButtonIndex;    ///< `_listAbilityButton` 마다 폰 버튼의 인덱스(없으면 비어 있다)
        uint32                  _previousButtonDown; ///< 지난 틱에 누르고 있던 폰 버튼(뗌을 알아챈다)
        uint8                   _bButtonsResolved : 1;
        [[maybe_unused]] uint8  _reserved         : 7;
    };
} // namespace sw
