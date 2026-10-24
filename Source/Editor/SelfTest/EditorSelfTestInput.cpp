#include "pch.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Core/Container/vector.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

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

            /** @brief 테스트가 돈다(레이블을 적는다)는 표시와 큐 · 레이블입니다. */
            struct State
            {
                vector<PendingEvent> _listEvent;
                vector<NamedMark>    _listMark;
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorSelfTestMarks::note( const utf8* pKey )
    {
        EditorSelfTestInputInternal::State* pState     = EditorSelfTestInputInternal::findState();
        const bool                          bRecording = pState != nullptr && pState->_bEnabled;
        if ( bRecording == false || pKey == nullptr )
            return;
        EditorSelfTestInputInternal::State& state = *pState;
        EditorSelfTestMark                  mark;
        mark._itemID     = ImGui::GetItemID();
        mark._viewportID = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
        mark._min        = float2{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y };
        mark._max        = float2{ ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y };
        mark._frame      = static_cast<uint32>( ImGui::GetFrameCount() );
        mark._bClipped   = ImGui::IsItemVisible() == false;
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

    void EditorSelfTestMarks::noteInputFrame( const utf8* pKey )
    {
        note( pKey );
        EditorSelfTestInputInternal::State* pState     = EditorSelfTestInputInternal::findState();
        const bool                          bRecording = pState != nullptr && pState->_bEnabled;
        if ( bRecording == false || pKey == nullptr )
            return;
        // 레이블 글까지 한 위젯으로 잡힌 사각형에서 입력 칸만 남긴다. 레이블이 창 밖으로 길게 나가도 가운데가 칸 위에 있게 한다.
        for ( EditorSelfTestInputInternal::NamedMark& named : pState->_listMark )
        {
            if ( named._key == pKey )
            {
                const float32 frameRight = named._mark._min._x + ImGui::CalcItemWidth();
                if ( frameRight < named._mark._max._x )
                    named._mark._max._x = frameRight;
                return;
            }
        }
    }

    void EditorSelfTestMarks::noteRect( const utf8* pKey, const float2& min, const float2& max )
    {
        EditorSelfTestInputInternal::State* pState     = EditorSelfTestInputInternal::findState();
        const bool                          bRecording = pState != nullptr && pState->_bEnabled;
        if ( bRecording == false || pKey == nullptr )
            return;
        EditorSelfTestInputInternal::State& state = *pState;
        EditorSelfTestMark                  mark;
        mark._viewportID = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
        mark._min        = min;
        mark._max        = max;
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
                case EditorSelfTestInputInternal::EventKind::FrameBreak:
                {
                    break;
                }
            }
        }
        listEvent.erase( listEvent.begin(), listEvent.begin() + static_cast<ptrdiff_t>( consumedCount ) );
    }
} // namespace sw::editor
