/**
 * @file GimmickCircuit.h
 * @brief 기믹 회로 실행입니다. 정의를 레지스트리로 검증해서 회로를 만들고, 고정 스텝으로 센서, 연산자, 액추에이터 순서로 평가합니다. 상태는 `GimmickCircuitState` 로 저장하고 복원하고 해시합니다.
 * @details 결정적이다. 시간은 `step()`(고정 스텝)으로만 흐르고 벽시계를 읽지 않으며, 평가 순서는 회로를 만들 때 정한 위상 순서 하나뿐이다. 같은 정의와
 *          같은 센서 입력이면 같은 상태 해시가 나온다. 그래서 롤백 넷코드가 되감아서 다시 돌릴 수 있다. 상태 값 하나를 레벨 세이브(A)와 롤백 · 넷 스냅샷(B)이 함께 쓰고,
 *          바이트로 바꾸는 일은 이 클래스가 하지 않는다(`GimmickCircuitState.h`). 실행 중에는 슬롯을 회로 전체의 평평한 목록에 두고, 저장할 때만 노드별로 나눈다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitDef.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitState.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickNodeRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 저장 상태가 이 회로와 맞는 정도입니다. 값이 클수록 약하게 맞습니다(`GimmickCircuit::loadState` 의 받아들이는 한계로 씁니다). */
    enum class GimmickStateMatch : uint8
    {
        Exact = 0,      ///< 회로의 모든 노드가 저장 상태에 있고 남는 저장 노드도 없다
        NodeSetChanged, ///< 노드가 늘거나 줄었다. 짝이 맞는 노드만 옮기고, 새 노드는 처음 상태, 저장에만 있는 노드는 버린다
        Mismatch        ///< 같은 id 인데 종류, 상태 버전, 슬롯 수가 다른 노드가 있거나, 저장 상태에 같은 id 가 둘이다. 아무것도 옮기지 않는다
    };

    /** @brief `GimmickCircuit::loadState` 의 결과입니다. */
    struct [[nodiscard]] GimmickStateLoad
    {
        GimmickStateMatch _match{ GimmickStateMatch::Mismatch };
        int32             _changedNode{ -1 }; ///< `Mismatch` 일 때 종류나 상태 뜻이 바뀐 이 회로의 노드 번호. 저장 상태 자체가 깨졌으면(같은 id 둘) -1
        bool              _bApplied{ false }; ///< 상태를 옮겼는지. 받아들이는 한계보다 약하게 맞으면 false 이고 회로는 그대로다
    };
} // namespace sw

namespace sw
{
    /**
     * @class GimmickCircuit
     * @brief 만들어진 회로 하나입니다. 노드는 정의의 순서 번호로 가리킵니다(`findNode`).
     */
    class SW_GF_API GimmickCircuit
    {
    public:
        GimmickCircuit();

        /**
         * @brief 정의를 검증하고 회로를 만듭니다. 문제는 하나에서 멈추지 않고 모두 @p outListError 에 한 줄씩 모읍니다. 문제가 하나라도 있으면 false 를 반환하고 회로는 비어 있습니다.
         * @details 검증 항목은 다음과 같다. 비어 있거나 겹치는 노드 id, 알 수 없는 종류, 알 수 없는 매개변수, 숫자가 아닌 숫자 매개변수, 배선의 알 수 없는 노드와 포트(출력에서 입력 방향),
         *          종류의 초기화가 거절한 매개변수, 지연(Delay)을 지나지 않는 고리, 상태가 담을 수 있는 노드 수(`GimmickCircuitState::kMaxNodeCount`)를 넘는 회로이다.
         *          슬롯 수 상한은 종류를 등록할 때 이미 막았다(`GimmickNodeRegistry::registerKind`). 레지스트리는 소유하지 않고 참조만 하므로 회로보다 오래 살아 있어야 한다.
         */
        [[nodiscard]] bool populate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError );
        /** @brief 회로를 만들지 않고 검증만 합니다. 로드할 때와 에디터에서 저장할 때 씁니다. */
        [[nodiscard]] static bool validate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError );
        void                      clear();

        /** @brief 만든 직후의 상태로 되돌립니다. 스텝 번호는 0, 센서 값은 0 입니다. */
        void resetToInitial();
        /** @brief 한 스텝을 평가합니다. 충격(`addSensorImpulse`)은 이 스텝이 소비하고 비웁니다. */
        void step();
        /** @brief 시간을 누적해서 고정 스텝을 실행합니다. 실행한 스텝 수를 반환합니다. */
        int32 update( float32 deltaTime );
        /** @brief 시간을 누적만 하고 이번에 실행할 스텝 수를 반환합니다. 스텝마다 센서를 넣고 액추에이터를 적용하는 쪽이 그 수만큼 `step` 을 부릅니다. */
        int32 consumeTime( float32 deltaTime );

        /** @brief 센서의 수준 값(인원, 무게, 거리, 신호)을 넣습니다. 다음 스텝부터 반영됩니다. */
        void setSensorValue( int32 node, float32 value );
        /** @brief 센서에 충격(피해, 사용 횟수)을 더합니다. 다음 스텝이 한꺼번에 소비합니다. */
        void addSensorImpulse( int32 node, float32 amount );
        /** @brief 무버가 따라갈 경로의 길이를 지정합니다. 상태가 아니라 배치 정보이므로 저장하지 않습니다. */
        void setPathLength( int32 node, float32 length );

        int32                  findNode( const hashed_string& id ) const;
        int32                  getNodeCount() const { return static_cast<int32>( _listNode.size() ); }
        const GimmickNodeKind& getKind( int32 node ) const { return *_listNode[static_cast<size_t>( node )]._pKind; }
        const hashed_string&   getNodeID( int32 node ) const { return _listNode[static_cast<size_t>( node )]._id; }
        uint32                 getOutputBits( int32 node ) const { return _listNode[static_cast<size_t>( node )]._outputBits; }
        bool                   getOutput( int32 node, int32 port ) const { return port >= 0 && ( getOutputBits( node ) & ( 1u << static_cast<uint32>( port ) ) ) != 0; }
        /** @brief 이름으로 출력을 읽습니다. 알 수 없는 포트이면 false 입니다. */
        bool getOutput( int32 node, const hashed_string& port ) const { return getOutput( node, getKind( node ).findOutput( port ) ); }
        /** @brief 액추에이터가 오브젝트에 적용하는 값입니다. 상태 float[0] 이며 열림, 거리, 높이, 각도를 나타냅니다. 실수 상태가 없으면 0 입니다. */
        float32 getActuatorValue( int32 node ) const;
        float32 getFloatState( int32 node, int32 slot ) const;
        int32   getIntState( int32 node, int32 slot ) const;
        /** @brief 해석된 매개변수입니다. 종류에 없는 이름이면 nullptr 입니다. */
        const GimmickParamValue* findParam( int32 node, const hashed_string& name ) const;

        /**
         * @brief 상태 전체를 씁니다. 스텝 번호, 누적 시간, 노드마다 id, 종류, 상태 버전, 출력, 지난 입력, 센서 값, 상태 슬롯이 들어 있습니다.
         * @details @p outState 의 목록 용량을 이어 쓰므로, 틱마다 같은 값에 쓰면 새로 할당하지 않는다.
         */
        void saveState( GimmickCircuitState& outState ) const;
        /**
         * @brief 저장 상태를 노드 id 로 짝지어 옮깁니다. 짝이 맞는 정도(`GimmickStateMatch`)가 @p weakestAccepted 보다 약하면 아무것도 바꾸지 않습니다.
         * @details 롤백과 넷 스냅샷(같은 빌드, 같은 회로)은 `Exact` 만, 레벨 세이브와 씬처럼 회로 데이터가 그 뒤에 바뀌었을 수 있는 것은 `NodeSetChanged` 까지 받는다.
         *          `Mismatch` 는 어떤 한계로도 받지 않는다. 받지 않은 결과를 무엇으로 볼지는 부르는 쪽이 정한다(`GimmickCircuitComponent::applySavedState`).
         */
        GimmickStateLoad loadState( const GimmickCircuitState& state, GimmickStateMatch weakestAccepted );
        /** @brief 상태의 64 비트 해시입니다. 고정 배치형 바이트(`FixedLayout::write`)의 FNV-1a 입니다. 같은 입력이면 같은 값이 나오므로 결정성 검증과 비동기 감지에 씁니다. */
        uint64 computeStateHash() const;

        bool    isBuilt() const { return _listNode.empty() == false; }
        uint32  getStepIndex() const { return _stepIndex; }
        float32 getStepTime() const { return _stepTime; }

    private:
        /** @brief 노드 하나의 실행 정보입니다. */
        struct NodeRuntime
        {
            hashed_string          _id{};
            const GimmickNodeKind* _pKind{ nullptr };
            uint32                 _paramStart{ 0 };
            uint32                 _floatStart{ 0 };
            uint32                 _intStart{ 0 };
            uint32                 _wireStart{ 0 }; ///< 이 노드로 들어오는 배선(`_listInputWire`)의 시작 위치
            uint32                 _wireCount{ 0 };
            uint32                 _outputBits{ 0 };
            uint32                 _previousInputBits{ 0 };
            uint32                 _connectedInputBits{ 0 };
            float32                _sensorValue{ 0.0f };
            float32                _sensorImpulse{ 0.0f };
            float32                _pathLength{ 0.0f };
        };

        /** @brief 노드로 들어오는 배선 하나입니다. */
        struct InputWire
        {
            uint32 _sourceNode{ 0 };
            uint8  _sourcePort{ 0 };
            uint8  _targetPort{ 0 };
            uint8  _bInvert{ SW_FALSE };
        };

        uint32             gatherInputs( const NodeRuntime& node ) const;
        GimmickNodeContext makeContext( NodeRuntime& node );
        /** @brief `populate` 1 단계입니다. 노드의 id, 종류, 매개변수를 읽고 상태 슬롯을 나눠 줍니다. */
        void populateNodes( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError );
        /** @brief `populate` 2 단계입니다. 배선을 받는 노드 순서대로 모읍니다. 같은 입력에 여러 배선이 오면 OR 로 처리합니다. */
        void populateWires( const GimmickCircuitDef& def, vector<string>& outListError );
        /** @brief `populate` 3 단계입니다. 종류의 초기화가 매개변수를 해석해 처음 상태를 채웁니다. 틀린 이름이나 형식은 여기서 거절합니다. */
        void initializeNodeStates( const GimmickCircuitDef& def, vector<string>& outListError );
        /**
         * @brief `populate` 4 단계입니다. 평가 순서를 정합니다.
         * @details 고리를 끊는 노드(지연)가 먼저이고, 나머지는 위상 순서입니다. 위상 순서가 같으면 정의 순서를 따릅니다.
         *          남는 노드가 있으면 지연 없는 고리가 있다는 뜻이라 오류로 알립니다.
         */
        void computeEvaluationOrder( const GimmickCircuitDef& def, vector<string>& outListError );
        /** @brief 저장 노드 하나를 이 회로의 @p nodeIndex 노드에 옮깁니다. 종류와 슬롯 수는 부르는 쪽이 이미 대조했습니다. */
        void writeNode( size_t nodeIndex, const GimmickNodeState& state );
        /** @brief 같은 회로가 쓴 상태(노드 순서가 같음)를 그대로 옮깁니다. `resetToInitial` 과 `loadState` 가 씁니다. */
        void copyState( const GimmickCircuitState& state );

        vector<NodeRuntime>       _listNode;
        vector<InputWire>         _listInputWire;
        vector<GimmickParamValue> _listParam;
        vector<float32>           _listFloatState;
        vector<int32>             _listIntState;
        vector<uint32>            _listOrder; ///< 평가 순서. 고리를 끊는 노드가 먼저 온다
        GimmickCircuitState       _initialState;
        FixedStepTimer            _timer;
        uint32                    _stepIndex;
        float32                   _stepTime;
    };
} // namespace sw
