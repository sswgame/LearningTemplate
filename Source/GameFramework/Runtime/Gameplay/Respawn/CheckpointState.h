/**
 * @file CheckpointState.h
 * @brief 체크포인트(세이브 포인트) 상태입니다. 켠 체크포인트(id · 자리 · 요 · 순번)와 지금 부활 지점을 담습니다.
 * @details 켜는 입구는 `activate` 하나다 — 트리거 진입 · 상호작용(화톳불에 앉기) · 스크립트가 모두 이것을 부른다. 다시 쓸 수 있는지는 정의(`CheckpointDef::_bReusable`)가,
 *          앞으로만 가는지(플랫포머 — 지난 체크포인트로 되돌아가지 않음)는 상태 규칙(`setForwardOnly`)이 정한다.
 *          반사 구조체라 주인이 `PROPERTY( RuntimeState )` 칸으로 두면 주인의 상태 바이트(세이브 · 스냅숏 · 핫 리로드)에 그대로 실린다 — 새 세이브 형식을 만들지 않는다.
 *          칸 사이 규칙(자리 범위 · 겹친 id · 앞으로만의 순번)은 주인이 읽은 뒤 `isConsistent` 로 본다(안쪽 반사 구조체는 범위만 검사된다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 체크포인트 정의 하나입니다(장르 카탈로그 · 씬 데이터가 준다). */
    REFLECT()
    struct SW_GF_API CheckpointDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 0 )
        int32 _order{ 0 }; ///< 지나가는 순서(앞으로만 가는 상태가 본다)
        PROPERTY()
        bool _bReusable{ false }; ///< 켠 뒤 다른 곳으로 옮겨 가도 다시 닿으면 부활 지점이 된다(화톳불). 아니면 한 번뿐이다
    };

    /** @brief 켠 체크포인트 하나입니다. */
    REFLECT()
    struct SW_GF_API CheckpointRecord
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        float3 _position{};
        PROPERTY()
        float32 _yaw{ 0.0f };
        PROPERTY( Min = 0 )
        int32 _order{ 0 };
    };

    /** @brief `CheckpointState::activate` 의 결과입니다. */
    enum class CheckpointActivation : uint8
    {
        Activated = 0,  ///< 처음 켰고 부활 지점이 됐다
        Reused,         ///< 전에 켠 다시 쓸 수 있는 체크포인트가 다시 부활 지점이 됐다
        AlreadyCurrent, ///< 이미 지금 부활 지점이다
        Spent,          ///< 한 번뿐인 체크포인트를 이미 썼다(부활 지점이 다른 곳으로 옮겨 갔다)
        Behind,         ///< 앞으로만 가는 상태에서 지금보다 앞쪽(작은 순번) 체크포인트다
        Invalid         ///< 이름이 없다
    };

    /**
     * @class CheckpointState
     * @brief 켠 체크포인트 목록과 지금 부활 지점입니다.
     */
    REFLECT()
    class SW_GF_API CheckpointState
    {
    public:
        REFLECT_BODY();

        CheckpointState();

        /** @brief 앞으로만 가는지 정합니다. 규칙이라 상태 바이트에 싣지 않으며, 읽기 전에 주인이 정합니다(카탈로그를 묶듯이). */
        void setForwardOnly( bool bForwardOnly ) { _bForwardOnly = bForwardOnly ? SW_TRUE : SW_FALSE; }
        bool isForwardOnly() const { return _bForwardOnly == SW_TRUE; }

        /** @brief 체크포인트에 닿았음을 알립니다. 부활 지점이 됐으면 `Activated` · `Reused` 입니다. 자리 · 요는 처음 켤 때의 것을 남깁니다. */
        CheckpointActivation activate( const CheckpointDef& def, const float3& position, float32 yaw );
        /** @brief 켠 것을 모두 지웁니다(스테이지를 처음부터). 규칙은 그대로입니다. */
        void reset();

        /** @brief 지금 부활 지점입니다. 켠 것이 없으면 nullptr 입니다. */
        const CheckpointRecord* getCurrent() const;
        /** @brief 켠 체크포인트를 id 로 찾습니다. */
        const CheckpointRecord*         find( const hashed_string& id ) const;
        const vector<CheckpointRecord>& getActivated() const { return _listActivated; }
        int32                           getCurrentIndex() const { return _currentIndex; }

        /** @brief 칸 사이 규칙(지금 자리가 목록 안 · 켠 것이 있으면 지금 자리가 있음 · id 가 비지 않고 겹치지 않음 · 앞으로만이면 순번이 오르고 지금 자리가 마지막)을 지키면 true 입니다. */
        bool isConsistent() const;

        /** @brief 켠 목록과 지금 자리(리플렉션 상태 칸)를 씁니다. 읽은 바이트가 `isConsistent` 를 어기면 거절하고 상태는 그대로입니다. */
        SW_DECLARE_STATE_IO();

    private:
        PROPERTY( RuntimeState )
        vector<CheckpointRecord> _listActivated; ///< 켠 순서
        PROPERTY( RuntimeState, Min = -1 )
        int32 _currentIndex; ///< 지금 부활 지점의 목록 자리(−1 이면 없음)
        uint8 _bForwardOnly; ///< 규칙 — 상태 바이트에 싣지 않는다
    };
} // namespace sw
