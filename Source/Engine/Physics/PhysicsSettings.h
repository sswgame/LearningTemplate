/**
 * @file PhysicsSettings.h
 * @brief 사람이 고치는 물리 설정입니다. 충돌 채널 · 충돌 프로필(응답), 물리 재질, 중력, 고정 스텝을 담습니다(`engine/physics/physicssettings.xml`).
 * @details 충돌은 언리얼 충돌 체계와 같은 세 층이다(DesignBacklog "충돌 채널 · 응답 · 프리셋").
 *          - **채널**(`CollisionChannelDef`): 이름 있는 칸 32 개 이하. 오브젝트 채널(바디의 종류 — WorldStatic · Pawn · Projectile …)과 트레이스 채널(질의의 종류 — Visibility · Camera).
 *          - **프로필**(`CollisionProfileDef`): 오브젝트 채널 하나 + 채널마다의 응답(막음 · 겹침 · 무시, 적지 않은 채널은 그 채널의 기본 응답) + 질의 · 물리 켜기. 32 개 이하이고 차례가 바디의 레이어 번호다.
 *          - **응답 표**: 두 프로필의 응답은 서로의 오브젝트 채널에 준 응답 중 약한 쪽이다(`makeCollisionLayers`). 트레이스 채널 질의는 그 채널을 막는 프로필만 맞는다(`makeTraceLayerMask`).
 *          코드는 이름으로만 고른다(`findLayerIndex` — 프로필 번호, `findMaterial`). 표에 없는 이름, 32 개를 넘는 채널 · 프로필, 겹친 이름은 읽을 때 오류다.
 *          표는 2D 와 3D 가 함께 쓰고, 2D 콜라이더(`BoxCollider2DComponent`)도 2D 씬의 바디라 같은 표를 받는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Content/AssetID.h"
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
        /** @brief 물의 밀도(kg/m^3)입니다 — 재질 밀도의 기준이자 기본값이고, 부력은 이것과 견준다(나무는 뜨고 돌은 가라앉는다). */
        static constexpr float32 kWaterDensity = 1000.0f;

        PROPERTY( Min = 0.0, Tooltip = "Mass per volume (3D, kg/m^3) or per area (2D, kg/m^2)" )
        float32 _density{ kWaterDensity };
    };
} // namespace sw

namespace sw
{
    /** @brief 충돌 채널의 종류입니다(언리얼 Object Channel · Trace Channel). */
    ENUM()
    enum class CollisionChannelKind : uint8
    {
        Object = 0, ///< 바디의 종류 — 프로필이 하나를 고른다
        Trace       ///< 질의의 종류 — 광선 · 쓸기 · 겹침 질의가 고른다
    };

    /**
     * @brief 충돌 채널 하나입니다. 프로필이 이 채널에 응답을 적지 않으면 `_defaultResponse` 를 쓴다(새 채널을 더해도 프로필마다 적지 않게 — 언리얼 채널 기본 응답).
     * @code
     *     <CollisionChannelDef _name="Camera" _kind="Trace" _defaultResponse="Ignore" />
     * @endcode
     */
    REFLECT()
    struct SW_API CollisionChannelDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Channel name" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Object: what a body is. Trace: what a query looks for" )
        CollisionChannelKind _kind{ CollisionChannelKind::Object };
        PROPERTY( Tooltip = "Response of the profiles that do not name this channel" )
        CollisionResponse _defaultResponse{ CollisionResponse::Block };
    };

    /** @brief 프로필이 채널 하나에 주는 응답입니다. */
    REFLECT()
    struct SW_API CollisionResponseDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Channel name", Meta = "NameSource=CollisionChannel" )
        hashed_string _channel{};
        PROPERTY( Tooltip = "Block stops, Overlap reports without stopping, Ignore sees nothing" )
        CollisionResponse _response{ CollisionResponse::Block };
    };

    /**
     * @brief 충돌 프로필(프리셋) 하나입니다 — 바디 · 콜라이더가 이름으로 고른다(언리얼 Collision Preset). 차례가 바디의 레이어 번호다.
     * @code
     *     <CollisionProfileDef _name="Projectile" _objectChannel="Projectile">
     *         <_listResponse><CollisionResponseDef _channel="Pawn" _response="Block" /><CollisionResponseDef _channel="Trigger" _response="Ignore" /></_listResponse>
     *     </CollisionProfileDef>
     * @endcode
     */
    REFLECT()
    struct SW_API CollisionProfileDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Profile name the bodies and colliders pick" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Object channel of the bodies with this profile", Meta = "NameSource=CollisionChannel" )
        hashed_string _objectChannel{};
        PROPERTY( Tooltip = "Responses to channels; channels not named use their default response" )
        vector<CollisionResponseDef> _listResponse;
        PROPERTY( Tooltip = "Traces and overlap queries can hit these bodies" )
        bool _bQueryEnabled{ true }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        PROPERTY( Tooltip = "These bodies block and overlap other bodies (off: queries only)" )
        bool _bPhysicsEnabled{ true }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 물리 설정 하나입니다. `PhysicsSystem` 이 기동할 때 읽어 모든 씬에 나눠 줍니다.
     * @details 프로필 순서가 바디의 레이어 번호다(0..31). 채널 · 프로필 · 재질은 데이터가 하나 이상 적어야 한다 — 비면 `validate` 가 오류다(코드가 채우지 않는다).
     */
    REFLECT()
    struct SW_API PhysicsSettings
    {
        REFLECT_BODY();

        /** @brief 프로필 수(= 레이어 수)의 상한입니다(`CollisionLayers::kLayerCount`). */
        static constexpr uint32 kMaxLayerCount = CollisionLayers::kLayerCount;
        /** @brief 채널 수의 상한입니다(오브젝트 + 트레이스, 언리얼 `ECollisionChannel` 32 칸과 같다). */
        static constexpr uint32 kMaxChannelCount = 32;
        /** @brief 2D 일꾼 수의 상한입니다(`_workerCount2D`). Box2D 의 상한(`B2_MAX_WORKERS`)보다 작다. */
        static constexpr uint32 kMaxWorkerCount2D = 16;
        /** @brief 3D 캐릭터 일괄 이동 청크 수의 상한입니다(`_characterChunkCount`). */
        static constexpr uint32 kMaxCharacterChunkCount = 64;

        PROPERTY( Tooltip = "3D gravity", Units = "m/s2" )
        float3 _gravity{ 0.0f, -constant::physics::kDefaultGravity, 0.0f };
        PROPERTY( Tooltip = "2D gravity", Units = "m/s2" )
        float2 _gravity2D{ 0.0f, -constant::physics::kDefaultGravity };
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
        /**
         * @brief 3D 씬 하나가 담는 바디 수 · 바디 쌍 수 · 접촉 제약 수의 상한입니다(Jolt `PhysicsSystem::Init`). 데이터가 적어야 한다 — 0 이면 `validate` 가 오류다.
         * @details 쌍 · 접촉이 넘치면 그 스텝의 접촉 일부를 버리고 경고한다(`JoltPhysicsScene.JoltStepReportedOverflow`). 언리얼 `MaxBodies` · Jolt 샘플 `cMaxBodies` 자리.
         */
        PROPERTY( Min = 1, Tooltip = "Most bodies one 3D scene holds" )
        uint32 _maxBodyCount{ 0 };
        PROPERTY( Min = 1, Tooltip = "Most body pairs the 3D broad phase keeps per step" )
        uint32 _maxBodyPairCount{ 0 };
        PROPERTY( Min = 1, Tooltip = "Most contact constraints a 3D step solves" )
        uint32 _maxContactCount{ 0 };
        /**
         * @brief 3D 씬 하나의 임시 할당 블록 크기(바이트)입니다 — 스텝 · 캐릭터 이동 · 소프트 바디가 나눠 쓴다. 0 이면 `validate` 가 오류다.
         * @details 넘치면 멈추지 않고 힙에서 받는다(`JoltTempAllocator`, Jolt `TempAllocatorImplWithMallocFallback` 과 같은 길) — 그 스텝을 카운터
         *          (`Physics.TempAllocator.fallbacks`)와 씬마다 한 번의 경고로 알린다. 경고가 나면 이 값을 `Physics.TempAllocator.peakBytes` 보다 크게 올린다.
         */
        PROPERTY( Min = 1, Tooltip = "Bytes of the per-scene temporary block (step, character moves, soft bodies); overflow falls back to the heap with a warning" )
        uint32 _tempAllocatorBytes{ 0 };
        /**
         * @brief 3D 캐릭터 일괄 이동(`moveCharacters`)을 나누는 청크 수입니다 — 청크마다 임시 블록(`_characterChunkTempBytes`)을 하나씩 둔다(감사 S-8).
         * @details 청크는 작업 풀의 병렬 for 가 나눠 돈다. 워커보다 넉넉히 두면 무거운 캐릭터(모여 서로 미는 무리)가 한 청크에 몰려도 다른 워커가 나머지를 가져간다.
         *          결과는 청크 수와 관계없이 같다(캐릭터끼리는 단계 앞 사본을 보고, 바디에 주는 힘은 단계 끝에 목록 차례로 준다 — `PhysicsCharacterTest.BatchMoveEqualsOneByOne`).
         *          측정하는 법은 부하 시나리오 `load/charactermove`(워커 1 · 4 · 8).
         */
        PROPERTY( Min = 1, Max = kMaxCharacterChunkCount, Tooltip = "Chunks a 3D scene splits a character batch move into; each chunk owns a temporary block" )
        uint32 _characterChunkCount{ 16 };
        /** @brief 캐릭터 이동 청크 하나의 임시 블록 크기(바이트)입니다. 넘치면 힙으로 이어 가며 씬 임시 블록과 같은 카운터 · 경고로 알린다(`JoltTempAllocator`). */
        PROPERTY( Min = 1, Tooltip = "Bytes of each character-move chunk's temporary block; overflow falls back to the heap with a warning" )
        uint32 _characterChunkTempBytes{ 262144 };
        /** @brief 한 씬의 모든 파괴 오브젝트가 함께 드는 떨어진 덩어리 바디의 상한입니다. 넘으면 씬 전체에서 작은 것 · 오래된 것부터 사라진다(`DebrisBudget`). 0 이면 `validate` 가 오류다. */
        PROPERTY( Min = 1, Tooltip = "Most falling destruction debris bodies one scene keeps; past it the smallest, oldest pieces scene-wide fade out" )
        uint32 _maxDebrisBodyCount{ 0 };
        PROPERTY( Tooltip = "Collision channels: object channels (what a body is) and trace channels (what a query looks for)" )
        vector<CollisionChannelDef> _listChannel;
        PROPERTY( Tooltip = "Collision profiles in layer order: object channel and responses per channel" )
        vector<CollisionProfileDef> _listProfile;
        PROPERTY( Tooltip = "Physics materials" )
        vector<PhysicsMaterialDef> _listMaterial;
        /**
         * @brief 발 · 손 IK 의 땅 광선이 맞는 층입니다(걸을 수 있는 세계). 비거나 모르는 이름이면 `validate` 가 오류다.
         * @details 캐릭터 · 래그돌 · 파편 · 투사체를 빼야 지나가는 몸 · 떨어진 덩어리에 발이 올라타지 않는다(언리얼 발 IK 트레이스 채널 · 유니티 Foot IK 레이어 마스크).
         */
        PROPERTY( Tooltip = "Profiles foot and hand IK ground traces hit (the walkable world - not characters, ragdolls, debris or projectiles)", Meta = "NameSource=CollisionProfile" )
        vector<hashed_string> _listGroundQueryLayer;

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 파일이 없거나 모르는 이름이거나 이름이 겹쳐서 실패하면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 문자열을 읽고 검사합니다(테스트용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText );
        /**
         * @brief 채널 · 프로필 · 재질이 하나 이상인지, 이름이 겹치지 않는지, 32 개 이하인지, 프로필의 오브젝트 채널이 오브젝트 채널인지, 응답이 가리키는 채널이 있는지 검사합니다.
         *        어긋나면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] bool validate() const;

        /** @brief 충돌 프로필 이름의 레이어 번호(프로필 차례)를 찾습니다. */
        [[nodiscard]] optional<uint8> findLayerIndex( const hashed_string& name ) const;
        /** @brief 채널을 찾습니다. 없으면 nullptr 입니다. */
        const CollisionChannelDef* findChannel( const hashed_string& name ) const;
        /** @brief 프로필 @p profile 이 채널 @p channel 에 주는 응답입니다(적지 않았으면 채널 기본 응답, 모르는 채널이면 무시). */
        CollisionResponse getProfileResponse( const CollisionProfileDef& profile, const hashed_string& channel ) const;
        /**
         * @brief 트레이스 채널 @p traceChannel 질의가 맞는 레이어 마스크입니다 — 질의가 켜졌고 그 채널을 막는 프로필만(언리얼 트레이스 응답).
         * @details 모르는 이름 · 오브젝트 채널이면 오류를 남기고 nullopt 입니다(모두 맞는 마스크로 대신하지 않는다).
         */
        [[nodiscard]] optional<uint32> makeTraceLayerMask( const hashed_string& traceChannel ) const;
        /** @brief 재질을 찾습니다. 없으면 nullptr 입니다. */
        const PhysicsMaterialDef* findMaterial( const hashed_string& name ) const;
        /** @brief 프로필 쌍의 응답 표를 만듭니다 — 서로의 오브젝트 채널에 준 응답 중 약한 쪽, 물리가 꺼진 프로필은 모두 무시. 표에 없는 레이어 번호는 아무것과도 부딪히지 않습니다. */
        CollisionLayers makeCollisionLayers() const;
        /** @brief 땅 광선의 층 마스크입니다(`_listGroundQueryLayer`). */
        uint32 makeGroundQueryLayerMask() const;
    };
} // namespace sw
