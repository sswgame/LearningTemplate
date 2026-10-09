/**
 * @file GimmickCircuitComponent.h
 * @brief 씬 · 프리팹에 저장되는 기믹 회로 — 노드 · 배선 목록(PROPERTY)으로 `GimmickCircuit` 을 짓고, 센서를 오브젝트에서 읽고, 액추에이터를 오브젝트에 겁니다.
 * @details 노드마다 대상 오브젝트(`_listNodeTarget`, 비면 이 컴포넌트의 소유자)를 핸들로 가리킵니다 — 저장된 참조는 이름이 아니라 id 라서 씬 · 프리팹 로드
 *          묶음이 이 실행의 오브젝트로 옮깁니다. 로드(`onPostLoad`)에서 정의를 검증하고 모르는 종류 · 노드 · 포트 · 매개변수는 오류로 남깁니다(회로는 돌지 않는다).
 *
 *          센서 노드가 읽는 것: Volume = 대상의 `GimmickSensorComponent` 겹친 수, PressurePlate = 겹친 무게, Damage · Interaction = 쌓인 피해 · 사용,
 *          Signal = 신호, Laser = 대상 자리에서 `direction`(대상 회전 적용) × `range` 광선이 막혔는가(`WorldQuery`), Proximity = `tag` 를 가진 가장 가까운
 *          오브젝트까지 거리(`planar` 면 Z 를 뺀 2D 거리). 액추에이터: Door · Elevator · Rotator 는 플레이 시작의 로컬 자세(`_listRestPose`)에서 더하고,
 *          Mover 는 대상(없으면 소유자)의 `SplineComponent` 월드 곡선 위 자리를 월드 자리로 씁니다. Light 는 `LightComponent` 세기, Enable 은 오브젝트 켜기,
 *          Spawner 는 프리팹 스폰, Sound 는 `GameSound`, Hazard 는 켜진 동안 대상 센서의 겹친 것들에게 피해(`GimmickDamageEvent` + 그쪽 센서의 Damage).
 *          모두 2D(XY 평면)와 3D 에서 같습니다 — 자리 · 회전 · 축은 데이터의 벡터 그대로입니다.
 *
 *          고정 스텝으로 돕니다(`_stepTime`). 걸음을 낸 틱마다 회로 상태를 `_stateBytes`(PROPERTY)에 써 두어 레벨 세이브 · 핫 리로드 · 플레이 복원이
 *          그 상태로 이어 갑니다. 체크포인트는 `captureCheckpoint` · `restoreCheckpoint`, 처음은 `resetCircuit`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuit.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 저장되는 노드 하나 — id · 종류 · 매개변수 글("openTime=0.5; openOffset=0 3 0")입니다. */
    REFLECT()
    struct SW_GF_API GimmickNodeDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Node id, unique in this circuit" )
        hashed_string _id{};
        PROPERTY( Tooltip = "Node kind name (GimmickNodeRegistry)" )
        hashed_string _kind{};
        PROPERTY( Tooltip = "Parameters as name=value pairs separated by ';'" )
        string _params{};
    };
} // namespace sw

namespace sw
{
    /** @brief 저장되는 배선 하나 — "node.Output" → "node.Input" 입니다. */
    REFLECT()
    struct SW_GF_API GimmickWireDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Source node.output" )
        string _from{};
        PROPERTY( Tooltip = "Target node.input" )
        string _to{};
        PROPERTY( Tooltip = "Invert the signal on this wire" )
        bool _bInvert{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 액추에이터 대상의 쉬는 자세 — 플레이를 처음 시작할 때 한 번 잡고 저장해, 핫 리로드 · 세이브 로드가 움직이던 자리를 쉬는 자세로 착각하지 않게 합니다. */
    REFLECT()
    struct SW_GF_API GimmickRestPose
    {
        REFLECT_BODY();

        PROPERTY( Units = m )
        float3 _position{};
        PROPERTY( Units = rad )
        float3 _rotation{};
        PROPERTY()
        float32 _intensity{ 1.0f };
        PROPERTY()
        bool _bCaptured{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GimmickCircuitComponent
     * @brief 기믹 회로 하나를 씬에 둡니다. 레벨 하나에 여럿 둘 수 있고(퍼즐마다), 프리팹 안에 넣으면 그 프리팹의 기믹이 됩니다(점프대 · 문).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Gimmick Circuit", Tooltip = "Sensors, operators and actuators wired in data; fixed-step and deterministic" )
    class SW_GF_API GimmickCircuitComponent : public Component
    {
    public:
        REFLECT_BODY();

        GimmickCircuitComponent();
        virtual ~GimmickCircuitComponent() override = default;

        void onPostLoad() override;
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 노드를 더합니다(코드로 짓는 회로 · 시험). @p target 이 비면 소유자입니다. 다음 `rebuild` 부터 반영됩니다. */
        void addNode( const hashed_string& id, const hashed_string& kind, string_view params, GameObjectHandle target = GameObjectHandle{} );
        /** @brief 배선을 더합니다("node.Output", "node.Input"). */
        void addWire( string_view from, string_view to, bool bInvert = false );
        void clearCircuit();

        /** @brief 저장된 목록으로 정의를 만듭니다. 매개변수 글이 깨졌으면 그 이유를 @p outListError 에 더합니다. */
        void makeDef( GimmickCircuitDef& outDef, vector<string>& outListError ) const;
        /** @brief 정의를 다시 검증 · 짓고 오류를 로그로 냅니다. 지었으면 true 입니다. 회로 상태는 처음으로 돌아갑니다. */
        bool rebuild();
        /** @brief 센서를 읽고 한 걸음 돌리고 액추에이터를 겁니다(시험 · 일시정지 중 한 걸음). */
        void stepOnce();

        /** @brief 지금 상태를 체크포인트로 잡습니다. */
        void captureCheckpoint();
        /** @brief 잡아 둔 체크포인트로 돌아갑니다(없으면 처음으로). 액추에이터 자세도 그 상태로 다시 겁니다. */
        void restoreCheckpoint();
        /** @brief 지은 직후 상태로 돌립니다. */
        void resetCircuit();

        const GimmickCircuit& getCircuit() const { return _circuit; }
        GimmickCircuit&       getCircuit() { return _circuit; }
        const vector<string>& getErrors() const { return _listError; }
        const vector<uint8>&  getStateBytes() const { return _stateBytes; }
        int32                 findNode( const hashed_string& id ) const { return _circuit.findNode( id ); }

    private:
        /** @brief 노드가 센서로서 무엇을 읽는가 · 액추에이터로서 무엇을 거는가입니다(종류 이름으로 정한다 — 게임이 더한 종류는 None). */
        enum class Binding : uint8
        {
            None = 0,
            Volume,
            PressurePlate,
            Laser,
            Proximity,
            Damage,
            Interaction,
            Signal,
            Door,
            Mover,
            Elevator,
            Rotator,
            Spawner,
            Hazard,
            Light,
            Sound,
            Enable
        };

        void           bindNodes();
        void           pullSensors( GameObjectManager& manager );
        void           applyActuators( GameObjectManager& manager );
        void           applyActuator( GameObjectManager& manager, int32 node, Binding binding );
        void           storeState();
        GameObject*    resolveTarget( GameObjectManager& manager, int32 node ) const;
        float32        computeProximity( GameObjectManager& manager, int32 node, const GameObject& target ) const;
        float32        computeLaserBlocked( GameObjectManager& manager, int32 node, const GameObject& target ) const;
        static Binding findBinding( const hashed_string& kind );

    private:
        PROPERTY( Category = "Circuit", DisplayName = "Nodes" )
        vector<GimmickNodeDesc> _listNode;
        PROPERTY( Category = "Circuit", DisplayName = "Node Targets", Tooltip = "Object each node reads or drives, by node index; empty is this object" )
        vector<GameObjectHandle> _listNodeTarget;
        PROPERTY( Category = "Circuit", DisplayName = "Wires" )
        vector<GimmickWireDesc> _listWire;
        PROPERTY( Category = "Circuit", DisplayName = "Rest Poses", Tooltip = "Actuator rest poses captured at first play (runtime)" )
        vector<GimmickRestPose> _listRestPose;
        PROPERTY( HideInInspector )
        vector<uint8> _stateBytes;
        PROPERTY( Category = "Circuit", DisplayName = "Step Time", Min = 0.001, Tooltip = "Fixed simulation step", Units = s )
        float32 _stepTime;

        GimmickCircuit  _circuit;
        vector<Binding> _listBinding;
        vector<string>  _listError;
        vector<uint8>   _checkpointBytes;
    };
} // namespace sw
