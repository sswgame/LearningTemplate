/**
 * @file PoseModifierWatches.cpp
 * @brief 리그(포즈 모디파이어)의 자동화 감시 값입니다. `Rig.Output` — 대상 오브젝트 리그의 노드 출력(`key` = `<노드>.<출력>`, 예: `Feet.foot_offset_0`).
 * @details 노드 출력은 `RigNode::getOutputValue` 가 정한다(발 디딤의 `pelvis_offset` · `foot_offset_<번호>` · `ground_hits`). 게임마다 감시 값을 따로 두지 않고 이 하나로 읽는다.
 */
#include "pch.h"

#include "Core/Name/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    namespace
    {
        struct PoseModifierWatchesInternal
        {
            /** @brief `Rig.Output` 의 몸통입니다. 대상 · 리그 · 키 모양(`노드.출력`) · 노드 출력 중 하나가 없으면 까닭을 적고 false 입니다. */
            [[nodiscard]] static bool readRigOutput( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*            pTarget = query.findTargetObject();
                const PoseModifierComponent* pRig    = pTarget != nullptr ? pTarget->getComponent<PoseModifierComponent>() : nullptr;
                if ( pRig == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "PoseModifierComponent" ) );
                    return false;
                }
                const size_t dot = query._key.find( '.' );
                if ( dot == string_view::npos || dot == 0 || dot + 1 >= query._key.size() )
                {
                    outValue.setText( "key must be '<node>.<output>'" );
                    return false;
                }
                float32 value = 0.0f;
                if ( pRig->getNodeOutputValue( hashed_string( query._key.substr( 0, dot ) ), hashed_string( query._key.substr( dot + 1 ) ), value ) == false )
                {
                    outValue.setText( pRig->isRigReady() ? query.describeMissing( "that rig node output" ) : query.describeMissing( "a bound rig" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( value ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( rigOutput, "Rig.Output", "Output named key ('<node>.<output>') of the rig node on target, from the last evaluation", &PoseModifierWatchesInternal::readRigOutput, true,
                               true );
} // namespace sw
