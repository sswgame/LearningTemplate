/**
 * @file GrowthLog.h
 * @brief 성장 기록입니다 — 경험치 · 레벨 · 스킬 랭크 · 얻은 것의 변화와 그 출처(무엇이 그것을 일으켰는지)를 고정 용량으로 남겨 성장 디버거(HUD 섹션)가 읽습니다.
 * @details 성장 상태는 키트마다 다른 규칙 객체(`LevelProgress` 를 가진 전장 유닛 · 파티 멤버 · 말 유대 · 스킬 트리)에 있다. 그 상태를 가진 쪽이
 *          일이 일어날 때 한 줄을 적고(프레임 비용 없음), 자기 HUD 섹션에서 지금 값과 이 기록을 함께 낸다 — 언리얼 Gameplay Debugger 의 카테고리를
 *          모듈마다 등록하는 나눔이다. 기록은 표시용이라 저장 · 넷에 들지 않고, 배포본은 적지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class DebugHUDSectionWriter;

    /** @brief 성장 기록 한 줄의 종류입니다. 이름은 리플렉션이 준다(`getEnumName`). */
    ENUM()
    enum class GrowthEventKind : uint8
    {
        Experience = 0, ///< 경험치를 얻었다(`_value` 는 얻은 양)
        LevelUp,        ///< 레벨이 올랐다(`_value` 는 새 레벨)
        SkillRank,      ///< 스킬 랭크가 바뀌었다(`_value` 는 새 랭크)
        Gained,         ///< 아이템 · 점수 같은 것을 얻었다(`_value` 는 양)
    };

    /** @brief 성장 기록 한 줄입니다. 글은 고정 길이로 잘라 담는다(할당 없음). */
    struct GrowthEvent
    {
        fixed_string<constant::buffer::kMax64> _subject{}; ///< 자란 대상(유닛 · 멤버 · 캐릭터 이름)
        fixed_string<constant::buffer::kMax64> _what{};    ///< 무엇이(파일럿 레벨 · 스킬 id · 아이템 id)
        fixed_string<constant::buffer::kMax64> _source{};  ///< 일으킨 것(명중 · 격파 · 퀘스트 보상 · 아이템 사용)
        int64                                  _value{ 0 };
        GrowthEventKind                        _kind{ GrowthEventKind::Experience };
    };

    /**
     * @class GrowthLog
     * @brief 최근 성장 기록(고정 용량 링)입니다. 넘치면 가장 오래된 줄을 덮고, 누적 수는 이어 센다. 게임 스레드에서만 씁니다.
     */
    class SW_GF_API GrowthLog
    {
    public:
        static constexpr uint32 kCapacity = 16;

        GrowthLog();

        /** @brief 한 줄을 적습니다. 배포본은 아무것도 하지 않는다. */
        void record( GrowthEventKind kind, string_view subject, string_view what, int64 value, string_view source );
        void clear();
        /** @brief 남은 줄 수입니다(최대 `kCapacity`). */
        uint32 getCount() const { return _count; }
        /** @brief 지금까지 적은 줄 수입니다(덮인 것 포함 — 감시 값 · 시나리오). */
        uint32 getRecordedCount() const { return _recordedCount; }
        /** @brief @p index 번째 줄입니다(0 이 가장 오래된 것). */
        const GrowthEvent& getEntry( uint32 index ) const;
        /** @brief 종류 @p kind 의 남은 줄 수입니다. */
        uint32 countKind( GrowthEventKind kind ) const;
        /** @brief 최근 것부터 줄을 씁니다(`t-<n> <종류> <대상> <무엇> <값> from <출처>`). @p maxLines 에서 끊는다. */
        void writeDebugLines( DebugHUDSectionWriter& writer, uint32 maxLines ) const;

    private:
        GrowthEvent _arrEntry[kCapacity];
        uint32      _recordedCount;
        uint32      _head; ///< 다음에 쓸 자리
        uint32      _count;
    };
} // namespace sw
