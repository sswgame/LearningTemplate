/**
 * @file GimmickCircuitDesc.h
 * @brief 기믹 회로 컴포넌트(`GimmickCircuitComponent`)가 저장하는 노드 · 배선 한 줄입니다. 값만 담아 컴포넌트 헤더 없이 읽힌다(기믹 그래프 창의 모델이 이것만 본다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 저장되는 노드 하나입니다. id, 종류, 매개변수 문자열("openTime=0.5; openOffset=0 3 0")을 담습니다. */
    REFLECT()
    struct SW_GF_API GimmickNodeDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Node id, unique in this circuit" )
        hashed_string _id{};
        PROPERTY( Tooltip = "Node kind name (GimmickNodeRegistry)" )
        hashed_string _kind{};
        PROPERTY( Meta = "NotLocalizable", Tooltip = "Parameters as name=value pairs separated by ';'" )
        string _params{};
        /** @brief 기믹 그래프 창 캔버스의 자리입니다. 회로는 읽지 않는다(언리얼 블루프린트 노드 좌표처럼 편집 배치 데이터다). 모두 (0, 0) 이면 창이 처음 배치를 한다. */
        PROPERTY( Tooltip = "Position of the node on the gimmick graph canvas; the circuit does not read it" )
        float2 _editorPosition{ 0.0f, 0.0f };
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
