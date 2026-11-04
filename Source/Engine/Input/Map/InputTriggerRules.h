/**
 * @file InputTriggerRules.h
 * @brief 바인딩 하나의 프레임 사이 상태(`InputBindingState`)와, 그 상태를 눌림 입력으로 한 프레임 진행해 발화를 정하는 규칙(`InputTriggerRules`)입니다.
 * @details `InputMap` 에서 뗐다 — 맵은 장치를 읽어(레이어 · 콘솔 가림 · 수정 키) 눌림을 정하고, 홀드 · 탭 · 연타 · 끌기 · 반복 · 펄스 판정은 이 규칙이 한다
 *          (언리얼 Enhanced Input 의 `UInputTrigger` 가 값만 받아 상태를 판정하는 자리). 장치 · 레이어를 모르므로 테스트가 눌림 줄만으로 판정을 돌릴 수 있다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Input/Map/InputBinding.h"

namespace sw
{
    /**
     * @brief 바인딩 하나의 프레임 사이 상태입니다.
     * @details `_bPhysicalDown` 은 레이어가 꺼져 있거나 콘솔이 키를 가려도 매 프레임 장치를 읽어 갱신한다(`_bDown` 은 보이는 입력에서만 눌림이다).
     *          바인딩이 "보이게 되는" 프레임(`_bLive` 가 false 에서 true)에 이미 눌려 있던 입력이 새 누름(Pressed)으로 읽히지 않게 하는 근거다 — `InputTriggerRules::advance` 가 `_bWasDown` 을 맞춘다.
     *          레이어가 켜질 때, 콘솔이 닫혀 키가 다시 보일 때, 리바인딩 · 기본값 복원 · 새 바인딩으로 상태가 새로 만들어질 때(`InputMap::makeBindingState`)가 모두 이 한 규칙이다.
     */
    struct InputBindingState
    {
        float32                _holdDuration;
        float32                _timeSinceLastPress;
        float32                _pulseTimer;
        int2                   _lastPress; ///< 더블클릭 판정에 쓰는 직전 누름 위치입니다.
        uint8                  _tapStreak; ///< 간격 안에 이어진 누름 수입니다(DoubleTap · MultiTap 판정).
        uint8                  _bDown          : 1;
        uint8                  _bPressed       : 1;
        uint8                  _bReleased      : 1;
        uint8                  _bDoubleClicked : 1;
        uint8                  _bHoldThreshold : 1;
        uint8                  _bWasDown       : 1;
        uint8                  _bTriggered     : 1;
        uint8                  _bPhysicalDown  : 1; ///< 직전 프레임에 레이어 · 콘솔 가림과 상관없이 장치가 눌려 있었는지입니다.
        uint8                  _bLive          : 1; ///< 직전 프레임에 바인딩이 보였는지입니다(레이어가 켜져 있고, 콘솔이 눌린 키를 가리지 않음).
        uint8                  _bDragged       : 1; ///< 이번 누름 동안 포인터가 끌기 문턱을 넘었는지입니다(`Click` · `Drag`). 뗀 다음 프레임에 지운다.
        uint8                  _bDragStarted   : 1; ///< 이번 프레임에 끌기 문턱을 처음 넘었는지입니다(`Drag` 의 발화).
        [[maybe_unused]] uint8 _reserved       : 5;

        InputBindingState()
            : _holdDuration{ 0.0f }
            , _timeSinceLastPress{ InputMapDefaults::kNeverPressedSentinel }
            , _pulseTimer{ 0.0f }
            , _lastPress{}
            , _tapStreak{ 0 }
            , _bDown{ SW_FALSE }
            , _bPressed{ SW_FALSE }
            , _bReleased{ SW_FALSE }
            , _bDoubleClicked{ SW_FALSE }
            , _bHoldThreshold{ SW_FALSE }
            , _bWasDown{ SW_FALSE }
            , _bTriggered{ SW_FALSE }
            , _bPhysicalDown{ SW_FALSE }
            , _bLive{ SW_FALSE }
            , _bDragged{ SW_FALSE }
            , _bDragStarted{ SW_FALSE }
            , _reserved{ 0 } {}
    };

    /** @brief 바인딩 하나를 한 프레임 진행할 때 맵이 읽어 넘기는 눌림입니다. */
    struct InputBindingSample
    {
        int2 _mousePos{};             ///< 이번 프레임 포인터 위치(연타 거리 · 끌기 문턱)
        bool _bDown{ false };         ///< 게임이 보는 눌림(레이어가 켜져 있고 가리지 않은 입력) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool _bPhysicalDown{ false }; ///< 가림과 상관없는 장치 눌림 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool _bLive{ false };         ///< 바인딩이 보이는지(레이어가 켜져 있고, 콘솔이 눌린 키를 가리지 않음) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @struct InputTriggerRules
     * @brief 맵 전체의 시간 · 거리 문턱과, 그것으로 바인딩 상태를 진행해 발화를 정하는 판정입니다. 값은 맵 정의(`<InputMap doubleClickTime …>`)와 설정 함수가 정합니다.
     */
    struct SW_API InputTriggerRules
    {
        float32 _doubleClickTime{ InputMapDefaults::kDoubleClickTime };
        float32 _doubleClickMaxDistance{ InputMapDefaults::kDoubleClickMaxDistance };
        float32 _dragThreshold{ InputMapDefaults::kDragThreshold }; ///< 클릭과 끌기를 가르는 포인터 이동 거리(픽셀, `dragThreshold`)
        float32 _doubleTapTime{ InputMapDefaults::kDoubleTapTime };
        float32 _holdThreshold{ InputMapDefaults::kHoldThreshold };
        float32 _navRepeatDelay{ 0.4f };
        float32 _navRepeatRate{ 0.08f };

        /** @brief 바인딩이 발화하는 데 필요한 누름 횟수입니다. MultiTap 은 `_tapCount`, DoubleTap · DoubleClicked 는 2, 나머지는 0 입니다. */
        static uint32 getRequiredTapCount( const ActionBinding& binding );
        /** @brief 바인딩의 트리거가 실제로 쓰는 시간(초)입니다. 바인딩 값이 0 이면 트리거별 맵 값입니다. 시간을 쓰지 않는 트리거는 0 입니다. */
        float32 getEffectiveTriggerTime( const ActionBinding& binding ) const;
        /** @brief 이 바인딩이 "홀드에 닿았다" 를 보는 시간입니다. 홀드 트리거는 바인딩 시간, 나머지는 맵 값입니다. */
        float32 getHoldTime( const ActionBinding& binding ) const;
        /** @brief 바인딩의 첫 슬롯이 마우스 단추면 true 입니다(끌기 · 더블클릭 거리를 보는 바인딩). */
        static bool isPointerBinding( const ActionBinding& binding );

        /**
         * @brief @p sample 로 @p state 를 한 프레임 진행하고 발화(`_bTriggered`)를 정합니다. 눌림 · 뗌 · 홀드 · 연타 · 끌기 · 펄스 타이머가 모두 여기서 바뀝니다.
         * @details 뗀 프레임 뒤의 홀드 · 펄스 타이머 지우기는 부르는 쪽이 이번 프레임 값을 다 읽은 뒤 `endFrame` 으로 한다(액션 합산이 뗀 프레임의 홀드 시간을 본다).
         */
        void advance( const ActionBinding& binding, InputBindingState& state, float32 deltaSeconds, const InputBindingSample& sample ) const;
        /** @brief 뗀 바인딩의 홀드 · 펄스 타이머를 지웁니다. `advance` 뒤 이번 프레임 값을 읽고 부릅니다. */
        static void endFrame( InputBindingState& state );

    private:
        bool evaluateTrigger( const ActionBinding& binding, const InputBindingState& state, float32 deltaSeconds ) const;
        /** @brief 눌림이 시작된 프레임에 연타 횟수를 세어 `_bDoubleClicked` 를 정합니다. */
        void advanceTapStreak( const ActionBinding& binding, InputBindingState& state, uint32 requiredTaps, float32 doubleWindow, const int2& mousePos ) const;
        /** @brief 눌린 바인딩의 포인터가 이번 누름 위치(`_lastPress`)에서 끌기 문턱을 넘었는지 갱신합니다(`_bDragged` · `_bDragStarted`). */
        void advanceDrag( const ActionBinding& binding, InputBindingState& state, const int2& mousePos ) const;
    };
} // namespace sw
