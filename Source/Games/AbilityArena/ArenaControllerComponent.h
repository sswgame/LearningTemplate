/**
 * @file ArenaControllerComponent.h
 * @brief 아레나 유닛 하나를 움직이는 컴포넌트의 공통 부분 — 디렉터 핸들 · 바라보는 쪽 · 쓰러짐 연출 · 이동입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 아레나 유닛의 종류입니다 — 어빌리티 세트 · 모양 · AI 를 고릅니다. */
    ENUM()
    enum class ArenaUnitKind : uint8
    {
        Player = 0, ///< 입력(또는 자동 전투)으로 움직인다 — 세트 "Player"
        Grunt,      ///< 다가와 때린다, 맞으면 가시로 되갚는다 — 세트 "Grunt"
        Caster      ///< 거리를 두고 화염탄을 쏜다 — 세트 "Caster"
    };
} // namespace sw

namespace sw
{
    class AbilitySystemComponent;
    class ArenaDirectorComponent;
    class MeshComponent;

    /**
     * @class ArenaControllerComponent
     * @brief 유닛 오브젝트(메시 · 어빌리티 시스템 · HP 바와 같은 오브젝트)에 붙어 그 유닛만 움직입니다. 플레이어(입력)와 적(AI)이 이것을 잇습니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돕니다 — 디렉터(`PrePhysics`)가 이번 프레임의 유닛 모습(자리 · 편 · 살아 있음)을 다 적은 뒤입니다.
     *          자기 오브젝트에만 씁니다(자리 · 회전 · 쓰러질 때 납작해지기). 같은 오브젝트의 어빌리티 시스템과 같은 워커에서 돌기 때문에 입력 번호를
     *          누르면 어빌리티가 그 자리에서 발동하고, 다른 유닛에 거는 이펙트는 어빌리티 시스템이 틱 뒤로 미룹니다.
     *          디렉터는 핸들로 들고 매 프레임 풉니다. 어빌리티가 디렉터를 찾는 길도 이 핸들입니다(`ArenaDirectorComponent::findForUnit`).
     */
    REFLECT( Abstract, Category = "AbilityArena", DisplayName = "Arena Controller", Tooltip = "Common part of the arena unit controllers" )
    class ArenaControllerComponent : public Component
    {
    public:
        REFLECT_BODY();

        ArenaControllerComponent();
        virtual ~ArenaControllerComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 따를 디렉터와 처음 바라볼 쪽을 정합니다(디렉터가 스폰한 뒤 부른다). */
        void             assignDirector( GameObjectHandle director, const float3& facing );
        GameObjectHandle getDirector() const { return _director; }
        /** @brief 바라보는 방향(XZ 평면의 단위 벡터)입니다. 어빌리티가 투사체 방향으로 쓴다(같은 오브젝트의 워커에서). */
        const float3& getFacing() const { return _facing; }

        /** @brief XZ 평면 성분만 남긴 단위 벡터입니다. 길이가 거의 0 이면 @p fallback 입니다. */
        static float3 flattenDirection( const float3& direction, const float3& fallback );
        /** @brief 입력 번호 하나를 한 번 눌렀다 뗍니다(AI · 자동 전투). */
        static void tapInput( AbilitySystemComponent& abilitySystem, int32 inputId );

    protected:
        /** @brief 살아 있는 동안 한 프레임 — 입력 · AI 를 읽고 @p inoutPosition 을 옮깁니다. 바라보는 쪽(`_facing`)도 바꿀 수 있습니다. */
        virtual void tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem, float3& inoutPosition ) = 0;
        /** @brief @p inoutPosition 을 @p direction 으로 이동 속도(`MoveSpeed` current)만큼 옮기고 바라보는 쪽을 그쪽으로 돌립니다. */
        void moveTowards( const AbilitySystemComponent& abilitySystem, const float3& direction, float32 deltaTime, float3& inoutPosition );
        void setFacing( const float3& facing ) { _facing = facing; }

    private:
        /** @brief 쓰러진 뒤 — 납작해지다가 디렉터가 걷는다. */
        void tickDeath( float32 deltaTime, MeshComponent& mesh );

    private:
        PROPERTY( Category = "Arena", DisplayName = "Director", Tooltip = "Object with the ArenaDirectorComponent" )
        GameObjectHandle _director;

        float3  _facing;     ///< XZ 단위 벡터
        float3  _deathScale; ///< 쓰러질 때의 크기(납작해지기 시작점)
        float32 _deathTimer; ///< 0 이상이면 쓰러진 뒤 걷기까지 남은 시간
    };
} // namespace sw
