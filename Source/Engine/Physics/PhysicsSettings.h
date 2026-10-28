/**
 * @file PhysicsSettings.h
 * @brief 사람이 고치는 물리 설정입니다. 충돌 레이어와 레이어끼리의 충돌 테이블, 물리 재질, 중력, 고정 스텝을 담습니다(`engine/physics/physicssettings.xml`).
 * @details 코드는 이름으로만 고른다(`findLayerIndex`, `findMaterial`). 테이블에 없는 이름, 32 개를 넘는 레이어, 겹친 이름은 읽을 때 오류다.
 *          레이어 테이블은 2D 와 3D 가 함께 쓰고(`makeCollisionLayers`), 2D 콜라이더(`BoxCollider2DComponent`)도 2D 씬의 바디라 같은 행렬을 받는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Physics/Collision/CollisionLayers.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 물리 재질 하나입니다. 두 셰이프가 닿으면 마찰은 기하 평균(√(a·b))을, 반발은 큰 쪽을 씁니다(Jolt 와 Box2D 의 기본과 같습니다).
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
     * @brief 충돌 레이어 하나와 그 레이어가 부딪히는 레이어 이름들입니다. 테이블은 대칭으로 읽으므로 한쪽만 적어도 둘이 부딪힙니다.
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
     * @brief 물리 설정 하나입니다. `PhysicsSystem` 이 기동할 때 읽어 모든 씬에 나눠 줍니다.
     * @details 레이어 순서가 레이어 번호다(0..31). 레이어와 재질은 데이터가 하나 이상 적어야 한다 — 비면 `validate` 가 오류다(코드가 채우지 않는다).
     */
    REFLECT()
    struct SW_API PhysicsSettings
    {
        REFLECT_BODY();

        /** @brief 레이어 수의 상한입니다(`CollisionLayers::kLayerCount`). */
        static constexpr uint32 kMaxLayerCount = CollisionLayers::kLayerCount;
        /** @brief 2D 일꾼 수의 상한입니다(`_workerCount2D`). Box2D 의 상한(`B2_MAX_WORKERS`)보다 작다. */
        static constexpr uint32 kMaxWorkerCount2D = 16;

        PROPERTY( Tooltip = "3D gravity", Units = "m/s2" )
        float3 _gravity{ 0.0f, -constant::kDefaultGravity, 0.0f };
        PROPERTY( Tooltip = "2D gravity", Units = "m/s2" )
        float2 _gravity2D{ 0.0f, -constant::kDefaultGravity };
        /** @brief 엔진 고정 스텝(`EngineConfig::_fixedDeltaTime`) 하나에 도는 물리 스텝 수입니다. 물리 스텝 = 고정 스텝 / 이 값, 프레임당 상한도 이 배수다. */
        PROPERTY( Min = 1, Max = 16, Tooltip = "Physics steps per engine fixed step (the step length comes from EngineConfig)" )
        uint32 _subStepCount{ 1 };
        PROPERTY( Min = 1, Tooltip = "Box2D sub-steps per step" )
        uint32 _subStepCount2D{ 4 };
        /**
         * @brief 2D 씬 하나가 스텝에 쓰는 일꾼 수(스텝 스레드 포함)입니다. 1 이면 한 스레드이고, 2 이상이면 씬마다 전용 워커를 그만큼 하나 적게 띄웁니다(`Box2DWorkerPool`).
         * @details 바디가 수천일 때만 이득이다 — 측정하는 법은 `PhysicsBenchTest.Box2DStepByWorkerCount`. 결과는 워커 수와 관계없이 같다.
         */
        PROPERTY( Min = 1, Max = kMaxWorkerCount2D, Tooltip = "Threads a 2D scene steps with (1 = single thread; more starts dedicated Box2D workers per scene)" )
        uint32 _workerCount2D{ 1 };
        PROPERTY( Tooltip = "Collision layers in index order and what they collide with" )
        vector<PhysicsLayerDef> _listLayer;
        PROPERTY( Tooltip = "Physics materials" )
        vector<PhysicsMaterialDef> _listMaterial;

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 파일이 없거나 모르는 이름이거나 이름이 겹쳐서 실패하면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 문자열을 읽고 검사합니다(테스트용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText );
        /** @brief 레이어 · 재질이 하나 이상인지, 이름이 겹치지 않는지, 레이어가 32 개 이하인지, 충돌 목록의 이름이 모두 존재하는 레이어인지 검사합니다. 어긋나면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool validate() const;

        /** @brief 레이어 이름의 번호를 찾습니다. 없으면 false 를 반환합니다. */
        [[nodiscard]] bool findLayerIndex( const hashed_string& name, uint8& outIndex ) const;
        /** @brief 재질을 찾습니다. 없으면 nullptr 입니다. */
        const PhysicsMaterialDef* findMaterial( const hashed_string& name ) const;
        /** @brief 레이어 테이블을 대칭 행렬로 풉니다. 테이블에 없는 레이어 번호는 아무것과도 부딪히지 않습니다. */
        CollisionLayers makeCollisionLayers() const;
    };
} // namespace sw
