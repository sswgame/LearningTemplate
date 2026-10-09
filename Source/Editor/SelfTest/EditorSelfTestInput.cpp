#include "pch.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Core/Container/vector.h"

#include <imgui.h>

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

            /** @brief 시험이 돈다(이름표를 적는다)는 표시와 큐 · 이름표입니다. 에디터 모듈 안의 정적이라 핫 리로드면 비워진다 — 시험 중에는 리로드하지 않는다. */
            struct State
            {
                vector<PendingEvent> _listEvent;
                vector<NamedMark>    _listMark;
                float2               _heldMousePosition{};
                uint32               _heldMouseViewportId{ 0 };
                bool                 _bHoldMouse{ false }; ///< 누른 채 두는 동안 프레임마다 커서를 이 자리로 다시 넣는다(플랫폼이 실제 커서를 넣어 호버가 풀리지 않게)
                bool                 _bEnabled{ false };
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }

            static void push( PendingEvent&& event ) { getState()._listEvent.push_back( std::move( event ) ); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorSelfTestMarks::note( const utf8* pKey )
    {
        EditorSelfTestInputInternal::State& state = EditorSelfTestInputInternal::getState();
        if ( state._bEnabled == false || pKey == nullptr )
            return;
        EditorSelfTestMark mark;
        mark._itemId     = ImGui::GetItemID();
        mark._viewportId = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
        mark._min        = float2{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y };
        mark._max        = float2{ ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y };
        mark._frame      = static_cast<uint32>( ImGui::GetFrameCount() );
        for ( EditorSelfTestInputInternal::NamedMark& named : state._listMark )
        {
            if ( named._key == pKey )
            {
                named._mark = mark;
                return;
            }
        }
        state._listMark.push_back( EditorSelfTestInputInternal::NamedMark{ string( pKey ), mark } );
    }

    bool EditorSelfTestMarks::find( string_view key, EditorSelfTestMark& outMark )
    {
        const uint32 frame = static_cast<uint32>( ImGui::GetFrameCount() );
        for ( const EditorSelfTestInputInternal::NamedMark& named : EditorSelfTestInputInternal::getState()._listMark )
        {
            if ( named._key == key && named._mark._frame + 1 >= frame )
            {
                outMark = named._mark;
                return true;
            }
        }
        return false;
    }

    void EditorSelfTestMarks::setEnabled( bool bEnabled )
    {
        EditorSelfTestInputInternal::State& state = EditorSelfTestInputInternal::getState();
        state._bEnabled                           = bEnabled;
        if ( bEnabled == false )
        {
            state._listMark.clear();
            state._listEvent.clear();
            state._bHoldMouse = false;
        }
    }

    bool EditorSelfTestMarks::isEnabled() { return EditorSelfTestInputInternal::getState()._bEnabled; }

    void EditorSelfTestInput::moveMouse( const float2& position, uint32 viewportId )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind     = EditorSelfTestInputInternal::EventKind::MousePos;
        event._position = position;
        event._code     = static_cast<int32>( viewportId );
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
    }

    bool EditorSelfTestInput::findKeyByName( string_view name, int32& outImguiKey )
    {
        struct ModifierName
        {
            const utf8* _pName;
            int32       _key;
        };
        constexpr ModifierName kArrModifier[] = {
            { "Ctrl",  ImGuiMod_Ctrl},
            {"Shift", ImGuiMod_Shift},
            {  "Alt",   ImGuiMod_Alt},
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

    bool EditorSelfTestInput::moveMouseToMark( string_view key )
    {
        EditorSelfTestMark mark;
        if ( EditorSelfTestMarks::find( key, mark ) == false )
            return false;
        moveMouse( float2{ ( mark._min._x + mark._max._x ) * 0.5f, ( mark._min._y + mark._max._y ) * 0.5f }, mark._viewportId );
        return true;
    }

    bool EditorSelfTestInput::holdMouseAtMark( string_view key )
    {
        EditorSelfTestMark mark;
        if ( EditorSelfTestMarks::find( key, mark ) == false )
            return false;
        EditorSelfTestInputInternal::State& state = EditorSelfTestInputInternal::getState();
        state._heldMousePosition                  = float2{ ( mark._min._x + mark._max._x ) * 0.5f, ( mark._min._y + mark._max._y ) * 0.5f };
        state._heldMouseViewportId                = mark._viewportId;
        state._bHoldMouse                         = true;
        return true;
    }

    void EditorSelfTestInput::releaseMouseHold() { EditorSelfTestInputInternal::getState()._bHoldMouse = false; }

    void EditorSelfTestInput::flushIntoImGui()
    {
        EditorSelfTestInputInternal::State&                state     = EditorSelfTestInputInternal::getState();
        vector<EditorSelfTestInputInternal::PendingEvent>& listEvent = state._listEvent;
        if ( state._bHoldMouse )
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent( state._heldMousePosition._x, state._heldMousePosition._y );
            if ( state._heldMouseViewportId != 0 )
                io.AddMouseViewportEvent( static_cast<ImGuiID>( state._heldMouseViewportId ) );
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
                case EditorSelfTestInputInternal::EventKind::FrameBreak:
                {
                    break;
                }
            }
        }
        listEvent.erase( listEvent.begin(), listEvent.begin() + static_cast<ptrdiff_t>( consumedCount ) );
    }
} // namespace sw::editor
