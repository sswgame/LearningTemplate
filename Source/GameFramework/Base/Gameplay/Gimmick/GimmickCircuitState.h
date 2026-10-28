/**
 * @file GimmickCircuitState.h
 * @brief 기믹 회로 하나의 실행 상태입니다. 같은 `REFLECT` 선언 하나가 두 형식을 냅니다. 씬, 프리팹, 레벨 세이브는 자기 기술형(A)으로, 롤백과 넷 스냅샷은 고정 배치형(B, `FixedLayout`)으로 씁니다.
 * @details 노드 상태는 **노드 id 로 짝짓는다**(`GimmickCircuit::loadState`). 그래서 디자이너가 노드를 더하거나 지워도
 *          옛 세이브의 나머지 노드는 이어지고, 새 노드는 처음 상태로 시작한다. 같은 id 인데 종류나 종류의 상태 버전(`GimmickNodeKind::_stateVersion`), 슬롯 수가 다르면
 *          그 상태는 읽지 않는다(`GimmickStateMatch::Mismatch`). 슬롯의 뜻을 기본값으로 메울 수 없기 때문이다.
 *
 *          개수 상한은 B 가 요구하는 정적 상한이다. 기준은 C++ 상수(`kMaxNodeCount` · `kMaxSlotCount`) 하나이고, `PROPERTY( MaxCount = kMaxSlotCount )` 처럼
 *          그 이름을 적으면 생성기가 상수의 값을 코덱과 `PropertyMetadata::_maxCount` 에 넣는다(`Source/Engine/Serialization/Fixed/README.md` 3.5). 숫자를 두 곳에 적지 않는다.
 *          회로를 만들 때(`GimmickCircuit::populate`)와 종류를 등록할 때(`GimmickNodeRegistry::registerKind`) 이 상수로 막으므로, 만들어진 회로의 상태는 늘 B 로 쓸 수 있다.
 *
 *          구조가 바뀔 때의 규칙은 계획 문서 2.6 이다. 필드 추가는 선언한 기본값, 이름 변경은 `PROPERTY( Alias = "old" )`, 값 계산이 필요한 변환은
 *          저장소의 데이터를 다시 쓴다(이관 함수는 없다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 노드 하나의 실행 상태입니다. 상태 슬롯은 노드마다 따로 보관하고, 종류가 정한 개수(`GimmickNodeKind::_floatStateCount`, `_intStateCount`)와 같습니다. */
    REFLECT()
    struct SW_GF_API GimmickNodeState
    {
        REFLECT_BODY();

        /** @brief 종류 하나가 가질 수 있는 실수 · 정수 슬롯 수의 상한입니다. 아래 두 목록의 `MaxCount` 가 이 상수입니다. */
        static constexpr uint32 kMaxSlotCount = 32;

        PROPERTY()
        uint32 _id{ 0 }; ///< 노드 id 의 해시(`hashed_string::getHash`). 저장 상태를 회로의 노드와 짝짓는 키다
        PROPERTY()
        uint32 _kind{ 0 }; ///< 종류 이름의 해시. 같은 id 인데 종류가 바뀌었는지 본다
        PROPERTY()
        uint32 _outputBits{ 0 }; ///< 포트가 32 개 이하(`GimmickNodeRegistry::registerKind`)라 비트 마스크 하나다
        PROPERTY()
        uint32 _previousInputBits{ 0 };
        PROPERTY()
        float32 _sensorValue{ 0.0f };
        PROPERTY()
        float32 _sensorImpulse{ 0.0f };
        PROPERTY()
        uint16 _stateVersion{ 0 }; ///< 저장할 때 종류의 상태 버전(`GimmickNodeKind::_stateVersion`). 다르면 슬롯의 뜻이 바뀐 것이다
        PROPERTY( MaxCount = kMaxSlotCount )
        vector<float32> _listFloatState{};
        PROPERTY( MaxCount = kMaxSlotCount )
        vector<int32> _listIntState{};
    };
} // namespace sw

namespace sw
{
    /** @brief 회로 하나의 상태입니다. 노드 목록이 비어 있으면 저장된 상태가 없다는 뜻입니다(만들어진 회로는 노드가 하나 이상입니다). */
    REFLECT()
    struct SW_GF_API GimmickCircuitState
    {
        REFLECT_BODY();

        /** @brief 회로 하나의 노드 수 상한입니다. `_listNode` 의 `MaxCount` 가 이 상수입니다. */
        static constexpr uint32 kMaxNodeCount = 1024;

        PROPERTY()
        uint32 _stepIndex{ 0 };
        /** @brief 고정 스텝 누적 시간입니다. `Step` 을 붙이면 손실 인코딩이 되어 롤백 결정성이 깨지므로 원본 비트로 씁니다. 위 경계는 회로의 스텝 시간이라 이 타입 밖의 값입니다. */
        PROPERTY( Min = 0 )
        float32 _accumulator{ 0.0f };
        PROPERTY( MaxCount = kMaxNodeCount )
        vector<GimmickNodeState> _listNode{};

        bool isEmpty() const { return _listNode.empty(); }
    };
} // namespace sw
