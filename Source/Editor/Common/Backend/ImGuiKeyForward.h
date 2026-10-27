/**
 * @file ImGuiKeyForward.h
 * @brief ImGui 가 받은 키보드 입력을 엔진 키 · 글자 입력 사건으로 바꿉니다.
 * @details 게임 뷰 패널을 다른 OS 창(ImGui 보조 뷰포트)으로 떼면 그 창의 키 메시지는 ImGui 백엔드만 받고 게임 입력(`InputManager`)에는 닿지 않습니다.
 *          그 키를 ImGui 가 이번 프레임에 알게 된 변화에서 읽어 게임 입력에 넣을 사건으로 만듭니다. 키 ↔ ImGui 키 짝은 여기 한 곳에 둡니다(입력 맵 패널의 키 바인딩 잡기도 이것을 씁니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/RawInputEvent.h"

namespace sw::editor
{
    /**
     * @struct ImGuiKeyForward
     * @brief 이번 ImGui 프레임의 키보드 변화를 엔진 입력 사건으로 읽습니다. ImGui 컨텍스트가 있고 프레임 안(`NewFrame` 뒤)이어야 합니다.
     */
    struct ImGuiKeyForward
    {
        /** @brief 엔진 키가 지금 ImGui 에서 눌려 있으면 true 입니다. 짝이 없는 키(`Key::Unknown`)는 false 입니다. */
        [[nodiscard]] static bool isKeyDown( Key key );
        /** @brief 이번 프레임에 새로 눌린 키(자동 반복 제외) 하나를 돌려줍니다. 없으면 `Key::Unknown` 입니다. */
        [[nodiscard]] static Key findPressedKey();
        /**
         * @brief 이번 프레임에 눌리거나(자동 반복 포함) 떼어진 키를 @p outListEvent 에 덧붙입니다. 사건마다 지금 누른 수정 키(Ctrl · Shift · Alt · Super)가 붙습니다.
         * @details 누름 사건의 반복 표시(`_bRepeat`)는 자동 반복이면 켭니다.
         */
        static void appendKeyChanges( vector<RawInputEvent>& outListEvent );
        /** @brief 이번 프레임에 ImGui 가 받은 글자 입력(`io.InputQueueCharacters`)을 글자마다 `TextInput` 사건으로 @p outListEvent 에 덧붙입니다. */
        static void appendTextInput( vector<RawInputEvent>& outListEvent );
        /**
         * @brief 이번 프레임에 새로 눌린 수정 키가 아닌 키 하나와 지금 누른 수정 키 마스크(`ModifierKey`)를 돌려줍니다(단축키 재배치가 받는 조합). 없으면 false 입니다.
         * @details 수정 키만 누른 프레임은 조합이 아니다 — Ctrl 을 먼저 누르고 U 를 누르면 U 의 프레임에 `Ctrl+U` 가 된다.
         */
        [[nodiscard]] static bool findPressedChord( Key& outKey, uint8& outModifierMask );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class DetachedKeyForwarder
     * @brief 떼어 낸 창의 키 사건을 게임 입력에 넣으면서 "누름을 넣고 아직 뗌을 넣지 않은 키" 를 기억해, 포커스를 잃어도 짝 뗌을 넣게 합니다.
     * @details ImGui 와 `InputManager` 를 모르는 순수 상태라 실제 OS 창 없이 시험한다(넣는 쪽은 @c post 콜백). 포커스가 OS 창을 따라 옮겨 가는 것 자체는 ImGui 백엔드의 일이다.
     */
    class DetachedKeyForwarder
    {
    public:
        /**
         * @brief 이번 프레임의 사건을 넣습니다. @p bForward 가 거짓(포커스를 잃었거나 패널을 주 창에 되돌림)이면 사건을 버리고 누른 채 남은 키를 모두 뗍니다.
         * @param post `bool( const RawInputEvent& )` — 큐가 차서 못 넣으면 false. 못 넣은 누름은 기억하지 않고, 못 넣은 뗌은 다음 프레임에 다시 시도한다.
         */
        template <class Post>
        void forward( bool bForward, const vector<RawInputEvent>& listEvent, Post&& post )
        {
            if ( bForward == false )
            {
                for ( size_t index = _listHeldKey.size(); index > 0; --index )
                {
                    if ( post( RawInputEvent::makeKeyUp( _listHeldKey[index - 1] ) ) )
                        VectorUtil::removeAtSwap( _listHeldKey, index - 1 );
                }
                return;
            }
            for ( const RawInputEvent& event : listEvent )
            {
                if ( post( event ) == false )
                    continue; // 큐가 차면 이 사건만 잃는다
                // 누름은 먼저 지우고 넣어 중복 없이 한 번만 든다(자동 반복 누름이 여럿 와도 뗌은 한 번이다).
                if ( event._type == RawInputEventType::KeyDown )
                {
                    VectorUtil::removeValue( _listHeldKey, event._payload._keyData._key );
                    _listHeldKey.push_back( event._payload._keyData._key );
                }
                else if ( event._type == RawInputEventType::KeyUp )
                {
                    VectorUtil::removeValue( _listHeldKey, event._payload._keyData._key );
                }
            }
        }

        /** @brief 누름을 넣고 아직 뗌을 넣지 않은 키입니다. */
        [[nodiscard]] const vector<Key>& getHeldKeys() const { return _listHeldKey; }

    private:
        vector<Key> _listHeldKey{};
    };
} // namespace sw::editor
