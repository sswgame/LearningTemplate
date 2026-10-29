/**
 * @file UIAccessibilityTypes.h
 * @brief 위젯이 스크린 리더(운영체제 접근성 API)에 내보이는 값입니다: 역할, 상태 비트, 이름 · 값 · 범위.
 * @details 위젯이 자기 모습을 `Widget::describeAccessible` 로 이 값에 적고, 접근성 트리(`UIAccessibilityTree`)가 화면의 위젯 트리에서 읽어 모은다.
 *          역할 · 상태는 UI 자동화(Windows UI Automation, Linux AT-SPI)가 공통으로 가진 것만 둔다 — 플랫폼 번호로의 바꿈은 각 플랫폼 브리지가 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 위젯의 접근성 역할입니다. `None` 은 읽을 것이 없는 구조(여백 패널, 장식 그림)라 접근성 트리에서 빠진다. */
    enum class AccessibleRole : uint8
    {
        None,        ///< 접근성 트리에 넣지 않습니다(자손은 가장 가까운 조상 아래로 올라온다).
        Group,       ///< 이름을 가진 묶음입니다(`accessibleName` 을 준 패널).
        Text,        ///< 글자입니다.
        Image,       ///< 이름을 준 그림입니다.
        Button,      ///< 누르는 단추입니다.
        CheckBox,    ///< 켜고 끄는 상자입니다.
        Slider,      ///< 값을 끄는 슬라이더입니다.
        ProgressBar, ///< 진행 막대입니다(읽기 전용 범위).
        ComboBox,    ///< 팝업으로 하나를 고르는 상자입니다.
        SpinButton,  ///< 좌우로 하나씩 넘기는 선택기(로테이터)입니다.
        TextInput,   ///< 글 입력 칸입니다.
        List         ///< 목록입니다.
    };

    /** @brief `AccessibleInfo::_states` 의 비트입니다. */
    namespace AccessibleState
    {
        inline constexpr uint32 kDisabled  = 1u << 0; ///< 자기나 조상이 꺼졌습니다.
        inline constexpr uint32 kFocusable = 1u << 1; ///< 포커스를 받을 수 있습니다.
        inline constexpr uint32 kFocused   = 1u << 2; ///< 포커스를 가졌습니다.
        inline constexpr uint32 kChecked   = 1u << 3; ///< 켜졌습니다(체크 상자).
        inline constexpr uint32 kPressed   = 1u << 4; ///< 눌려 있습니다.
        inline constexpr uint32 kReadOnly  = 1u << 5; ///< 값을 바꿀 수 없습니다(진행 막대).
    } // namespace AccessibleState

    /** @brief 위젯 하나가 스크린 리더에 내보이는 내용입니다. */
    struct AccessibleInfo
    {
        AccessibleRole _role{ AccessibleRole::None };
        uint32         _states{ 0 };        ///< `AccessibleState` 비트입니다. 꺼짐 · 포커스는 접근성 트리가 채운다.
        string         _name{};             ///< 읽을 이름입니다(비면 이름 없음). 사람이 읽는 글이라 현지화된 글이어야 한다.
        string         _value{};            ///< 읽을 값입니다(입력 칸의 글, 고른 항목). 범위 값은 `_bHasRange` 로 따로 낸다.
        float32        _rangeMin{ 0.0f };   ///< 범위 값의 최솟값입니다.
        float32        _rangeMax{ 0.0f };   ///< 범위 값의 최댓값입니다.
        float32        _rangeValue{ 0.0f }; ///< 범위 값입니다.
        float32        _rangeStep{ 0.0f };  ///< 한 번에 바꾸는 크기입니다(0 이면 정하지 않았다).
        bool           _bHasRange{ false }; ///< 범위 값을 가졌는지 나타냅니다(슬라이더 · 진행 막대).
        bool           _bLeaf{ false };     ///< 안의 위젯을 따로 읽지 않는지 나타냅니다(단추 안의 글은 단추의 이름이다).
    };

    /** @brief 알림(`UIAccessibilityService::announce`)이 끼어드는 정도입니다. */
    enum class UIAnnouncePriority : uint8
    {
        Polite,   ///< 읽고 있던 것이 끝난 뒤에 읽습니다(저장했습니다, 힌트).
        Assertive ///< 읽던 것을 끊고 바로 읽습니다(경고).
    };
} // namespace sw
