#include "pch.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Core/Container/vector.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestInputInternal
        {
            enum class EventKind : uint8
            {
                MousePos,
                MouseButton,
                Text,
                Key,
                MouseWheel, ///< 세로 휠 눈금(`_position._y`)
                FrameBreak, ///< 여기까지를 이번 프레임에 넣고 나머지는 다음 프레임에 넣는다
            };

            struct PendingEvent
            {
                string    _text;
                float2    _position{};
                int32     _code{ 0 }; ///< 버튼 번호 · ImGuiKey, 마우스 위치면 뷰포트 id(0 = 알리지 않음)
                EventKind _kind{ EventKind::MousePos };
                uint8     _bDown{ SW_FALSE };
            };

            struct NamedMark
            {
                string             _key;
                EditorSelfTestMark _mark;
            };

            /** @brief 키 · 글 입력을 받을 곳입니다(`EditorSelfTestInput::isKeyboardTargetSettled`). */
            struct KeyboardTarget
            {
                float2 _pos{};
                float2 _size{};
                uint32 _windowID{ 0 };
                uint32 _activeID{ 0 };
                uint32 _navID{ 0 };
                uint32 _popupCount{ 0 };

                bool operator==( const KeyboardTarget& ) const = default;
            };

            /** @brief 테스트가 돈다(레이블을 적는다)는 표시와 큐 · 레이블입니다. */
            struct State
            {
                vector<PendingEvent> _listEvent;
                vector<NamedMark>    _listMark;
                KeyboardTarget       _keyboardTarget{};
                uint32               _keyboardTargetFrame{ 0 };       ///< `_keyboardTarget` 을 뜬 ImGui 프레임
                uint32               _keyboardTargetStableSince{ 0 }; ///< 지금 표본과 같은 값이 이어진 첫 프레임
                float2               _heldMousePosition{};
                uint32               _heldMouseViewportID{ 0 };
                bool                 _bHoldMouse{ false }; ///< 누른 채 두는 동안 프레임마다 커서를 이 위치로 다시 넣는다(플랫폼이 실제 커서를 넣어 호버가 풀리지 않게)
                bool                 _bEnabled{ false };
            };

            /**
             * @brief 지금 에디터 범위의 입력 상태입니다. 에디터 컨텍스트가 없으면 nullptr 입니다.
             * @details 상태의 주인은 `EditorStateStore` 다 — 컨텍스트가 `initialize` 에서 맨 먼저 만드는 내장 서브시스템이라 컨텍스트가 있는 순간부터 있다.
             *          시나리오는 첫 에디터 프레임 앞(런타임 `prepare`)에 `setEnabled( true )` 를 부른다. `SW_EDITOR_SUBSYSTEM` 서브시스템은 첫 프레임 앞의
             *          `syncExtensionRegistrations` 에서야 만들어져 그때 켜는 일이 조용히 사라졌고, 위젯 레이블이 한 번도 기록되지 않았다.
             */
            static State* findState()
            {
                EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>();
                return pStore != nullptr ? &pStore->get<State>() : nullptr;
            }

            static void push( PendingEvent&& event )
            {
                State* pState = findState();
                if ( pState != nullptr )
                    pState->_listEvent.push_back( std::move( event ) );
            }

            /** @brief 레이블을 기록하는 중이면 상태를, 아니면(꺼짐 · 컨텍스트 없음 · 키 없음) nullptr 를 반환합니다. */
            static State* findRecordingState( const utf8* pKey )
            {
                State* pState = findState();
                return pState != nullptr && pState->_bEnabled && pKey != nullptr ? pState : nullptr;
            }

            /** @brief 바로 앞에 그린 위젯(ImGui "last item")의 레이블입니다. */
            static EditorSelfTestMark makeLastItemMark()
            {
                EditorSelfTestMark mark;
                mark._itemID     = ImGui::GetItemID();
                mark._viewportID = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
                mark._min        = float2{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y };
                mark._max        = float2{ ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y };
                mark._frame      = static_cast<uint32>( ImGui::GetFrameCount() );
                mark._bClipped   = ImGui::IsItemVisible() == false;
                return mark;
            }

            static bool isSamePlace( const EditorSelfTestMark& lhs, const EditorSelfTestMark& rhs )
            {
                return lhs._min == rhs._min && lhs._max == rhs._max && lhs._viewportID == rhs._viewportID && lhs._bClipped == rhs._bClipped;
            }

            /**
             * @brief @p mark 를 @p pKey 로 남깁니다. 앞 기록이 바로 앞(또는 같은) 프레임의 같은 자리면 자리 잡은 첫 프레임을 이어받고, 아니면 이 프레임부터 다시 센다.
             * @details 같은 프레임에 같은 키를 다시 남기면(목록이 같은 레이블을 둘 그린다) 마지막 것이 이긴다 — 자리가 다르면 자리 잡지 않은 것으로 센다.
             */
            static void record( State& state, const utf8* pKey, EditorSelfTestMark mark )
            {
                mark._stableSinceFrame = mark._frame;
                for ( NamedMark& named : state._listMark )
                {
                    if ( named._key != pKey )
                        continue;
                    const bool bContinued = named._mark._frame + 1 >= mark._frame && isSamePlace( named._mark, mark );
                    if ( bContinued )
                        mark._stableSinceFrame = named._mark._stableSinceFrame;
                    named._mark = mark;
                    return;
                }
                state._listMark.push_back( NamedMark{ string( pKey ), mark } );
            }

            /** @brief 키 · 글 입력을 받을 곳의 표본입니다. 둘이 같으면 그 사이 초점이 움직이지 않았다. */
            static KeyboardTarget sampleKeyboardTarget()
            {
                const ImGuiContext& context = *ImGui::GetCurrentContext();
                KeyboardTarget      target;
                if ( context.NavWindow != nullptr )
                {
                    target._windowID = context.NavWindow->ID;
                    target._pos      = float2{ context.NavWindow->Pos.x, context.NavWindow->Pos.y };
                    target._size     = float2{ context.NavWindow->Size.x, context.NavWindow->Size.y };
                }
                target._activeID   = context.ActiveId;
                target._navID      = context.NavId;
                target._popupCount = static_cast<uint32>( context.OpenPopupStack.Size );
                return target;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorSelfTestMarks::note( const utf8* pKey )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findRecordingState( pKey );
        if ( pState != nullptr )
            EditorSelfTestInputInternal::record( *pState, pKey, EditorSelfTestInputInternal::makeLastItemMark() );
    }

    void EditorSelfTestMarks::noteInputFrame( const utf8* pKey )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findRecordingState( pKey );
        if ( pState == nullptr )
            return;
        // 레이블 글까지 한 위젯으로 잡힌 사각형에서 입력 칸만 남긴다. 레이블이 창 밖으로 길게 나가도 가운데가 칸 위에 있게 한다.
        EditorSelfTestMark mark       = EditorSelfTestInputInternal::makeLastItemMark();
        const float32      frameRight = mark._min._x + ImGui::CalcItemWidth();
        if ( frameRight < mark._max._x )
            mark._max._x = frameRight;
        EditorSelfTestInputInternal::record( *pState, pKey, mark );
    }

    void EditorSelfTestMarks::noteRect( const utf8* pKey, const float2& min, const float2& max )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findRecordingState( pKey );
        if ( pState == nullptr )
            return;
        EditorSelfTestMark mark;
        mark._viewportID = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
        mark._min        = min;
        mark._max        = max;
        mark._frame      = static_cast<uint32>( ImGui::GetFrameCount() );
        EditorSelfTestInputInternal::record( *pState, pKey, mark );
    }

    bool EditorSelfTestMarks::find( string_view key, EditorSelfTestMark& outMark )
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false;
        const uint32 frame = static_cast<uint32>( ImGui::GetFrameCount() );
        for ( const EditorSelfTestInputInternal::NamedMark& named : pState->_listMark )
        {
            if ( named._key == key && named._mark._frame + 1 >= frame )
            {
                outMark = named._mark;
                return true;
            }
        }
        return false;
    }

    bool EditorSelfTestMarks::setEnabled( bool bEnabled )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false; // 에디터 컨텍스트가 없다 — 기록할 곳이 없으니 켠 척하지 않고 알린다
        pState->_bEnabled = bEnabled;
        if ( bEnabled == false )
        {
            pState->_listMark.clear();
            pState->_listEvent.clear();
            pState->_bHoldMouse = false;
        }
        return true;
    }

    bool EditorSelfTestMarks::isEnabled()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        return pState != nullptr && pState->_bEnabled;
    }

    void EditorSelfTestInput::moveMouse( const float2& position, uint32 viewportID )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind     = EditorSelfTestInputInternal::EventKind::MousePos;
        event._position = position;
        event._code     = static_cast<int32>( viewportID );
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::setMouseButton( int32 button, bool bDown )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind  = EditorSelfTestInputInternal::EventKind::MouseButton;
        event._code  = button;
        event._bDown = bDown ? SW_TRUE : SW_FALSE;
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::typeText( string_view utf8Text )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind = EditorSelfTestInputInternal::EventKind::Text;
        event._text = string( utf8Text );
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::scrollMouseWheel( float32 notches )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind     = EditorSelfTestInputInternal::EventKind::MouseWheel;
        event._position = float2{ 0.0f, notches };
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::waitNextFrame()
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind = EditorSelfTestInputInternal::EventKind::FrameBreak;
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::setKey( int32 imguiKey, bool bDown )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind  = EditorSelfTestInputInternal::EventKind::Key;
        event._code  = imguiKey;
        event._bDown = bDown ? SW_TRUE : SW_FALSE;
        EditorSelfTestInputInternal::push( std::move( event ) );
        // 실제 플랫폼 백엔드와 같이 수정 키는 두 이벤트다 — 왼쪽 키(에디터 입력 맵이 읽는다)와 ImGui 의 수정자 비트(`io.KeyCtrl`, 서드파티 노드 에디터가 읽는다).
        int32 modifierBit = 0;
        if ( imguiKey == ImGuiKey_LeftCtrl )
            modifierBit = ImGuiMod_Ctrl;
        else if ( imguiKey == ImGuiKey_LeftShift )
            modifierBit = ImGuiMod_Shift;
        else if ( imguiKey == ImGuiKey_LeftAlt )
            modifierBit = ImGuiMod_Alt;
        if ( modifierBit != 0 )
        {
            EditorSelfTestInputInternal::PendingEvent modifierEvent;
            modifierEvent._kind  = EditorSelfTestInputInternal::EventKind::Key;
            modifierEvent._code  = modifierBit;
            modifierEvent._bDown = bDown ? SW_TRUE : SW_FALSE;
            EditorSelfTestInputInternal::push( std::move( modifierEvent ) );
        }
    }

    bool EditorSelfTestInput::findKeyByName( string_view name, int32& outImguiKey )
    {
        struct ModifierName
        {
            const utf8* _pName;
            int32       _key;
        };
        constexpr ModifierName kArrModifier[] = {
            { "Ctrl",  ImGuiKey_LeftCtrl},
            {"Shift", ImGuiKey_LeftShift},
            {  "Alt",   ImGuiKey_LeftAlt},
        };
        for ( const ModifierName& modifier : kArrModifier )
        {
            if ( name == modifier._pName )
            {
                outImguiKey = modifier._key;
                return true;
            }
        }
        for ( int32 key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key )
        {
            if ( name == ImGui::GetKeyName( static_cast<ImGuiKey>( key ) ) )
            {
                outImguiKey = key;
                return true;
            }
        }
        return false;
    }

    bool EditorSelfTestInput::moveMouseToMark( string_view key, const float2& anchor )
    {
        EditorSelfTestMark mark;
        if ( EditorSelfTestMarks::find( key, mark ) == false )
            return false;
        moveMouse( float2{ mark._min._x + ( mark._max._x - mark._min._x ) * anchor._x, mark._min._y + ( mark._max._y - mark._min._y ) * anchor._y }, mark._viewportID );
        return true;
    }

    bool EditorSelfTestInput::holdMouseAtMark( string_view key, const float2& anchor )
    {
        EditorSelfTestMark mark;
        if ( EditorSelfTestMarks::find( key, mark ) == false )
            return false;
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false;
        EditorSelfTestInputInternal::State& state = *pState;
        state._heldMousePosition                  = float2{ mark._min._x + ( mark._max._x - mark._min._x ) * anchor._x, mark._min._y + ( mark._max._y - mark._min._y ) * anchor._y };
        state._heldMouseViewportID                = mark._viewportID;
        state._bHoldMouse                         = true;
        return true;
    }

    bool EditorSelfTestInput::hasPendingEvents()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        return pState != nullptr && pState->_listEvent.empty() == false;
    }

    bool EditorSelfTestInput::isKeyboardTargetSettled()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false;
        // 표본이 지난 프레임 것이어야 한다 — 오래된 표본(기록을 다시 켰다)은 지금 초점을 말하지 않는다.
        const bool bFresh = pState->_keyboardTargetFrame + 1 >= static_cast<uint32>( ImGui::GetFrameCount() );
        return bFresh && pState->_keyboardTargetFrame > pState->_keyboardTargetStableSince;
    }

    void EditorSelfTestInput::releaseMouseHold()
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState != nullptr )
            pState->_bHoldMouse = false;
    }

    void EditorSelfTestInput::flushIntoImGui()
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return;
        EditorSelfTestInputInternal::State&                state     = *pState;
        vector<EditorSelfTestInputInternal::PendingEvent>& listEvent = state._listEvent;
        if ( state._bEnabled )
        {
            // 앞 프레임이 끝난 뒤의 초점이다(이번 프레임의 입력을 넣기 전). 바로 앞 프레임과 같으면 자리 잡은 첫 프레임을 이어받는다.
            const EditorSelfTestInputInternal::KeyboardTarget target     = EditorSelfTestInputInternal::sampleKeyboardTarget();
            const uint32                                      frame      = static_cast<uint32>( ImGui::GetFrameCount() );
            const bool                                        bContinued = state._keyboardTargetFrame + 1 >= frame && state._keyboardTarget == target;
            if ( bContinued == false )
                state._keyboardTargetStableSince = frame;
            state._keyboardTarget      = target;
            state._keyboardTargetFrame = frame;
        }
        if ( state._bHoldMouse )
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent( state._heldMousePosition._x, state._heldMousePosition._y );
            if ( state._heldMouseViewportID != 0 )
                io.AddMouseViewportEvent( static_cast<ImGuiID>( state._heldMouseViewportID ) );
        }
        if ( listEvent.empty() )
            return;
        ImGuiIO& io            = ImGui::GetIO();
        size_t   consumedCount = 0;
        for ( const EditorSelfTestInputInternal::PendingEvent& event : listEvent )
        {
            ++consumedCount;
            if ( event._kind == EditorSelfTestInputInternal::EventKind::FrameBreak )
                break;
            switch ( event._kind )
            {
                case EditorSelfTestInputInternal::EventKind::MousePos:
                {
                    io.AddMousePosEvent( event._position._x, event._position._y );
                    // 떠 있는 창(자기 플랫폼 창)은 플랫폼이 실제 커서로 고른 뷰포트로 호버가 갈린다 — 위젯의 뷰포트를 뒤에 넣어 이긴다.
                    if ( event._code != 0 )
                        io.AddMouseViewportEvent( static_cast<ImGuiID>( event._code ) );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::MouseButton:
                {
                    io.AddMouseButtonEvent( event._code, event._bDown != SW_FALSE );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::Text:
                {
                    io.AddInputCharactersUTF8( event._text.c_str() );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::Key:
                {
                    io.AddKeyEvent( static_cast<ImGuiKey>( event._code ), event._bDown != SW_FALSE );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::MouseWheel:
                {
                    io.AddMouseWheelEvent( 0.0f, event._position._y );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::FrameBreak:
                {
                    break;
                }
            }
        }
        listEvent.erase( listEvent.begin(), listEvent.begin() + static_cast<ptrdiff_t>( consumedCount ) );
    }
} // namespace sw::editor
