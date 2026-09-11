/**
 * @file ReflectionConstants.h
 * @brief Reflection 서브시스템 및 PropertyMetaHint 관련 공통 상수 및 데이터 테이블 정의.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::constants::reflection
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
    /** @brief 플래그 분리 구분자입니다. */
    inline constexpr const utf8* kFlagSplitDelimiter = "|";
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
} // namespace sw::constants::reflection

namespace sw::constants::propertyHint
{
    /** @brief Color 계열 타입 및 카테고리 식별자 목록입니다. */
    inline constexpr const utf8* kArrColorTypes[] = {
        "Color",
        "sw::Color",
        "LinearColor" };

    /** @brief bool 계열 타입 및 식별자입니다. */
    inline constexpr const utf8* kBool       = "bool";
    inline constexpr const utf8* kUint8      = "uint8";
    inline constexpr const utf8* kBoolPrefix = "b";

    // 에셋 타입별 **파일 다이얼로그 필터 표는 여기 두지 않는다.** 확장자는 에디터가 아는 것이고
    // (`Editor/Common/Workspace/EditorAssetType`), Engine 은 Editor 를 볼 수 없다 — 여기에 두면
    // 같은 목록이 두 벌이 되어 한쪽만 늙는다. 실제로 그랬다: 이 표는 `*.mat` 와 `*.glsl` 를
    // 광고하고 있었는데 저장소에 `.mat` 은 없고 엔진은 GLSL 을 컴파일하지 않는다.
    // 인스펙터의 에셋 필드는 드래그앤드롭 + 텍스트라 이 표를 **한 번도 읽지 않았다**(2026-09-12 삭제).
} // namespace sw::constants::propertyHint
