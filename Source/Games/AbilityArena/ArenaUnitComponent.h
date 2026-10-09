/**
 * @file ArenaUnitComponent.h
 * @brief 아레나 유닛 하나의 폰 쪽 — 의도(`ControlIntent`)로 움직이고 어빌리티 입력 번호를 누르며, 쓰러지면 납작해집니다. 조종자(플레이어 · AI)는 따로 산다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/Pawn/CharacterPawnMovementComponent.h"

namespace sw
{
    /** @brief 아레나 유닛의 종류입니다 — 어빌리티 세트 · 모양 · 조종자(AI 프리팹)를 고릅니다. */
    ENUM()
    enum class ArenaUnitKind : uint8
    {
        Player = 0, ///< 플레이어 조종자(또는 자동 전투 AI)가 쥔다 — 세트 "Player"
        Grunt,      ///< 다가와 때린다, 맞으면 가시로 되갚는다 — 세트 "Grunt"
        Caster      ///< 거리를 두고 화염탄을 쏜다 — 세트 "Caster"
    };
} // namespace sw

namespace sw
{
    class AbilitySystemComponent;
    class ArenaDirectorComponent;
    class MeshComponent;
    class PawnComponent;

    /**
     * @class ArenaUnitComponent
     * @brief 유닛 오브젝트(메시 · 어빌리티 시스템 · HP 바 · 폰과 같은 오브젝트)에 붙어 그 유닛만 움직입니다. **의도만 읽습니다** — 입력 맵도, 판단도 없다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돕니다 — 디렉터(`PrePhysics`)가 이번 프레임의 유닛 모습을 다 적은 뒤이고, 의도는 조종 시스템이 틱 전에 채웠다.
     *          한 틱의 차례: (1) 바라보는 쪽 — `_facingMode` 가 `MoveDirection` 이면 움직이는 쪽, `ControlYaw` 면 조종 요(AI 가 초점으로 돌린다)
     *          (2) 폰 버튼(스키마 순서 = `kArrAbilityButton`)의 발동 · 뗌을 어빌리티 입력 번호로 — 같은 오브젝트의 어빌리티 시스템과 같은 워커라 그 자리에서 발동한다
     *          (3) 이동 — 이동 속도는 `MoveSpeed` current, 대시 중(`State.Dashing`)에는 의도와 상관없이 바라보는 쪽으로 (4) 적끼리 겹치지 않게 밀기 (5) 아레나 안으로.
     *          자기 오브젝트에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다. 어빌리티가 디렉터를 찾는 길도 이 핸들입니다(`ArenaDirectorComponent::findForUnit`).
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Unit", Tooltip = "Pawn side of an arena unit: moves and presses ability inputs from the control intent" )
    class ArenaUnitComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 폰 버튼 이름 → 어빌리티 입력 번호입니다(abilities.xml 의 세트와 같다). */
        struct AbilityButton
        {
            const utf8* _pName;
            int32       _inputId;
        };
        static constexpr AbilityButton kArrAbilityButton[] = {
            {   "Arena.Melee", 1},
            {"Arena.Fireball", 2},
            {    "Arena.Heal", 3},
            {    "Arena.Dash", 4},
        };
        static constexpr int32 kAbilityButtonCount = static_cast<int32>( sizeof( kArrAbilityButton ) / sizeof( kArrAbilityButton[0] ) );

        ArenaUnitComponent();
        virtual ~ArenaUnitComponent() override = default;

        /** @brief 폰 버튼 이름의 자리를 다시 풀게 합니다(폰 스키마는 시작 전에 정해진다). */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따를 디렉터와 처음 바라볼 쪽을 정합니다(디렉터가 스폰한 뒤 부른다). */
        void             assignDirector( GameObjectHandle director, const float3& facing );
        GameObjectHandle getDirector() const { return _director; }
        ArenaUnitKind    getKind() const { return _kind; }
        /** @brief 바라보는 방향(XZ 평면의 단위 벡터)입니다. 어빌리티가 투사체 방향으로 쓴다(같은 오브젝트의 워커에서). */
        const float3& getFacing() const { return _facing; }

        /** @brief XZ 평면 성분만 남긴 단위 벡터입니다. 길이가 거의 0 이면 @p fallback 입니다. */
        static float3 flattenDirection( const float3& direction, const float3& fallback );

    private:
        /** @brief 폰 버튼의 발동 · 뗌을 어빌리티 입력 번호의 눌림 · 뗌으로 넘깁니다. */
        void pressAbilityButtons( const PawnComponent& pawn, AbilitySystemComponent& abilitySystem );
        /** @brief 다른 적과 겹친 만큼의 절반을 자기 쪽으로 밀어냅니다(상대도 같은 일을 한다 — 물리 없이, 유닛 수가 적다). */
        void separateFromEnemies( const ArenaDirectorComponent& director, float3& inoutPosition ) const;
        /** @brief 쓰러진 뒤 — 납작해지다가 디렉터가 걷는다. */
        void tickDeath( float32 deltaTime, MeshComponent& mesh );

    private:
        PROPERTY( Category = "Arena", DisplayName = "Director", Tooltip = "Object with the ArenaDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Arena", DisplayName = "Kind", Tooltip = "Player units are not pushed apart; enemies are" )
        ArenaUnitKind _kind;
        PROPERTY( Category = "Arena", DisplayName = "Facing", Tooltip = "Face the move direction (player) or the control yaw the AI turns to its focus (enemies)" )
        PawnFacingMode _facingMode;
        PROPERTY( Category = "Arena", DisplayName = "Unit Radius", Tooltip = "Body radius used to push overlapping enemies apart", Min = 0.0, Units = m )
        float32 _unitRadius;

        float3                 _facing;                              ///< XZ 단위 벡터
        float3                 _deathScale;                          ///< 쓰러질 때의 크기(납작해지기 시작점)
        float32                _deathTimer;                          ///< 0 이상이면 쓰러진 뒤 걷기까지 남은 시간
        int32                  _arrButtonIndex[kAbilityButtonCount]; ///< `kArrAbilityButton` 마다 폰 버튼의 자리(없으면 −1)
        uint32                 _previousButtonDown;                  ///< 지난 틱에 누르고 있던 폰 버튼(뗌을 알아챈다)
        uint8                  _bButtonsResolved : 1;
        [[maybe_unused]] uint8 _reserved         : 7;
    };
} // namespace sw
