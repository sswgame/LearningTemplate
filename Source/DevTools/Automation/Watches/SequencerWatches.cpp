/**
 * @file SequencerWatches.cpp
 * @brief 시퀀서 재생기의 자동화 감시 값(프레임 · 기록 · 재생 속도 · 입력 막기 · 바인딩)입니다.
 * @details 기능 .cpp 의 등록 블록과 그 블록만 부르던 Internal 함수를 옮겼다 — 배포 엔진에 컴파일되지 않게 개발 도구가 짓는다. 본문은 기능 형식의 공개 조회만 읽는다.
 */
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Presentation/IUILayerVisibility.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

// Engine/Sequencer/SequencePlayerComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct SequencePlayerComponentWatchesInternal
        {
            static const SequencePlayerComponent* findTargetPlayer( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<SequencePlayerComponent>( "sequence player", outValue );
            }

            /** @brief 감시 값 `Sequence.BoundObject` 입니다 — 바인딩 id key 가 묶인 오브젝트의 이름이고, 묶이지 않았으면 `none` 입니다. */
            [[nodiscard]] static bool readBoundObject( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                int32 bindingID = 0;
                if ( StringUtil::parseInt( query._key, bindingID ) == false || bindingID <= 0 )
                {
                    outValue.setText( "key must be a binding id (1 or more)" );
                    return false;
                }
                const GameObject* pObject = pPlayer->findBoundObject( static_cast<uint32>( bindingID ) );
                outValue.setText( pObject != nullptr ? pObject->getName().c_str() : "none" );
                return true;
            }

            /** @brief 감시 값 `Sequence.Frame` 입니다 — 대상 플레이어의 지금 프레임입니다. */
            [[nodiscard]] static bool readFrame( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlayer->getCurrentFrame() ) );
                return true;
            }

            /** @brief 감시 값 `Sequence.History` 입니다 — 대상 플레이어의 기록에서 종류 key(`Event` · `CameraSignal` · `Finished` ...)의 줄 수입니다. */
            [[nodiscard]] static bool readHistory( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                for ( uint8 kindIndex = 0; kindIndex <= static_cast<uint8>( SequenceHistoryKind::Span ); ++kindIndex )
                {
                    const SequenceHistoryKind kind = static_cast<SequenceHistoryKind>( kindIndex );
                    if ( query._key == getEnumName( kind ) )
                    {
                        outValue.setNumber( static_cast<float64>( pPlayer->getHistory().countKind( kind ) ) );
                        return true;
                    }
                }
                outValue.setText( "key must be Started, Finished, Stopped, Event, CameraSignal, UnboundBinding or Span" );
                return false;
            }

            /** @brief 감시 값 `Sequence.InputBlocked` 입니다 — 대상 플레이어가 컷씬 레이어로 플레이어 입력(이동 · 시선 · 게임 동작)을 막고 있으면 1 입니다. */
            [[nodiscard]] static bool readInputBlocked( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( pPlayer->isBlockingPlayerInput() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Sequence.PlayRate` 입니다 — 지금 걸린 재생 속도(빨리감기 배율 포함, 음수 = 역방향 속도 값)입니다. */
            [[nodiscard]] static bool readPlayRate( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlayer->getEffectivePlayRate() ) );
                return true;
            }

            /** @brief 감시 값 `Sequence.Reversed` 입니다 — 지금 거꾸로 진행하면 1(속도 부호와 왕복이 뒤집은 방향)입니다. */
            [[nodiscard]] static bool readReversed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( pPlayer->isReversed() ? 1.0 : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( sequenceFrame, "Sequence.Frame", "Current frame of the target's sequence player", &SequencePlayerComponentWatchesInternal::readFrame, true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceHistory, "Sequence.History", "Lines of one kind (key: Started, Finished, Stopped, Event, CameraSignal, UnboundBinding, Span) in the target's recent sequence history",
                               &SequencePlayerComponentWatchesInternal::readHistory, true, true );
    SW_AUTOMATION_QUERY_WATCH( sequencePlayRate, "Sequence.PlayRate", "Play rate applied to the target's sequence player now (fast forward included; negative plays backwards)",
                               &SequencePlayerComponentWatchesInternal::readPlayRate, true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceReversed, "Sequence.Reversed", "1 while the target's sequence plays backwards (negative rate or a ping-pong turn)", &SequencePlayerComponentWatchesInternal::readReversed,
                               true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceInputBlocked, "Sequence.InputBlocked", "1 while the target's sequence player blocks player movement, look and game actions with the Cutscene input layer",
                               &SequencePlayerComponentWatchesInternal::readInputBlocked, true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceBoundObject, "Sequence.BoundObject", "Name of the object the target's sequence player binds to binding id key; none when unbound",
                               &SequencePlayerComponentWatchesInternal::readBoundObject, true, true );
} // namespace sw
