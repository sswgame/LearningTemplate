/**
 * @file GimmickCircuitComponent.h
 * @brief 씬과 프리팹에 저장되는 기믹 회로입니다. 노드와 배선 목록(PROPERTY)으로 `GimmickCircuit` 을 만들고, 센서 값을 오브젝트에서 읽고, 액추에이터를 오브젝트에 적용합니다.
 * @details 노드마다 대상 오브젝트(`_listNodeTarget`, 비어 있으면 이 컴포넌트의 소유자)를 핸들로 가리킨다. 저장된 참조는 이름이 아니라 id 이므로 씬과 프리팹을 로드할 때
 *          그 id 를 이번 실행의 오브젝트로 옮긴다. 로드(`onPostLoad`)에서 정의를 검증하고, 알 수 없는 종류, 노드, 포트, 매개변수는 오류로 남긴다. 오류가 있으면 회로는 돌지 않는다.
 *
 *          센서 노드가 읽는 값은 다음과 같다. Volume 은 대상의 `GimmickSensorComponent` 에 겹친 수, PressurePlate 는 겹친 무게, Damage 와 Interaction 은 쌓인 피해와 사용 횟수이다.
 *          Signal 은 신호이고, Laser 는 대상 위치에서 `direction`(대상 회전 적용) × `range` 광선이 막혔는지 여부(`WorldQuery`)이며, Proximity 는 `tag` 를 가진 가장 가까운
 *          오브젝트까지의 거리이다(`planar` 이면 Z 를 뺀 2D 거리). 액추에이터는 다음과 같이 동작한다. Door, Elevator, Rotator 는 플레이 시작 시점의 로컬 자세(`_listRestPose`)에 값을 더한다.
 *          Mover 는 대상(없으면 소유자)의 `SplineComponent` 월드 곡선 위의 위치를 월드 위치로 쓴다. Light 는 `LightComponent` 의 세기를, Enable 은 오브젝트의 켜기와 끄기를 바꾼다.
 *          Spawner 는 프리팹을 스폰하고, Sound 는 `GameSound` 를 재생하며, Hazard 는 켜져 있는 동안 대상 센서에 겹친 것들에게 피해를 준다(`GimmickDamageEvent` 와 그쪽 센서의 Damage).
 *          모두 2D(XY 평면)와 3D 에서 같게 동작한다. 위치, 회전, 축은 데이터의 벡터를 그대로 쓴다.
 *
 *          고정 스텝으로 돈다(`_stepTime`). 스텝을 실행한 틱마다 회로 상태를 `_state`(PROPERTY, `GimmickCircuitState`)에 써 두므로, 레벨 세이브와 핫 리로드와 플레이 복원이
 *          그 상태에서 이어진다(자기 기술형 A). 체크포인트는 `captureCheckpoint` 와 `restoreCheckpoint` 로, 처음 상태로의 복귀는 `resetCircuit` 으로 한다.
 *          체크포인트는 프로세스 밖으로 나가지 않으므로 바이트가 아니라 값으로 둔다. 롤백 · 넷 스냅샷은 `GimmickStateBundle` 이 고정 배치형(B)으로 싣는다.
 *
 *          저장 상태를 회로에 적용하는 길은 `applySavedState` 하나다(로드, 플레이 시작, 체크포인트). 상태는 노드 id 로 짝짓고(`GimmickCircuit::loadState`),
 *          노드가 늘거나 준 것은 경고 뒤 이어 가며, 같은 id 인데 종류나 상태 버전이 바뀐 노드가 있으면 오류를 남기고 회로를 돌리지 않는다. 조용히 처음 상태로 바꾸지 않는다.
 *          씬 · 프리팹 저작 저장에도 이 런타임 상태가 들어갈 수 있다(지금 저장 문맥이 레벨 세이브와 같다). 저작 저장에서 빼는 표시는 옛 `C-53` 다(GimmickCircuitState).
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

    /** @brief 저장되는 노드 하나입니다. id, 종류, 매개변수 문자열("openTime=0.5; openOffset=0 3 0")을 담습니다. */
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
    /** @brief 저장되는 배선 하나입니다. "node.Output" 에서 "node.Input" 으로 이어집니다. */
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
    /** @brief 액추에이터 대상의 쉬는 자세입니다. 플레이를 처음 시작할 때 한 번 기록해 저장합니다. 그래서 핫 리로드나 세이브 로드를 해도 움직이던 위치를 쉬는 자세로 착각하지 않습니다. */
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
     * @brief 기믹 회로 하나를 씬에 둡니다. 레벨 하나에 여러 개를 둘 수 있고(퍼즐마다), 프리팹 안에 넣으면 그 프리팹의 기믹이 됩니다(점프대, 문).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Gimmick Circuit", Tooltip = "Sensors, operators and actuators wired in data; fixed-step and deterministic" )
    class SW_GF_API GimmickCircuitComponent : public Component
    {
    public:
        REFLECT_BODY();

        GimmickCircuitComponent();
        virtual ~GimmickCircuitComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 올라 "회로를 모두" 를 씬을 훑지 않고 돌려줍니다(`GimmickStateBundle`). */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPostLoad() override;
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 노드를 추가합니다. 코드로 만드는 회로와 테스트에 씁니다. @p target 이 비어 있으면 소유자입니다. 다음 `rebuild` 부터 반영됩니다. */
        void addNode( const hashed_string& id, const hashed_string& kind, string_view params, GameObjectHandle target = GameObjectHandle{} );
        /** @brief 배선을 추가합니다("node.Output", "node.Input"). */
        void addWire( string_view from, string_view to, bool bInvert = false );
        void clearCircuit();

        /** @brief 저장된 목록으로 정의를 만듭니다. 매개변수 문자열이 깨졌으면 그 이유를 @p outListError 에 추가합니다. */
        void makeDef( GimmickCircuitDef& outDef, vector<string>& outListError ) const;
        /** @brief 정의를 다시 검증하고 회로를 만든 뒤 오류를 로그로 냅니다. 회로를 만들었으면 true 입니다. 회로 상태는 처음으로 돌아갑니다. */
        bool rebuild();
        /** @brief 센서를 읽고, 한 스텝 진행하고, 액추에이터를 적용합니다. 테스트와 일시정지 중 한 스텝 진행에 씁니다. */
        void stepOnce();

        /** @brief 지금 상태를 체크포인트로 저장합니다. */
        void captureCheckpoint();
        /**
         * @brief 저장해 둔 체크포인트로 돌아갑니다. 잡은 체크포인트가 없으면(잡기 전이거나 `resetCircuit`) 처음 상태로 돌아갑니다. 액추에이터 자세도 그 상태로 다시 적용합니다.
         * @details 체크포인트를 잡은 뒤 회로를 고쳐 다시 만들었으면 `applySavedState` 의 규칙을 따른다. 종류가 바뀐 노드가 있으면 오류로 멈춘다.
         */
        void restoreCheckpoint();
        /** @brief 만든 직후 상태로 되돌립니다. */
        void resetCircuit();
        /**
         * @brief 같은 빌드 · 같은 회로가 만든 상태(롤백 저장, 다른 기계에서 온 넷 스냅샷)로 되돌리고 액추에이터 자세를 다시 적용합니다.
         * @return 회로가 없거나 상태가 이 회로와 정확히 맞지 않으면(`GimmickStateMatch::Exact` 가 아니면) 아무것도 바꾸지 않고 false 입니다. 그 의미는 부르는 쪽이 정합니다.
         */
        [[nodiscard]] bool restoreState( const GimmickCircuitState& state );

        const GimmickCircuit&      getCircuit() const { return _circuit; }
        GimmickCircuit&            getCircuit() { return _circuit; }
        const vector<string>&      getErrors() const { return _listError; }
        const GimmickCircuitState& getState() const { return _state; }
        int32                      findNode( const hashed_string& id ) const { return _circuit.findNode( id ); }

    private:
        /** @brief 노드가 센서로서 무엇을 읽고, 액추에이터로서 무엇을 적용하는지 나타냅니다. 종류 이름으로 정하며, 게임이 추가한 종류는 None 입니다. */
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

        void bindNodes();
        void pullSensors( GameObjectManager& manager );
        void applyActuators( GameObjectManager& manager );
        void applyActuator( GameObjectManager& manager, int32 node, Binding binding );
        void storeState();
        /**
         * @brief 저장 상태(씬 · 프리팹 · 레벨 세이브의 `_state`, 체크포인트)를 회로에 적용하는 유일한 길입니다. `onPostLoad` · `onBeginPlay` · `restoreCheckpoint` 가 지납니다.
         * @details 노드가 늘거나 줄었으면 경고 한 줄 뒤 이어 간다. 같은 id 인데 종류 · 상태 버전이 바뀐 노드가 있으면 오류를 `_listError` 와 로그에 남기고
         *          회로를 비워 돌지 않게 한다. 처음 상태로 조용히 바꾸면 레벨 세이브의 진행이 경고 한 줄로만 사라진다.
         */
        void applySavedState( const GimmickCircuitState& state );
        /** @brief 상태를 바꾼 뒤 액추에이터 자세를 다시 적용하고 `_state` 를 맞춥니다. */
        void           commitRestoredState();
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
        PROPERTY( RuntimeState, Category = "Circuit", DisplayName = "Rest Poses", Tooltip = "Actuator rest poses captured at first play (runtime)" )
        vector<GimmickRestPose> _listRestPose;
        PROPERTY( RuntimeState, HideInInspector )
        GimmickCircuitState _state;
        PROPERTY( Category = "Circuit", DisplayName = "Step Time", Min = 0.001, Tooltip = "Fixed simulation step", Units = s )
        float32 _stepTime;

        GimmickCircuit      _circuit;
        vector<Binding>     _listBinding;
        vector<string>      _listError;
        GimmickCircuitState _checkpoint; ///< 비어 있으면 잡은 체크포인트가 없다
    };
} // namespace sw
