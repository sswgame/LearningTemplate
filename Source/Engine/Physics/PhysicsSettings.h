/**
 * @file PhysicsSettings.h
 * @brief 사람이 고치는 물리 표 — 충돌 레이어와 레이어끼리의 충돌 표 · 물리 재질 · 중력 · 고정 스텝입니다(`engine/physics/physicssettings.xml`).
 * @details 코드는 이름으로만 고릅니다(`findLayerIndex` · `findMaterial`). 표에 없는 이름 · 32 개를 넘는 레이어 · 겹친 이름은 읽을 때 오류입니다.
 *          레이어 표는 2D · 3D 가 같이 쓰고, 겹침 월드(`PhysicsWorld`)도 같은 행렬을 받습니다(`makeCollisionLayers`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Physics/CollisionLayers.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 물리 재질 하나입니다. 두 셰이프가 닿으면 마찰은 기하 평균(√(a·b)), 반발은 큰 쪽을 씁니다(Jolt · Box2D 기본과 같다).
     * @code
     *     <PhysicsMaterialDef _name="Rubber" _friction="0.9" _restitution="0.8" _density="1100" />
     * @endcode
     */
    REFLECT()
    struct SW_API PhysicsMaterialDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Name the bodies and shapes pick it by" )
        hashed_string _name{};
        PROPERTY( Min = 0.0, Tooltip = "Coulomb friction coefficient" )
        float32 _friction{ 0.5f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Bounciness: 0 stops, 1 keeps the speed" )
        float32 _restitution{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Mass per volume (3D, kg/m^3) or per area (2D, kg/m^2)" )
        float32 _density{ 1000.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 충돌 레이어 하나와 그것이 부딪히는 레이어 이름들입니다. 표는 대칭으로 읽습니다 — 한쪽만 적어도 둘이 부딪힙니다.
     * @code
     *     <PhysicsLayerDef _name="Debris">
     *         <_listCollidesWith><item>Default</item><item>Static</item></_listCollidesWith>
     *     </PhysicsLayerDef>
     * @endcode
     */
    REFLECT()
    struct SW_API PhysicsLayerDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Layer name" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Layers this one collides with (symmetric)" )
        vector<hashed_string> _listCollidesWith;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 물리 설정 표 하나입니다. `PhysicsSystem` 이 기동 때 읽어 모든 씬에 나눠 줍니다.
     * @details 레이어 순서가 레이어 번호입니다(0..31). 레이어 · 재질이 하나도 없으면 `Default` 하나씩을 채웁니다(`ensureDefaults`).
     */
    REFLECT()
    struct SW_API PhysicsSettings
    {
        REFLECT_BODY();

        /** @brief 레이어 수의 상한입니다(`CollisionLayers::kLayerCount`). */
        static constexpr uint32 kMaxLayerCount = CollisionLayers::kLayerCount;

        PROPERTY( Tooltip = "3D gravity", Units = "m/s2" )
        float3 _gravity{ 0.0f, -constant::kDefaultGravity, 0.0f };
        PROPERTY( Tooltip = "2D gravity", Units = "m/s2" )
        float2 _gravity2D{ 0.0f, -constant::kDefaultGravity };
        /** @brief 엔진 고정 스텝(`EngineConfig::_fixedDeltaTime`) 하나에 도는 물리 스텝 수입니다. 물리 스텝 = 고정 스텝 / 이 값, 프레임당 상한도 이 배수다. */
        PROPERTY( Min = 1, Max = 16, Tooltip = "Physics steps per engine fixed step (the step length comes from EngineConfig)" )
        uint32 _subStepCount{ 1 };
        PROPERTY( Min = 1, Tooltip = "Box2D sub-steps per step" )
        uint32 _subStepCount2D{ 4 };
        PROPERTY( Tooltip = "Collision layers in index order and what they collide with" )
        vector<PhysicsLayerDef> _listLayer;
        PROPERTY( Tooltip = "Physics materials" )
        vector<PhysicsMaterialDef> _listMaterial;

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면(파일 없음 · 모르는 이름 · 겹친 이름) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다(시험용). */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText );
        /** @brief 이름이 겹치지 않고 · 레이어가 32 개 이하이고 · 충돌 목록의 이름이 모두 있는 레이어인지 검사합니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate() const;
        /** @brief 레이어 · 재질이 비어 있으면 `Default` 하나씩을 채웁니다(Default 레이어는 자기와 부딪힌다). */
        void ensureDefaults();

        /** @brief 레이어 이름의 번호입니다. 없으면 false 입니다. */
        [[nodiscard]] bool findLayerIndex( const hashed_string& name, uint8& outIndex ) const;
        /** @brief 재질을 찾습니다. 없으면 nullptr 입니다. */
        const PhysicsMaterialDef* findMaterial( const hashed_string& name ) const;
        /** @brief 레이어 표를 대칭 행렬로 풉니다. 표에 없는 레이어 번호는 아무것과도 부딪히지 않습니다. */
        CollisionLayers makeCollisionLayers() const;
    };
} // namespace sw
