/**
 * @file GimmickCircuit.h
 * @brief 기믹 회로 실행 — 정의를 등록부로 검증 · 짓고, 고정 스텝으로 센서 → 연산자 → 액추에이터를 평가하며, 상태를 바이트로 저장 · 복원 · 해시합니다.
 * @details 결정적입니다: 시간은 `step()`(고정 걸음)으로만 흐르고 벽시계를 읽지 않으며, 평가 순서는 지을 때 정한 위상 순서 하나입니다. 같은 정의 ·
 *          같은 센서 입력이면 같은 상태 해시입니다 — 롤백 넷코드가 되감아 다시 돌리고, 레벨 세이브 · 체크포인트 · 네트워크 스냅샷이 같은 바이트를 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitDef.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickNodeRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class GimmickCircuit
     * @brief 지은 회로 하나입니다. 노드는 정의의 순서 번호로 가리킵니다(`findNode`).
     */
    class SW_GF_API GimmickCircuit
    {
    public:
        GimmickCircuit();

        /**
         * @brief 정의를 검증하고 짓습니다. 문제는 모두 @p outListError 에 한 줄씩 모으고(하나에서 멈추지 않는다) 하나라도 있으면 false 이고 비어 있습니다.
         * @details 검증: 빈 · 겹친 노드 id, 모르는 종류, 모르는 매개변수 · 숫자가 아닌 숫자 매개변수, 배선의 모르는 노드 · 포트(출력 → 입력 방향),
         *          종류의 초기화가 거절한 매개변수, 지연(Delay)을 지나지 않는 고리. 등록부는 빌려 들고 회로보다 오래 살아야 합니다.
         */
        [[nodiscard]] bool populate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError );
        /** @brief 짓지 않고 검증만 합니다(로드 때 · 에디터 저장 때). */
        [[nodiscard]] static bool validate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError );
        void                      clear();

        /** @brief 지은 직후의 상태로 돌립니다(걸음 번호 0, 센서 값 0). */
        void resetToInitial();
        /** @brief 한 걸음 평가합니다. 충격(`addSensorImpulse`)은 이 걸음이 먹고 비웁니다. */
        void step();
        /** @brief 시간을 쌓아 고정 걸음을 냅니다. 낸 걸음 수입니다. */
        int32 update( float32 deltaTime );
        /** @brief 시간을 쌓기만 하고 이번에 낼 걸음 수를 돌려줍니다(걸음마다 센서를 넣고 액추에이터를 거는 쪽이 `step` 을 그만큼 부른다). */
        int32 consumeTime( float32 deltaTime );

        /** @brief 센서의 수준 값(인원 · 무게 · 거리 · 신호)을 넣습니다. 다음 걸음부터 봅니다. */
        void setSensorValue( int32 node, float32 value );
        /** @brief 센서에 충격(피해 · 사용 횟수)을 더합니다. 다음 걸음이 한꺼번에 먹습니다. */
        void addSensorImpulse( int32 node, float32 amount );
        /** @brief 무버가 따라갈 길의 길이를 묶습니다(상태가 아니라 배치 — 저장하지 않는다). */
        void setPathLength( int32 node, float32 length );

        int32                  findNode( const hashed_string& id ) const;
        int32                  getNodeCount() const { return static_cast<int32>( _listNode.size() ); }
        const GimmickNodeKind& getKind( int32 node ) const { return *_listNode[static_cast<size_t>( node )]._pKind; }
        const hashed_string&   getNodeID( int32 node ) const { return _listNode[static_cast<size_t>( node )]._id; }
        uint32                 getOutputBits( int32 node ) const { return _listNode[static_cast<size_t>( node )]._outputBits; }
        bool                   getOutput( int32 node, int32 port ) const { return port >= 0 && ( getOutputBits( node ) & ( 1u << static_cast<uint32>( port ) ) ) != 0; }
        /** @brief 이름으로 출력을 읽습니다. 모르는 포트면 false 입니다. */
        bool getOutput( int32 node, const hashed_string& port ) const { return getOutput( node, getKind( node ).findOutput( port ) ); }
        /** @brief 액추에이터가 오브젝트에 거는 값(상태 float[0] — 열림 · 거리 · 높이 · 각)입니다. 실수 상태가 없으면 0 입니다. */
        float32 getActuatorValue( int32 node ) const;
        float32 getFloatState( int32 node, int32 slot ) const;
        int32   getIntState( int32 node, int32 slot ) const;
        /** @brief 해석된 매개변수입니다. 종류에 없는 이름이면 nullptr 입니다. */
        const GimmickParamValue* findParam( int32 node, const hashed_string& name ) const;

        /** @brief 상태 전체(걸음 번호 · 누적 시간 · 노드 출력 · 지난 입력 · 센서 값 · 상태 칸)를 바이트로 씁니다. */
        void saveState( vector<uint8>& outBytes ) const;
        /** @brief `saveState` 의 바이트를 읽습니다. 머리 · 회로 모양(`getLayoutHash`) · 크기가 맞지 않으면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool loadState( const vector<uint8>& bytes );
        /** @brief 상태의 64 비트 해시입니다(같은 입력이면 같은 값 — 결정성 · 비동기 감지). */
        uint64 computeStateHash() const;

        bool    isBuilt() const { return _listNode.empty() == false; }
        uint32  getStepIndex() const { return _stepIndex; }
        float32 getStepTime() const { return _stepTime; }
        /** @brief 노드 종류 · id · 상태 크기로 정한 회로 모양의 해시입니다. 저장된 상태가 이 회로의 것인지 가립니다. */
        uint64 getLayoutHash() const { return _layoutHash; }

    private:
        /** @brief 노드 하나의 실행 정보입니다. */
        struct NodeRuntime
        {
            hashed_string          _id{};
            const GimmickNodeKind* _pKind{ nullptr };
            uint32                 _paramStart{ 0 };
            uint32                 _floatStart{ 0 };
            uint32                 _intStart{ 0 };
            uint32                 _wireStart{ 0 }; ///< 이 노드로 들어오는 배선(`_listInputWire`)의 시작
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
        void               computeLayoutHash();

        vector<NodeRuntime>       _listNode;
        vector<InputWire>         _listInputWire;
        vector<GimmickParamValue> _listParam;
        vector<float32>           _listFloatState;
        vector<int32>             _listIntState;
        vector<uint32>            _listOrder; ///< 평가 순서(고리를 끊는 노드가 먼저)
        vector<uint8>             _initialStateBytes;
        FixedStepTimer            _timer;
        uint64                    _layoutHash;
        uint32                    _stepIndex;
        float32                   _stepTime;
    };
} // namespace sw
