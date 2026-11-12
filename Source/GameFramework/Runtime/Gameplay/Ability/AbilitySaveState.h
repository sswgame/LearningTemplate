/**
 * @file AbilitySaveState.h
 * @brief 어빌리티 컴포넌트의 런타임 상태(어트리뷰트 base · 걸린 이펙트 · 이펙트 밖 태그)를 상태 스냅샷에 싣는 바이트 본문을 정의합니다.
 * @details 컴포넌트(`AbilityComponent::serializeState`)가 지금 상태를 이 형식으로 모아 쓰고, 읽을 때는 끝까지 맞은 본문만 한 번에 적용한다.
 *          언리얼 GAS 는 이 일을 게임에 맡기고(이펙트는 남은 시간으로 다시 건다), 유니티는 길이 없다 — 여기서는 프레임워크가 모든 게임에 정적 등록으로 싣는다
 *          (게임마다 적으면 빠뜨린 게임만 불러오기로 체력 · 쿨다운이 회복된다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityTypes.h"

namespace sw
{
    /** @brief 이름 하나와 수 하나입니다(어트리뷰트 base, SetByCaller, 쏜 쪽 어트리뷰트 스냅샷). */
    struct SW_GF_API SavedAttributeValue
    {
        hashed_string _attribute{};
        float32       _value{ 0.0f };

        void serializeState( StateIO& io );
    };

    /** @brief 걸린 이펙트가 적용 순간 계산해 둔 모디파이어 하나입니다(스냅샷이라 다시 계산하지 않는다). */
    struct SW_GF_API SavedAbilityModifier
    {
        hashed_string  _attribute{};
        AttributeModOp _op{ AttributeModOp::Add };
        float32        _magnitude{ 0.0f };

        void serializeState( StateIO& io );
    };

    /**
     * @brief 걸린 지속 · 무한 이펙트 하나입니다. 정의는 카탈로그 id 로만 가리킵니다.
     * @details 건 쪽 핸들(`AbilityEffectContext::_instigator`)은 싣지 않는다 — 오브젝트 핸들은 실행마다 새로 나가 다른 실행에서는 엉뚱한 오브젝트를 가리킨다.
     *          그래서 불러온 주기 피해로 쓰러지면 처치자가 없다. 맞힌 자리(`_hit`)도 즉시 이펙트만 쓰므로 싣지 않는다.
     */
    struct SW_GF_API SavedAbilityEffect
    {
        hashed_string                _effectID{};
        hashed_string                _sourceName{}; ///< 건 길(`removeActiveEffectsFromSource` 가 이것으로 지운다)
        int32                        _level{ 1 };
        int32                        _stackCount{ 1 };
        float32                      _duration{ 0.0f };
        float32                      _remainingTime{ 0.0f };
        float32                      _periodTimer{ 0.0f };
        float32                      _startTime{ 0.0f };
        vector<SavedAttributeValue>  _listSetByCaller{};
        vector<SavedAttributeValue>  _listSourceAttribute{};
        vector<string>               _listDynamicGrantedTag{};
        vector<SavedAbilityModifier> _listModifier{};

        void serializeState( StateIO& io );
    };

    /** @brief 이펙트 · 실행 중 어빌리티가 주지 않은 태그 하나와 그 개수입니다(어빌리티 세트의 태그, `State.Dead`, 치트). 태그는 글로 싣는다(`TagID` 의 직렬화 규칙). */
    struct SW_GF_API SavedAbilityTag
    {
        string _tag{};
        int32  _count{ 0 };

        void serializeState( StateIO& io );
    };

    /**
     * @brief 어빌리티 컴포넌트 하나의 상태 본문입니다.
     * @details 부여한 어빌리티는 싣지 않는다 — 어빌리티 세트(`_abilitySetID`)가 플레이 시작에 다시 주고, 실행 중이던 어빌리티는 이어 가지 않는다(GAS 도 같다).
     */
    struct SW_GF_API AbilitySaveState
    {
        static constexpr uint32 kStateTag     = FourCCUtil::make( "ABIL" );
        static constexpr uint32 kStateVersion = 1;

        float32                     _time{ 0.0f };
        vector<SavedAttributeValue> _listBaseValue{}; ///< 붙인 세트 순서
        vector<SavedAbilityEffect>  _listEffect{};    ///< 적용 순서
        vector<SavedAbilityTag>     _listLooseTag{};

        void serializeState( StateIO& io );
    };
} // namespace sw
