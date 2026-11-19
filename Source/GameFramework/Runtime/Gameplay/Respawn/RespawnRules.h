/**
 * @file RespawnRules.h
 * @brief 부활 규칙(데이터)입니다. 차례로 시도할 부활 정책 목록, 부활 대기, 부활 뒤 무적, 벌칙(통화 · 경험치 · 내구도)을 담습니다.
 * @details 장르 카탈로그가 `<Respawn delay="3" invulnerableTime="2" penaltyCurrency="gold" penaltyCurrencyRatio="0.1"><Policy kind="InPlace" maxUses="1" safeRadius="6"/>
 *          <Policy kind="LastCheckpoint"/></Respawn>` 을 품고 `readXML` 로 읽는다. 언리얼 `AGameModeBase::RestartPlayer` 의 자리 고르기(`ChoosePlayerStart`)를
 *          코드가 아니라 데이터 목록으로 둔 것이다 — 소울류(마지막 화톳불), 플랫포머(체크포인트), 슈터(팀 거점 · 스폰 고르기), MMO(제자리 · 마을)가 목록만 바꾼다.
 *          동료 부활은 정책이 아니다 — 기절 · 부활 진행은 `HealthStateMachine` 이 갖고, 부활 흐름은 죽음에서만 시작한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 부활 자리를 고르는 정책입니다. 값은 원격 호출 인자(`RespawnRemoteTarget::requestRespawn`)이기도 합니다. */
    ENUM( Count = Count )
    enum class RespawnPolicyKind : uint8
    {
        LastCheckpoint = 0, ///< 지금 부활 지점인 체크포인트(`CheckpointState::getCurrent`)
        InPlace,            ///< 죽은 자리 — 위험 · 막힘 · 낭떠러지면 `safeRadius` 안의 가까운 안전 지점(`IRespawnWorld::findSafeLocation`)
        NearestSpawnPoint,  ///< 죽은 자리에서 가장 가까운 스폰 지점(내 팀 또는 모든 팀 지점)
        TeamBase,           ///< 내 팀의 첫 스폰 지점(팀 거점 · 본진)
        PlayerChoice,       ///< 플레이어가 켠 체크포인트 · 스폰 지점 하나를 고른다(서버가 판정). 시한이 지나면 다음 정책으로 넘어간다
        Count
    };

    /** @brief 정책 하나와 그 정책의 횟수 · 비용 · 범위입니다. `<Policy kind="InPlace" maxUses="1" costCurrency="gold" costAmount="50" safeRadius="6"/>`. */
    REFLECT()
    struct SW_GF_API RespawnPolicyEntry
    {
        REFLECT_BODY();

        PROPERTY( Required )
        RespawnPolicyKind _kind{ RespawnPolicyKind::LastCheckpoint };
        PROPERTY()
        hashed_string _costCurrency{}; ///< 이 정책으로 부활할 때 내는 통화(제자리 부활 비용). 비면 비용이 없다
        PROPERTY( Min = 0 )
        int32 _costAmount{ 0 };
        PROPERTY( Min = 0 )
        int32 _maxUses{ 0 }; ///< 이 정책으로 부활할 수 있는 횟수(흐름을 `reset` 할 때까지). 0 이면 제한이 없다
        PROPERTY( Min = 0.0 )
        float32 _safeRadius{ 0.0f }; ///< `InPlace` — 죽은 자리가 안전하지 않을 때 안전 지점을 찾는 반경(m). 0 이면 그 자리만 본다
        PROPERTY( Min = 0.0 )
        float32 _choiceTimeout{ 0.0f }; ///< `PlayerChoice` — 이 초 안에 고르지 않으면 다음 정책으로 넘어간다. 0 이면 고를 때까지 기다린다
        PROPERTY()
        bool _bRequestOnly{ false }; ///< 저절로 시도하지 않고 플레이어 요청(`PlayerChoice` 단계)으로만 쓴다 — MMO 의 "제자리에서 부활(비용)" 단추
    };

    /**
     * @brief 부활 규칙입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 정책 목록 · 칸 사이 규칙은 `describeErrors` 가 봅니다.
     * @details 벌칙 통화는 부활 때 잔액 × 비율 + 고정 값(잔액까지)을 빼고, 경험치 · 내구도 비율은 그 체계를 가진 게임이 결과(`RespawnSpawn`)를 보고 적용한다.
     */
    REFLECT()
    struct SW_GF_API RespawnRules
    {
        REFLECT_BODY();

        vector<RespawnPolicyEntry> _listPolicy{}; ///< 차례로 시도한다(자식 원소 `<Policy>`). 비면 시작을 거절한다
        PROPERTY( Min = 0.0 )
        float32 _delay{ 0.0f }; ///< 죽은 뒤 자리를 고르기 시작할 때까지(초)
        PROPERTY( Min = 0.0 )
        float32 _invulnerableTime{ 0.0f }; ///< 부활 뒤 피해를 받지 않는 시간(초, 스폰 보호)
        PROPERTY()
        hashed_string _penaltyCurrency{}; ///< 벌칙 통화. 비면 통화 벌칙이 없다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _penaltyCurrencyRatio{ 0.0f };
        PROPERTY( Min = 0 )
        int32 _penaltyCurrencyAmount{ 0 };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _penaltyExperienceRatio{ 0.0f }; ///< 잃는 경험치 비율 — 게임이 적용한다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _penaltyDurabilityRatio{ 0.0f }; ///< 잃는 장비 내구도 비율 — 게임이 적용한다

        /** @brief 정책 종류의 첫 항목 자리입니다. 없으면 −1 입니다. */
        [[nodiscard]] int32 findPolicyIndex( RespawnPolicyKind kind ) const;
        /**
         * @brief 규칙이 틀린 까닭을 "; " 로 이어 반환합니다. 맞으면 빈 글입니다.
         * @details 범위 밖 칸, 빈 정책 목록, 같은 정책 두 번(요청이 종류로 항목을 찾는다), 통화 없는 비용 · 벌칙, 저절로 시도할 정책이 없는 목록을 본다.
         */
        [[nodiscard]] string describeErrors() const;
        /** @brief `<Respawn>` 원소(속성 + 자식 `<Policy>`)를 읽습니다. 틀린 칸 · 정책 · 규칙이면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool readXML( const XMLNode& node, string_view sourceName, RespawnRules& outRules );
    };
} // namespace sw
