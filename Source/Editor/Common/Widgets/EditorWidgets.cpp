#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorLabelLayout.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorWidgetsInternal
        {
            /** @brief 0 건 안내를 그린 횟수입니다(`getNoSearchResultHintCount`). */
            static uint32& noSearchResultHintCount()
            {
                static uint32 s_count = 0;
                return s_count;
            }

            /** @brief ImGui 글꼴로 글자열 너비를 잽니다(`EditorLabelLayoutUtil::MeasureFunc`). */
            static float32 measureImGuiText( string_view text, void* /*pUserData*/ )
            {
                return ImGui::CalcTextSize( text.data(), text.data() + text.size() ).x;
            }

            /** @brief 검색어가 있는 0 건 안내를 마지막으로 그린 ImGui 프레임입니다(0 = 그린 적 없음). */
            static uint32& filteredHintFrame()
            {
                static uint32 s_frame = 0;
                return s_frame;
            }

            static ImVec4 toIm( const Color4& c )
            {
                return ImVec4( c._r, c._g, c._b, c._a );
            }

            /**
             * @brief InputText 가 요구하는 만큼 `string` 버퍼를 늘려 줍니다.
             * @details ImGui 는 알고 있는 크기를 넘는 입력이 오면 이 콜백으로 되묻습니다. 늘린 뒤 새 주소를 알려 주지 않으면 옛
             *          버퍼를 계속 쓰므로, `Buf` 갱신까지가 한 쌍입니다.
             */
            static int32 resizeStringCallback( ImGuiInputTextCallbackData* pData )
            {
                if ( pData == nullptr || pData->EventFlag != ImGuiInputTextFlags_CallbackResize )
                    return 0;

                string* pText = static_cast<string*>( pData->UserData );
                if ( pText == nullptr )
                    return 0;

                pText->resize( static_cast<size_t>( pData->BufTextLen ) );
                pData->Buf = pText->data();
                return 0;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorWidgets::drawVec3Control( const utf8* pLabel, float3& values, float32 resetValue, float32 columnWidth, float32 speed )
    {
        ImGui::PushID( pLabel );

        ImGui::Columns( 2 );
        ImGui::SetColumnWidth( 0, columnWidth * EditorThemeUtil::getDpiScale() );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( pLabel );
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths( 3, ImGui::CalcItemWidth() );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f } );

        const float32 lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        const ImVec2  buttonSize{ lineHeight + 3.0f, lineHeight };
        bool          bChanged{ false };

        auto axis = [&]( const utf8* pAxisLabel, float32& axisValue, const Color4& color )
        {
            ImGui::PushStyleColor( ImGuiCol_Button, EditorWidgetsInternal::toIm( color ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( color._r + 0.1f, color._g + 0.1f, color._b + 0.1f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorWidgetsInternal::toIm( color ) );
            if ( ImGui::Button( pAxisLabel, buttonSize ) )
            {
                axisValue = resetValue;
                bChanged  = true;
            }
            drawTooltip( "클릭하여 기본값으로 초기화합니다" );
            ImGui::PopStyleColor( 3 );
            ImGui::SameLine();
            ImGui::PushID( pAxisLabel );
            if ( ImGui::DragFloat( "##v", &axisValue, speed, 0.0f, 0.0f, "%.2f" ) )
                bChanged = true;
            drawTooltip( "마우스 드래그 또는 더블 클릭으로 값 수정" );
            ImGui::PopID();
            ImGui::PopItemWidth();
            ImGui::SameLine();
        };

        axis( "X", values._x, style::kAxisX );
        axis( "Y", values._y, style::kAxisY );
        axis( "Z", values._z, style::kAxisZ );

        ImGui::PopStyleVar();
        ImGui::Columns( 1 );
        ImGui::PopID();
        return bChanged;
    }

    bool EditorWidgets::beginComponentCard( const utf8* pName, uint64 id, bool* pBActive, bool* pBRemoveRequested, bool bAccent )
    {
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                             ImGuiTreeNodeFlags_FramePadding;

        ImGui::PushID( static_cast<int32>( id ) );
        if ( bAccent )
        {
            const Color4 accent = EditorThemeUtil::getAccentColor();
            ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( accent._r, accent._g, accent._b, 0.85f ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( accent._r * 1.15f, accent._g * 1.15f, accent._b * 1.15f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( accent._r * 1.30f, accent._g * 1.30f, accent._b * 1.30f, 1.0f ) );
        }
        else
        {
            const Color4 headerColor = EditorThemeUtil::getHeaderBgColor();
            const Color4 accent      = EditorThemeUtil::getAccentColor();
            ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( headerColor._r, headerColor._g, headerColor._b, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( accent._r, accent._g, accent._b, 0.70f ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( accent._r, accent._g, accent._b, 0.90f ) );
        }

        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2{ 4.0f, 4.0f } );
        const float32 lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        const float32 availX     = ImGui::GetContentRegionAvail().x;
        const bool    open       = ImGui::TreeNodeEx( "hdr", flags, "%s", pName );
        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 3 );

        if ( pBActive != nullptr )
        {
            ImGui::SameLine( availX - lineHeight * 2.2f );
            ImGui::Checkbox( "##active", pBActive );
            drawTooltip( "컴포넌트 활성화/비활성화" );
        }

        ImGui::SameLine( availX - lineHeight * 0.5f );
        if ( ImGui::Button( "+", ImVec2{ lineHeight, lineHeight } ) )
            ImGui::OpenPopup( "ComponentSettings" );
        drawTooltip( "컴포넌트 추가 옵션 및 삭제" );

        if ( ImGui::BeginPopup( "ComponentSettings" ) )
        {
            if ( ImGui::MenuItem( "Remove component" ) && pBRemoveRequested != nullptr )
                *pBRemoveRequested = true;
            ImGui::EndPopup();
        }

        if ( open == false )
            ImGui::PopID();
        return open;
    }

    void EditorWidgets::endComponentCard()
    {
        ImGui::TreePop();
        ImGui::PopID();
    }

    void EditorWidgets::drawSectionHeader( const utf8* pTitle, const utf8* pSubtitle )
    {
        ImGui::TextUnformatted( pTitle );
        if ( StringUtil::isNullOrEmpty( pSubtitle ) == false )
            ImGui::TextDisabled( "%s", pSubtitle );
        ImGui::Separator();
    }

    void EditorWidgets::drawGizmoOperationControls()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 값은 `EditorWorkspace` 의 기즈모 조작 번호다. 여기 숫자는 그 표현일 뿐이고, 뜻은 워크스페이스가 정한다(ImGuizmo OPERATION 과 짝이다).
        // 세 엔진 모두 이동 · 회전 · 크기를 아이콘 단추로 둔다 — 글자 라디오는 좁은 뷰포트에서 잘린다.
        struct GizmoButton
        {
            const utf8* _pID;
            const utf8* _pIcon;
            const utf8* _pTooltip;
            const utf8* _pMark;
        };
        static constexpr GizmoButton kArrGizmoButton[] = {
            {"##gizmoTranslate", editoricon::kTranslate, "Translate (W)", "gizmo.translate"},
            {   "##gizmoRotate",    editoricon::kRotate,    "Rotate (E)",    "gizmo.rotate"},
            {    "##gizmoScale",     editoricon::kScale,     "Scale (R)",     "gizmo.scale"},
        };
        const int32 operation = pContext->getWorkspace().getGizmoOperation();
        for ( int32 index = 0; index < 3; ++index )
        {
            const GizmoButton& button = kArrGizmoButton[index];
            if ( drawIconToggle( button._pID, button._pIcon, operation == index, button._pTooltip ) )
                pContext->getWorkspace().setGizmoOperation( index );
            EditorSelfTestMarks::note( button._pMark );
            ImGui::SameLine();
        }

        const bool bLocalSpace = pContext->getWorkspace().isGizmoLocalSpace();
        if ( drawToggleIconButton( "##gizmoSpace", bLocalSpace, editoricon::kAxes, editoricon::kGlobe, "Local space - click for world space",
                                   "World space - click for local space" ) )
            pContext->getWorkspace().setGizmoLocalSpace( bLocalSpace == false );
        EditorSelfTestMarks::note( "gizmo.space" );
    }

    void EditorWidgets::drawToolbarSeparator()
    {
        ImGui::SameLine();
        ImGui::TextDisabled( "|" );
        ImGui::SameLine();
    }

    bool EditorWidgets::drawToggleButton( const utf8* pLabel, bool bActive, const Color4& activeColor )
    {
        const Color4& color = bActive ? activeColor : style::kToggleInactive;
        ImGui::PushStyleColor( ImGuiCol_Button, EditorWidgetsInternal::toIm( color ) );
        const bool bClicked = ImGui::Button( pLabel );
        ImGui::PopStyleColor();
        return bClicked;
    }

    bool EditorWidgets::drawIconToggle( const utf8* pID, const utf8* pIcon, bool bActive, const utf8* pTooltip )
    {
        const float32 side = ImGui::GetFrameHeight();
        if ( bActive )
        {
            const Color4& accent = EditorThemeUtil::getAccentColor();
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ accent._r, accent._g, accent._b, 0.85f } );
        }
        ImGui::PushID( pID );
        const bool bPressed = ImGui::Button( pIcon, ImVec2{ side, side } );
        ImGui::PopID();
        if ( bActive )
            ImGui::PopStyleColor();
        drawTooltip( pTooltip );
        return bPressed;
    }

    bool EditorWidgets::drawToggleIconButton( const utf8* pID, bool bOn, const utf8* pIconOn, const utf8* pIconOff, const utf8* pTooltipOn,
                                              const utf8* pTooltipOff )
    {
        const float32 side = ImGui::GetFrameHeight();
        ImGui::PushID( pID );
        const bool bPressed = ImGui::Button( bOn ? pIconOn : pIconOff, ImVec2{ side, side } );
        ImGui::PopID();
        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", bOn ? pTooltipOn : pTooltipOff );
        return bPressed;
    }

    void EditorWidgets::drawClampedLabel( string_view text, float32 width, uint32 maxLineCount )
    {
        vector<string_view> listLine;
        EditorLabelLayoutUtil::breakLines( text, width, maxLineCount, &EditorWidgetsInternal::measureImGuiText, nullptr, listLine );
        ImDrawList*   pDrawList  = ImGui::GetWindowDrawList();
        const float32 lineHeight = ImGui::GetTextLineHeight();
        ImVec2        cursor     = ImGui::GetCursorScreenPos();
        for ( size_t index = 0; index < listLine.size(); ++index )
        {
            const string_view line  = listLine[index];
            const bool        bLast = index + 1 == listLine.size();
            const ImVec2      lineMax{ cursor.x + width, cursor.y + lineHeight };
            if ( bLast )
                ImGui::RenderTextEllipsis( pDrawList, cursor, lineMax, lineMax.x, line.data(), line.data() + line.size(), nullptr );
            else
                pDrawList->AddText( cursor, ImGui::GetColorU32( ImGuiCol_Text ), line.data(), line.data() + line.size() );
            cursor.y += lineHeight;
        }
        const uint32 rowCount = maxLineCount > 0 ? maxLineCount : 1u;
        ImGui::Dummy( ImVec2{ width, lineHeight * static_cast<float32>( rowCount ) } );
        // 마지막 줄이 넘쳤거나(말줄임) 줄 수가 모자라 남은 글이 있으면 전체 이름을 툴팁으로 보인다.
        const bool bLastLineOverflows = listLine.empty() == false && EditorWidgetsInternal::measureImGuiText( listLine.back(), nullptr ) > width;
        if ( bLastLineOverflows && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%.*s", static_cast<int32>( text.size() ), text.data() );
    }

    void EditorWidgets::drawEmptyHint( const utf8* pText )
    {
        if ( pText == nullptr )
            return;
        ImGui::TextDisabled( "%s", pText );
    }

    void EditorWidgets::drawNoSearchResultHint( string_view filter )
    {
        ++EditorWidgetsInternal::noSearchResultHintCount();
        if ( filter.empty() )
        {
            drawEmptyHint( "No matches." );
            return;
        }
        EditorWidgetsInternal::filteredHintFrame() = static_cast<uint32>( ImGui::GetFrameCount() );

        // 필터를 서식 **인자**로 넘긴다. 그래서 검색어에 '%' 가 들어와도 서식으로 해석되지 않는다.
        ImGui::TextDisabled( "No matches for \"%.*s\".", static_cast<int32>( filter.size() ), filter.data() );
    }

    uint32 EditorWidgets::getNoSearchResultHintCount()
    {
        return EditorWidgetsInternal::noSearchResultHintCount();
    }

    bool EditorWidgets::wasFilteredNoResultHintDrawnRecently()
    {
        const uint32 frame = EditorWidgetsInternal::filteredHintFrame();
        const uint32 now   = static_cast<uint32>( ImGui::GetFrameCount() );
        return frame != 0 && frame <= now && now - frame <= 1;
    }

    void EditorWidgets::drawCountLabel( uint32 visible, uint32 total, const utf8* pUnit )
    {
        const bool bHasUnit = ( StringUtil::isNullOrEmpty( pUnit ) == false );
        if ( total == 0 )
        {
            if ( bHasUnit )
                ImGui::TextDisabled( "%u %s", visible, pUnit );
            else
                ImGui::TextDisabled( "%u", visible );
            return;
        }

        if ( bHasUnit )
            ImGui::TextDisabled( "%u / %u %s", visible, total, pUnit );
        else
            ImGui::TextDisabled( "%u / %u", visible, total );
    }

    void EditorWidgets::drawPanelStatus( const utf8* pText )
    {
        if ( StringUtil::isNullOrEmpty( pText ) )
            return;
        ImGui::Separator();
        ImGui::TextDisabled( "%s", pText );
    }

    bool EditorWidgets::drawSearchField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, float32 width,
                                         bool bShowClear )
    {
        if ( pBuffer == nullptr || bufferBytes == 0 )
            return false;

        ImGui::PushID( pID != nullptr ? pID : "##search" );
        if ( width < 0.0f )
            ImGui::SetNextItemWidth( -1.0f );
        else if ( width > 0.0f )
            ImGui::SetNextItemWidth( width );
        else
        {
            const float32 avail      = ImGui::GetContentRegionAvail().x;
            const float32 clearWidth = bShowClear ? 28.0f : 0.0f;
            ImGui::SetNextItemWidth( ( avail > clearWidth + 4.0f ) ? ( avail - clearWidth ) : avail );
        }

        const utf8* pHintText = ( pHint != nullptr ) ? pHint : "Search...";
        bool        bChanged  = ImGui::InputTextWithHint( "##search", pHintText, pBuffer, bufferBytes );
        if ( bShowClear )
        {
            ImGui::SameLine();
            if ( ImGui::Button( "X", ImVec2{ 22.0f * EditorThemeUtil::getDpiScale(), 0.0f } ) && pBuffer[0] != '\0' )
            {
                pBuffer[0] = '\0';
                bChanged   = true;
            }
        }
        ImGui::PopID();
        return bChanged;
    }

    void EditorWidgets::drawChip( const utf8* pLabel, const Color4& color )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, EditorWidgetsInternal::toIm( color ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorWidgetsInternal::toIm( color ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorWidgetsInternal::toIm( color ) );
        ImGui::SmallButton( pLabel );
        ImGui::PopStyleColor( 3 );
    }

    void EditorWidgets::drawPropertyRowBegin( const utf8* pLabel, float32 labelWidth )
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled( "%s", pLabel );
        ImGui::SameLine( labelWidth );
        ImGui::SetNextItemWidth( -1.0f );
    }

    bool EditorWidgets::drawTextField( const utf8* pLabel, string& text, float32 width )
    {
        if ( width < 0.0f )
            ImGui::SetNextItemWidth( -1.0f );
        else if ( width > 0.0f )
            ImGui::SetNextItemWidth( width );

        // 버퍼로 `string` 을 그대로 넘긴다. 모자라면 아래 콜백이 늘려 주므로 길이 상한이 없다.
        return ImGui::InputText( pLabel != nullptr ? pLabel : "##text", text.data(), text.capacity() + 1,
                                 ImGuiInputTextFlags_CallbackResize, &EditorWidgetsInternal::resizeStringCallback, &text );
    }

    bool EditorWidgets::drawTextFieldMultiline( const utf8* pLabel, string& text, const uint32 lineCount )
    {
        const ImVec2 size( -1.0f, ImGui::GetTextLineHeight() * static_cast<float32>( lineCount ) + ImGui::GetStyle().FramePadding.y * 2.0f );
        return ImGui::InputTextMultiline( pLabel != nullptr ? pLabel : "##text", text.data(), text.capacity() + 1, size, ImGuiInputTextFlags_CallbackResize,
                                          &EditorWidgetsInternal::resizeStringCallback, &text );
    }

    bool EditorWidgets::drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt, float32 labelWidth )
    {
        ImGui::PushID( pLabel );
        bool bChanged{ false };

        if ( StringUtil::isNullOrEmpty( pLabel ) == false )
            drawPropertyRowBegin( pLabel, labelWidth );

        const float32 availWidth    = ImGui::GetContentRegionAvail().x;
        const float32 clearBtnWidth = 24.0f;
        const float32 inputWidth    = ( assetPath.empty() == false ) ? ( availWidth - clearBtnWidth - 4.0f ) : availWidth;

        const utf8* pDisplayPath = assetPath.empty() ? "(None / Drop Asset)" : assetPath.c_str();
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f } );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f } );
        ImGui::Button( pDisplayPath, ImVec2{ inputWidth, 0.0f } );
        ImGui::PopStyleColor( 2 );

        string droppedPath;
        if ( acceptAssetDrop( droppedPath ) )
        {
            const bool bAccept = StringUtil::isNullOrEmpty( pExpectedExt ) || FileUtil::hasExtension( droppedPath, pExpectedExt );
            if ( bAccept )
            {
                assetPath = droppedPath;
                bChanged  = true;
            }
        }

        if ( assetPath.empty() == false )
        {
            ImGui::SameLine();
            if ( ImGui::Button( "x", ImVec2{ clearBtnWidth, 0.0f } ) )
            {
                assetPath.clear();
                bChanged = true;
            }
        }

        ImGui::PopID();
        return bChanged;
    }

    bool EditorWidgets::drawColorEdit( const utf8* pLabel, Color4& color, float32 labelWidth )
    {
        ImGui::PushID( pLabel );
        if ( StringUtil::isNullOrEmpty( pLabel ) == false )
            drawPropertyRowBegin( pLabel, labelWidth );
        else
            ImGui::SetNextItemWidth( -1.0f );
        float32 arrColor[4]{ color._r, color._g, color._b, color._a };
        bool    bChanged{ false };
        if ( ImGui::ColorEdit4( "##color", arrColor, ImGuiColorEditFlags_AlphaBar ) )
        {
            color._r = arrColor[0];
            color._g = arrColor[1];
            color._b = arrColor[2];
            color._a = arrColor[3];
            bChanged = true;
        }
        ImGui::PopID();
        return bChanged;
    }

    void EditorWidgets::pushInspectorStyle()
    {
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 1.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2.0f, 2.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 5.0f );
    }

    void EditorWidgets::popInspectorStyle()
    {
        ImGui::PopStyleVar( 3 );
    }

    void EditorWidgets::drawAssetDragSource( const utf8* pRelativePath, bool bAllowNullID )
    {
        if ( StringUtil::isNullOrEmpty( pRelativePath ) )
            return;

        ImGuiDragDropFlags flags = 0;
        if ( bAllowNullID )
            flags |= ImGuiDragDropFlags_SourceAllowNullID;

        if ( ImGui::BeginDragDropSource( flags ) == false )
            return;

        const uint32 pathBytes = StringUtil::strlen( pRelativePath ) + 1;
        ImGui::SetDragDropPayload( kAssetPathPayload, pRelativePath, pathBytes );
        ImGui::TextUnformatted( pRelativePath );
        ImGui::EndDragDropSource();
    }

    bool EditorWidgets::tryAcceptAssetPayload( string& outPath )
    {
        const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( kAssetPathPayload );
        if ( pPayload == nullptr || pPayload->Data == nullptr )
            return false;

        outPath = static_cast<const utf8*>( pPayload->Data );
        return true;
    }

    bool EditorWidgets::acceptAssetDrop( string& outPath )
    {
        if ( ImGui::BeginDragDropTarget() == false )
            return false;

        const bool bAccepted = tryAcceptAssetPayload( outPath );
        ImGui::EndDragDropTarget();
        return bAccepted;
    }

    bool EditorWidgets::updateListSelection( int32& selectedIndex, int32 itemCount, bool bRepeat )
    {
        if ( itemCount <= 0 )
        {
            selectedIndex = 0;
            return false;
        }

        if ( selectedIndex >= itemCount )
            selectedIndex = itemCount - 1;
        if ( selectedIndex < 0 )
            selectedIndex = 0;

        if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow, bRepeat ) )
        {
            ++selectedIndex;
            if ( selectedIndex >= itemCount )
                selectedIndex = itemCount - 1;
        }
        if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow, bRepeat ) )
        {
            --selectedIndex;
            if ( selectedIndex < 0 )
                selectedIndex = 0;
        }

        const bool bValidSelection = ( 0 <= selectedIndex && selectedIndex < itemCount );
        if ( bValidSelection == false )
            return false;
        return ImGui::IsKeyPressed( ImGuiKey_Enter, bRepeat );
    }

    EditorUnsavedChoice EditorWidgets::drawUnsavedChangesModal( const utf8* pPopupID, const utf8* pMessage )
    {
        if ( StringUtil::isNullOrEmpty( pPopupID ) )
            return EditorUnsavedChoice::None;

        if ( ImGui::BeginPopupModal( pPopupID, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) == false )
            return EditorUnsavedChoice::None;

        ImGui::TextUnformatted( pMessage != nullptr ? pMessage : "You have unsaved changes." );
        EditorUnsavedChoice choice = EditorUnsavedChoice::None;
        if ( ImGui::Button( "Save" ) )
            choice = EditorUnsavedChoice::Save;
        ImGui::SameLine();
        if ( ImGui::Button( "Don't Save" ) )
            choice = EditorUnsavedChoice::Discard;
        ImGui::SameLine();
        if ( ImGui::Button( "Cancel" ) )
            choice = EditorUnsavedChoice::Cancel;
        if ( choice != EditorUnsavedChoice::None )
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return choice;
    }

    void EditorWidgets::drawTooltip( const utf8* pText )
    {
        if ( StringUtil::isNullOrEmpty( pText ) == false && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos( ImGui::GetFontSize() * 35.0f );
            ImGui::TextUnformatted( pText );
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void EditorWidgets::drawHelpMarker( const utf8* pDesc )
    {
        ImGui::TextDisabled( "(?)" );
        drawTooltip( pDesc );
    }
} // namespace sw::editor
