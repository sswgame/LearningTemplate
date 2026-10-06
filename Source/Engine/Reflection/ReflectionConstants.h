/**
 * @file ReflectionConstants.h
 * @brief Reflection 서브시스템이 쓰는 상수 · 데이터 테이블입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::constant::reflection
{
    /** @brief 기본 카테고리 이름입니다. */
    inline constexpr const utf8* kDefaultCategory = "General";
    /** @brief 기본 모듈 이름입니다. */
    inline constexpr const utf8* kDefaultModuleName = "Engine";
    /** @brief C++ 스코프 구분자입니다. */
    inline constexpr const utf8* kScopeDelimiter = "::";
    /** @brief 열거형 기본 폴백 이름입니다. */
    inline constexpr const utf8* kNone = "None";
    /** @brief 플래그 결합 구분자입니다. */
    inline constexpr const utf8* kFlagSeparator = " | ";
    /** @brief 맵 컨테이너 요소 멤버명입니다. */
    inline constexpr const utf8* kMappedType = "mapped_type";
    /** @brief 시퀀스 컨테이너 요소 멤버명입니다. */
    inline constexpr const utf8* kValueType = "value_type";
    /** @brief 타입 FQN 마커 접두어입니다. */
    inline constexpr const utf8* kTypeFqnPrefix = "typeFqn<";
    /** @brief 시그니처 등호 마커입니다. */
    inline constexpr const utf8* kSignatureEq = "E = ";
    /** @brief enum class 접두어입니다. */
    inline constexpr const utf8* kEnumClassPrefix = "enum class ";
    /** @brief enum struct 접두어입니다. */
    inline constexpr const utf8* kEnumStructPrefix = "enum struct ";
    /** @brief enum 접두어입니다. */
    inline constexpr const utf8* kEnumPrefix = "enum ";
    /** @brief class 접두어입니다. */
    inline constexpr const utf8* kClassPrefix = "class ";
    /** @brief struct 접두어입니다. */
    inline constexpr const utf8* kStructPrefix = "struct ";
    /** @brief 소규모 프로퍼티/메서드 목록 선형 탐색 임계값입니다. */
    inline constexpr size_t kLinearSearchThreshold = 4;
    /**
     * @brief 부모 체인을 걸을 때의 걸음 상한입니다.
     * @details `_parentFQN` 은 코드젠이 적는 값이지만 `registerClass` 는 공개 API 라 순환(A→B→A)을
     *          막지 못합니다. 방문 목록을 힙에 만들어 막는 대신 걸음 수를 세면, 캐스트 한 번에 할당이
     *          없고 순환이어도 여기서 멈춥니다. 실제 체인은 다섯을 넘지 않습니다.
     */
    inline constexpr uint32 kMaxParentChainDepth = 32;
    /**
     * @brief 조상 표의 칸 수입니다. 이 깊이까지의 타입은 상속 검사가 O(1) 입니다.
     * @details HotSpot 의 primary supers display 와 같은 방식입니다. 타입마다 루트부터 자기까지의 이름을
     *          깊이 순서로 적어 두면 "T 가 U 의 자손인가" 는 `표[U 의 깊이] == U 의 이름` 한 번입니다. 이보다
     *          깊은 사슬은 표 없이 부모 포인터를 걷습니다. 실제 사슬은 다섯을 넘지 않습니다.
     */
    inline constexpr uint32 kAncestorDisplayDepth = 8;
    /** @brief 조상 표를 아직 세우지 않았다는 깊이 표시입니다. 첫 상속 검사가 세웁니다. */
    inline constexpr uint8 kAncestorDepthUnknown = 0xFF;
    /** @brief 조상 표를 세울 수 없다는 깊이 표시입니다(이름 없음 · 순환 · 표보다 깊은 사슬). 등록 · 해제가 다시 비웁니다. */
    inline constexpr uint8 kAncestorDepthNone = 0xFE;
} // namespace sw::constant::reflection
