/**
 * @file UIInputConsumption.h
 * @brief UI 가 소비한 물리 입력(키, 버튼, 마우스 버튼, 스틱)을 보관합니다. 뗄 때까지 게임 쪽 액션이 그 입력으로 발화하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/KeyCodeUtil.h"

namespace sw
{
    class InputManager;
    class InputMap;

    /**
     * @class UIInputConsumption
     * @brief UI 가 처리한 액션이 쓴 물리 입력의 집합입니다(언리얼 Enhanced Input 의 입력 소비, CommonUI 액션 라우터의 "UI 가 소비한 입력"에 해당).
     * @details 액션을 소비하면(`consumeAction`) 그 액션의 바인딩 중 지금 눌린 슬롯(키, 패드 버튼, 마우스 버튼이며, 조합 키는 트리거 키만)과 기울인 스틱을 적는다.
     *          적힌 입력은 뗀 프레임까지 소비된 채로 남고(뗌으로 발화하는 액션도 막는다), 그 다음 `update` 에서 빠진다.
     *          게임 쪽은 `isActionConsumed` 로 묻는다. 그 액션을 지금 누르고 있는 물리 입력이 모두 소비된 것이면 true 이다(같은 액션의 다른 키를 함께 누르면 보인다).
     *          입력 맵이 여럿이어도(UI 맵, 게임 맵) 슬롯으로 비교하므로 같은 집합을 공유한다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIInputConsumption
    {
    public:
        /** @brief 스틱이 소비된 상태에서 해제되는 기울기입니다(이 아래로 돌아오면 놓은 것으로 봅니다). */
        static constexpr float32 kStickReleaseMagnitude = 0.35f;

        UIInputConsumption();

        /** @brief 뗀 입력을 집합에서 뺍니다(프레임마다 한 번, UI 가 입력을 처리하기 전에 부릅니다). */
        void update( const InputManager& input );
        /** @brief @p inputMap 의 액션 @p action 이 지금 쓰는 물리 입력을 소비합니다. */
        void consumeAction( const InputMap& inputMap, const InputManager& input, const hashed_string& action );
        /** @brief 마우스 버튼 @p button 을 소비합니다(위젯이 그 클릭을 처리했습니다). */
        void consumeMouseButton( MouseButton button );
        /** @brief 슬롯 @p slot 을 소비합니다(액션이 아닌 원시 입력을 받은 쪽, 곧 키 바인딩 창이 씁니다). 뗄 때까지 게임이 보지 못합니다. */
        void consumeSlot( const InputSlot& slot );
        /** @brief @p inputMap 의 액션 @p action 을 지금 누르는 물리 입력이 모두 UI 에 소비된 것이면 true 입니다. */
        bool isActionConsumed( const InputMap& inputMap, const InputManager& input, const hashed_string& action ) const;
        /** @brief 슬롯 @p slot 이 소비된 입력이면 true 입니다. */
        bool isSlotConsumed( const InputSlot& slot ) const;
        /** @brief 소비된 입력이 하나라도 있으면 true 입니다. */
        bool hasConsumedInput() const { return _listConsumedSlot.empty() == false || _consumedStickMask != 0; }
        /** @brief 모두 해제합니다. */
        void clear();

    private:
        /** @struct ConsumedSlot @brief 소비된 슬롯 하나입니다. 뗀 프레임까지 남습니다. */
        struct ConsumedSlot
        {
            InputSlot _slot{};
            uint8     _bReleased{ SW_FALSE }; ///< 뗀 것을 한 번 봤는지 나타냅니다(다음 update 에서 빠집니다).
        };

    private:
        vector<ConsumedSlot> _listConsumedSlot;
        uint8                _consumedStickMask; ///< 비트는 패드 번호 × 2 + 스틱(왼쪽 0, 오른쪽 1)입니다.
    };
} // namespace sw
