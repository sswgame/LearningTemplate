/**
 * @file Test/TestFramework/TestPropertyCoverage.h
 * @brief 지금 등록된 모든 PROPERTY 가 직렬화기가 실어 나를 수 있는 타입인지 — 이 판정을 부르는 시험이 둘이라 한 자리에 둔다.
 * @details ReflectionTest 는 엔진과 자기 시험 타입을, SmokeTest 는 모듈(GameFramework · 킷 · 게임 · 에디터)을 올린 뒤 같은 판정을 돈다.
 *          예전에는 ReflectionTest 하나뿐이라 모듈 타입의 PROPERTY 는 아무도 보지 않았다(2026-10-03).
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace test
{
    /** @brief 한 번 훑은 결과. */
    struct PropertyCarryReport
    {
        uint32     _typeCount{ 0 };    ///< 훑은 타입 수
        uint32     _checkedCount{ 0 }; ///< 본 PROPERTY 수(`Transient` 는 세지 않는다)
        sw::string _offender;          ///< 실어 나를 수 없는 것 — 한 줄에 `타입::이름 (타입 이름)` 하나. 없으면 비어 있다
    };

    /**
     * @brief 엔진 타입 등록부의 모든 타입 · 모든 PROPERTY 를 `SerializerUtil::canCarryProperty` 로 봅니다(`Transient` 는 건너뜁니다).
     * @details 직렬화기는 다룰 줄 모르는 타입을 조용히 텍스트 `null` · 바이너리 0 바이트로 써, 읽으면 그 칸이 기본값이 됐다. 저장할 수 없는
     *          런타임 값(창 핸들 같은 포인터)은 `Transient` 로 적는다.
     */
    PropertyCarryReport makePropertyCarryReport();
} // namespace test
