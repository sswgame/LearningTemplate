/**
 * @file RTSCommand.h
 * @brief 사람이 내리는 RTS 명령 하나를 값으로 담습니다. 화면이 월드에 바로 내리든 락스텝 입력으로 보내든 같은 값이 같은 길(`RTSWorld::applyCommand`)로 들어갑니다.
 * @details - 락스텝(`LockstepSession`)은 틱마다 플레이어의 입력 바이트만 모두에게 나눈다. 명령을 값으로 두면 보낸 쪽 · 받은 쪽이 같은 틱에 같은 명령을 같은 순서로 적용해
 *            월드가 결정적으로 같다(스타크래프트 · 에이지 오브 엠파이어의 명령 스트림). 화면이 월드 함수를 바로 부르던 모양은 혼자 할 때만 맞았다.
 *          - 바이트는 `StateIO` 로 쓰고 읽는다. 판 번호(`kFormatVersion`)가 첫 칸이라 다른 빌드의 입력은 거절된다. 유닛 id 는 슬롯 핸들의 묶은 값이다.
 *          - 적용은 명령을 낸 플레이어의 유닛에만 한다(`RTSWorld::applyCommand` 가 주인을 본다). 망을 건너온 입력이 남의 유닛을 움직이지 못한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
    /** @brief 명령 종류입니다. 바이트에 들어가므로 새 종류는 `Count` 앞에만 더하고 `RTSCommand::kFormatVersion` 을 올립니다. */
    ENUM( Count = Count )
    enum class RTSCommandKind : uint8
    {
        Smart = 0, ///< 오른쪽 클릭 — 유닛마다 `issueSmart`(목표 유닛이 있으면 그것)
        GroupMove, ///< 무리 이동 · 공격 이동(`issueGroupMove`)
        Hold,      ///< 유닛마다 `issueHold`
        Stop,      ///< 유닛마다 `issueStop`
        Build,     ///< 첫 유닛(일꾼)이 `_cell` 에 `_defID` 를 짓는다
        Train,     ///< 첫 유닛(건물)이 `_defID` 를 생산 대기열에 넣는다
        Rally,     ///< 건물마다 집결지를 `_target` 으로 둔다
        Count,
    };

    /**
     * @struct RTSCommand
     * @brief 명령 하나입니다. 종류마다 쓰는 칸만 채웁니다.
     */
    struct SW_KIT_API RTSCommand
    {
        static constexpr uint32 kFormatVersion = 1;
        static constexpr int32  kMaxUnitCount  = 64; ///< 명령 하나가 담는 유닛 상한(락스텝 입력 한 틱 상한 `NetLockstepMessage::kMaxInputBytes` 안에 든다)

        vector<RTSUnitID> _listUnit{};
        float3            _target{};
        RTSUnitID         _targetID{};
        hashed_string     _defID{}; ///< Build 는 건물, Train 은 유닛 카탈로그 id
        int2              _cell{ constant::invalid_index::kInt32, constant::invalid_index::kInt32 };
        RTSCommandKind    _kind{ RTSCommandKind::Smart };
        uint8             _bQueue{ SW_FALSE };      ///< 명령 대기열 뒤에 붙인다(쉬프트)
        uint8             _bAttackMove{ SW_FALSE }; ///< GroupMove 가 공격 이동이다

        void serializeState( StateIO& io );

        /** @brief 명령 목록을 바이트 하나로 씁니다(판 번호 · 개수 · 명령들). 빈 목록도 판 번호와 개수 0 을 씁니다. */
        static void writeList( const vector<RTSCommand>& listCommand, vector<uint8>& outBytes );
        /** @brief `writeList` 의 바이트를 읽습니다. 빈 바이트는 명령 없음(이탈한 플레이어의 빈 입력)입니다. 판이 다르거나 깨졌으면 false 이고 목록은 비어 있습니다. */
        [[nodiscard]] static bool readList( const vector<uint8>& bytes, vector<RTSCommand>& outListCommand );
    };
} // namespace sw
