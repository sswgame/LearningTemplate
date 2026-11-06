#include "pch.h"

#include "Editor/Common/Backend/Platform/ImGuiTextInputBridge.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"
#include "Engine/Window/WindowTextInput.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief `Platform_SetImeDataFn` 입니다. 사용자 값은 `Platform_ImeUserData` 의 다리입니다. */
        void onSetImeData( ImGuiContext* pContext, ImGuiViewport* pViewport, ImGuiPlatformImeData* pData )
        {
            (void)pContext;
            ImGuiTextInputBridge* const pBridge = static_cast<ImGuiTextInputBridge*>( ImGui::GetPlatformIO().Platform_ImeUserData );
            if ( pBridge != nullptr && pViewport != nullptr && pData != nullptr )
                pBridge->onImeData( *pViewport, *pData );
        }
    } // namespace

    ImGuiTextInputBridge::ImGuiTextInputBridge()
        : _pWindow{ nullptr }
        , _composition{}
        , _inputX{ 0.0f }
        , _inputY{ 0.0f }
        , _lineHeight{ 0.0f }
        , _viewportID{ 0 }
    {
    }

    void ImGuiTextInputBridge::initialize( IWindow& window )
    {
        _pWindow                       = &window;
        ImGuiPlatformIO& platform      = ImGui::GetPlatformIO();
        platform.Platform_SetImeDataFn = &onSetImeData;
        platform.Platform_ImeUserData  = this;
    }

    void ImGuiTextInputBridge::shutdown()
    {
        if ( _pWindow == nullptr )
            return;
        _pWindow->requestTextInput( WindowTextInputClient::Editor, false );
        ImGuiPlatformIO& platform      = ImGui::GetPlatformIO();
        platform.Platform_SetImeDataFn = nullptr;
        platform.Platform_ImeUserData  = nullptr;
        _pWindow                       = nullptr;
    }

    bool ImGuiTextInputBridge::processEvent( const NativeWindowEvent& event )
    {
        if ( event._message != NativeWindowEvent::kMessageTextInput || event._lParam == 0 )
            return false;
        const WindowTextInputEvent& input = *reinterpret_cast<const WindowTextInputEvent*>( event._lParam );
        if ( input._kind == WindowTextInputKind::Composition )
        {
            _composition.assign( input._text.data(), input._text.size() );
            return true;
        }
        const string text( input._text ); // AddInputCharactersUTF8 은 널로 끝나는 글을 받는다
        ImGui::GetIO().AddInputCharactersUTF8( text.c_str() );
        return true;
    }

    void ImGuiTextInputBridge::onImeData( const ImGuiViewport& viewport, const ImGuiPlatformImeData& data )
    {
        _pWindow->requestTextInput( WindowTextInputClient::Editor, data.WantVisible );
        if ( data.WantVisible == false )
            return;
        _inputX     = data.InputPos.x;
        _inputY     = data.InputPos.y;
        _lineHeight = data.InputLineHeight;
        _viewportID = viewport.ID;
        // 입력기의 자리는 키보드 포커스를 가진 창 기준이다 - ImGui 는 글 칸이 있는 뷰포트를 알리고 그 창이 포커스를 가진다.
        WindowTextInputArea area;
        area._position   = int2{ static_cast<int32>( data.InputPos.x - viewport.Pos.x ), static_cast<int32>( data.InputPos.y - viewport.Pos.y ) };
        area._lineHeight = static_cast<int32>( data.InputLineHeight );
        _pWindow->setTextInputArea( area );
    }

    void ImGuiTextInputBridge::drawComposition() const
    {
        if ( _composition.empty() )
            return;
        ImGuiViewport* const pViewport = ImGui::FindViewportByID( _viewportID );
        if ( pViewport == nullptr )
            return; // 글 칸의 창이 닫혔다 - 곧 조합도 끝난다
        // 앱 화면 규칙: 강조색은 쓰지 않는다. 팝업 바탕 · 본문 글자에 밑줄로 조합 중임을 보인다(Win32 의 조합 창과 같은 표시).
        ImDrawList* const pDrawList = ImGui::GetForegroundDrawList( pViewport );
        const ImVec2      position( _inputX, _inputY );
        const ImVec2      textSize = ImGui::CalcTextSize( _composition.c_str(), _composition.c_str() + _composition.size() );
        const ImVec2      rectEnd( position.x + textSize.x, position.y + MathUtil::max( textSize.y, _lineHeight ) );
        const ImU32       textColor = ImGui::GetColorU32( ImGuiCol_Text );
        pDrawList->AddRectFilled( position, rectEnd, ImGui::GetColorU32( ImGuiCol_PopupBg ) );
        pDrawList->AddText( position, textColor, _composition.c_str(), _composition.c_str() + _composition.size() );
        pDrawList->AddLine( ImVec2( position.x, rectEnd.y - 1.0f ), ImVec2( rectEnd.x, rectEnd.y - 1.0f ), textColor );
    }
} // namespace sw::editor
