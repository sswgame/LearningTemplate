#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>
#include <imgui_internal.h>

// EditorAssetFilter — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
// EditorLabelLayout — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
namespace sw::editor
{
    /**
     * @struct EditorLabelLayoutUtil
     * @brief 이름을 폭 안의 줄로 나눕니다. 끊는 위치는 공백 · '_' · '-' · '.' 뒤가 먼저이고, 없을 때만 글자 단위입니다.
     * @details ImGui 의 TextWrap 은 공백만 보므로 `nilecity` 같은 이름이 글자 가운데서 끊깁니다. 그리는 쪽은
     *          `EditorWidgets::drawClampedLabel` 이고(마지막 줄 말줄임 · 전체 이름 툴팁), 여기는 어디서 끊는지만 정합니다.
     */
    struct EditorLabelLayoutUtil
    {
        /** @brief 글자열 너비를 측정하는 함수입니다(ImGui 는 CalcTextSize, 테스트는 글자 수 × 고정 폭). */
        using MeasureFunc = float32 ( * )( string_view text, void* pUserData );

        /**
         * @brief @p text 를 @p width 안의 줄로 최대 @p maxLineCount 개 나눠 @p outListLine 에 채웁니다(먼저 비운다).
         * @details 마지막 줄은 넘쳐도 남은 전부다 — 말줄임은 그리는 쪽이 한다. 줄 머리의 공백은 버린다. 빈 글은 빈 줄 하나다.
         *          UTF-8 글자 가운데서 끊지 않는다.
         */
        static void breakLines( string_view text, float32 width, uint32 maxLineCount, MeasureFunc pfnMeasure, void* pUserData,
                                vector<string_view>& outListLine );
    };
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorLabelLayoutInternal
        {
            /** @brief 이 글자 뒤에서 줄을 끊어도 되는가(단어 경계)입니다. */
            static bool isBreakAfter( utf8 character ) { return character == ' ' || character == '_' || character == '-' || character == '.'; }

            /** @brief 줄 머리의 공백을 건너뜁니다. */
            static string_view skipLeadingSpace( string_view text )
            {
                size_t offset = 0;
                while ( offset < text.size() && text[offset] == ' ' )
                {
                    ++offset;
                }
                return text.substr( offset );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorLabelLayoutUtil::breakLines( string_view text, float32 width, uint32 maxLineCount, MeasureFunc pfnMeasure, void* pUserData,
                                            vector<string_view>& outListLine )
    {
        outListLine.clear();
        const bool bCannotWrapLabel = text.empty() || maxLineCount == 0 || pfnMeasure == nullptr;
        if ( bCannotWrapLabel )
        {
            outListLine.push_back( text );
            return;
        }

        string_view remaining = text;
        while ( outListLine.size() + 1 < maxLineCount && pfnMeasure( remaining, pUserData ) > width )
        {
            // 폭에 들어가는 가장 긴 앞부분(글자 경계)을 찾는다. 첫 글자는 넘쳐도 싣는다 — 빈 줄이 끝없이 생기지 않게.
            size_t fitEnd = 0;
            while ( fitEnd < remaining.size() )
            {
                size_t next = fitEnd;
                (void)StringUtil::decodeUTF8( remaining, next );
                const bool bOverflows = pfnMeasure( remaining.substr( 0, next ), pUserData ) > width;
                if ( fitEnd != 0 && bOverflows )
                    break;
                fitEnd = next;
            }

            // 그 안에서 마지막 단어 경계 뒤를 고른다. 없으면 글자 단위다.
            size_t cut = fitEnd;
            for ( size_t index = fitEnd; index > 0; --index )
            {
                if ( EditorLabelLayoutInternal::isBreakAfter( remaining[index - 1] ) )
                {
                    cut = index;
                    break;
                }
            }
            outListLine.push_back( remaining.substr( 0, cut ) );
            remaining = EditorLabelLayoutInternal::skipLeadingSpace( remaining.substr( cut ) );
            if ( remaining.empty() )
                return;
        }
        outListLine.push_back( remaining );
    }
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorAssetFilter
     * @brief 필터는 쉼표 또는 공백으로 나눈 확장자 목록입니다. 한 개짜리(`".hlsl"`)도 같은 규칙으로 읽습니다.
     */
    struct EditorAssetFilter
    {
        /** @brief 필터 문자열을 확장자 목록으로 나눕니다. 빈 조각은 버립니다. 뷰는 @p filter 를 가리킵니다. */
        static vector<string_view> splitExtensions( string_view filter );
        /** @brief 필터가 비었거나 @p path 가 목록의 확장자 하나와 맞으면(대소문자 무시) true 입니다. */
        static bool matches( string_view path, string_view filter );
    };
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        /** @brief 필터 글의 확장자 구분자(쉼표 · 공백 · 탭 · 세미콜론)입니다. */
        constexpr const utf8* kFilterSeparators = ", \t;";
    } // namespace

    vector<string_view> EditorAssetFilter::splitExtensions( string_view filter )
    {
        return StringUtil::split( filter, kFilterSeparators, { ._bDropEmpty = true } );
    }

    bool EditorAssetFilter::matches( string_view path, string_view filter )
    {
        const vector<string_view> listExtension = splitExtensions( filter );
        for ( const string_view extension : listExtension )
        {
            if ( FileUtil::hasExtension( path, extension ) )
                return true;
        }
        return listExtension.empty();
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorWidgetsInternal
        {
            /** @brief 0 건 안내를 그린 기록입니다. 감시 값이 읽는다. 에디터 범위의 상태 저장소가 소유해 에디터마다(테스트마다) 따로 센다. */
            struct HintState
            {
                uint32 _noSearchResultHintCount{ 0 }; ///< 0 건 안내를 그린 횟수(`getNoSearchResultHintCount`)
                uint32 _filteredHintFrame{ 0 };       ///< 검색어가 있는 0 건 안내를 마지막으로 그린 ImGui 프레임(0 = 그린 적 없음)
            };

            /** @brief ImGui 글꼴로 글자열 너비를 측정합니다(`EditorLabelLayoutUtil::MeasureFunc`). */
            static float32 measureImGuiText( string_view text, void* /*pUserData*/ )
            {
                return ImGui::CalcTextSize( text.data(), text.data() + text.size() ).x;
            }

            /** @brief 애셋 슬롯 버튼 바탕(평소 · 호버)입니다. */
            static constexpr Color kAssetSlotButton{ 0.15f, 0.15f, 0.15f, 1.0f };
            static constexpr Color kAssetSlotButtonHovered{ 0.25f, 0.25f, 0.25f, 1.0f };

            /**
             * @brief InputText 가 요구하는 만큼 `string` 버퍼를 늘려 줍니다.
             * @details ImGui 는 알고 있는 크기를 넘는 입력이 오면 이 콜백으로 되묻습니다. 늘린 뒤 새 주소를 알려 주지 않으면 옛
             *          버퍼를 계속 쓰므로, `Buf` 갱신까지가 한 쌍입니다.
             */
            /**
             * @brief ImGui 이름표(`"Name###id"`)의 보이는 부분이 있으면 칸 앞(왼쪽)에 흐린 이름표로 그립니다. 앞서 건 다음 항목 폭은 칸에 그대로 남깁니다.
             * @details ImGui 는 보이는 이름표를 칸 오른쪽에 붙여 "Client [v] Target Empty [v] Game" 처럼 이름이 다음 칸의 것으로 읽혔다(uiaudit — Packaging).
             *          글 칸 위젯이 이것으로 이름표를 앞으로 옮기므로 부르는 쪽 열아홉 곳을 고치지 않는다(언리얼 · 유니티 · Godot 모두 이름표는 왼쪽).
             */
            static void drawLeadingLabel( const utf8* pLabel )
            {
                const utf8* pVisibleEnd = ImGui::FindRenderedTextEnd( pLabel );
                if ( pVisibleEnd == pLabel )
                    return;
                ImGuiContext& g            = *ImGui::GetCurrentContext();
                const bool    bHasWidth    = ( g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasWidth ) != 0;
                const float32 pendingWidth = g.NextItemData.Width;
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled( "%.*s", static_cast<int32>( pVisibleEnd - pLabel ), pLabel );
                ImGui::SameLine();
                if ( bHasWidth )
                    ImGui::SetNextItemWidth( pendingWidth );
            }

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

            /** @brief 벡터 컨트롤의 축 버튼이 호버에서 밝아지는 정도(채널마다 더한다)입니다. */
            static constexpr float32 kAxisHoverBrighten = 0.1f;

            /**
             * @brief 벡터 컨트롤의 축 하나(색 버튼 + 드래그 입력)를 그립니다. 버튼을 누르면 값이 `resetValue` 로 돌아갑니다.
             * @return 값이 바뀌었으면 true
             */
            static bool drawAxis( const utf8* pAxisLabel, float32& axisValue, const Color& color, const ImVec2& buttonSize, float32 resetValue, float32 speed )
            {
                bool bChanged{ false };
                ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( color ) );
                const Color hovered{ color._r + kAxisHoverBrighten, color._g + kAxisHoverBrighten, color._b + kAxisHoverBrighten, 1.0f };
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( hovered ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorColorUtil::toImVec4( color ) );
                if ( ImGui::Button( pAxisLabel, buttonSize ) )
                {
                    axisValue = resetValue;
                    bChanged  = true;
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "Vec3AxisReset" ) );
                ImGui::PopStyleColor( 3 );
                ImGui::SameLine();
                ImGui::PushID( pAxisLabel );
                if ( ImGui::DragFloat( "##v", &axisValue, speed, 0.0f, 0.0f, "%.2f" ) )
                    bChanged = true;
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "Vec3AxisDrag" ) );
                ImGui::PopID();
                ImGui::PopItemWidth();
                ImGui::SameLine();
                return bChanged;
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
        ImGui::SetColumnWidth( 0, columnWidth * EditorThemeUtil::getDPIScale() );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( pLabel );
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths( 3, ImGui::CalcItemWidth() );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f } );

        const float32 lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
        const ImVec2  buttonSize{ lineHeight + 3.0f, lineHeight };
        bool          bChanged{ false };

        // 비단락(|=)으로 세 축을 모두 그린다.
        bChanged |= EditorWidgetsInternal::drawAxis( "X", values._x, SW_THEME_COLOR( "Viewport", "AxisX" ), buttonSize, resetValue, speed );
        bChanged |= EditorWidgetsInternal::drawAxis( "Y", values._y, SW_THEME_COLOR( "Viewport", "AxisY" ), buttonSize, resetValue, speed );
        bChanged |= EditorWidgetsInternal::drawAxis( "Z", values._z, SW_THEME_COLOR( "Viewport", "AxisZ" ), buttonSize, resetValue, speed );

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
            const Color accent = SW_THEME_COLOR( "Base", "Accent" );
            ImGui::PushStyleColor( ImGuiCol_Header, EditorColorUtil::toImVec4( accent.withAlpha( 0.85f ) ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderHovered, EditorColorUtil::toImVec4( accent.scaleBrightness( 1.15f ).withAlpha( 1.0f ) ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderActive, EditorColorUtil::toImVec4( accent.scaleBrightness( 1.30f ).withAlpha( 1.0f ) ) );
        }
        else
        {
            const Color headerColor = SW_THEME_COLOR( "Base", "HeaderBg" );
            const Color accent      = SW_THEME_COLOR( "Base", "Accent" );
            ImGui::PushStyleColor( ImGuiCol_Header, EditorColorUtil::toImVec4( headerColor.withAlpha( 1.0f ) ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderHovered, EditorColorUtil::toImVec4( accent.withAlpha( 0.70f ) ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderActive, EditorColorUtil::toImVec4( accent.withAlpha( 0.90f ) ) );
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
            drawTooltip( SW_LOCKEY( "EdCommon", "ComponentCardActive" ) );
        }

        ImGui::SameLine( availX - lineHeight * 0.5f );
        if ( ImGui::Button( "+", ImVec2{ lineHeight, lineHeight } ) )
            EditorInputGUI::openPopup( "ComponentSettings" );
        drawTooltip( SW_LOCKEY( "EdCommon", "ComponentCardOptions" ) );

        if ( ImGui::BeginPopup( "ComponentSettings" ) )
        {
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "RemoveComponent" ) ) && pBRemoveRequested != nullptr )
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

    void EditorWidgets::drawGizmoOperationControls( const utf8* pMarkScope, bool bVertical )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        // 값은 `EditorWorkspace` 의 기즈모 조작 번호다. 여기 숫자는 그 표현일 뿐이고, 뜻은 워크스페이스가 정한다(ImGuizmo OPERATION 과 짝이다).
        // 세 엔진 모두 이동 · 회전 · 크기를 아이콘 단추로 둔다 — 글자 라디오는 좁은 뷰포트에서 잘린다.
        struct GizmoButton
        {
            const utf8*    _pID;
            EditorIconName _pIconName;
            const utf8*    _pMarkSuffix;
        };
        static constexpr GizmoButton kArrGizmoButton[] = {
            {"##gizmoTranslate", SW_EDITOR_ICON_NAME( "Translate" ), ".translate"},
            {   "##gizmoRotate",    SW_EDITOR_ICON_NAME( "Rotate" ),    ".rotate"},
            {    "##gizmoScale",     SW_EDITOR_ICON_NAME( "Scale" ),     ".scale"},
        };
        // 툴팁은 지금 문화권의 글이라 상수 테이블에 넣지 않고 그릴 때 찾는다.
        const utf8* const arrTooltip[] = {
            SW_LOCKEY( "EdCommon", "GizmoTranslate" ),
            SW_LOCKEY( "EdCommon", "GizmoRotate" ),
            SW_LOCKEY( "EdCommon", "GizmoScale" ),
        };
        const int32 operation = editorContext.getSubsystem<EditorWorkspace>().getGizmoOperation();
        for ( int32 index = 0; index < 3; ++index )
        {
            const GizmoButton& button = kArrGizmoButton[index];
            if ( drawIconToggle( button._pID, EditorIconGlyphTable::findGlyphByName( button._pIconName ), operation == index, arrTooltip[index] ) )
                editorContext.getSubsystem<EditorWorkspace>().setGizmoOperation( index );
            EditorSelfTestMarks::record( ( string( pMarkScope ) + button._pMarkSuffix ).c_str() );
            if ( bVertical == false )
                ImGui::SameLine();
        }

        const bool bLocalSpace = editorContext.getSubsystem<EditorWorkspace>().isGizmoLocalSpace();
        if ( drawToggleIconButton( "##gizmoSpace", bLocalSpace, SW_EDITOR_ICON( "Axes" ), SW_EDITOR_ICON( "Globe" ),
                                   SW_LOCKEY( "EdCommon", "GizmoLocalSpace" ),
                                   SW_LOCKEY( "EdCommon", "GizmoWorldSpace" ) ) )
            editorContext.getSubsystem<EditorWorkspace>().setGizmoLocalSpace( bLocalSpace == false );
        EditorSelfTestMarks::record( ( string( pMarkScope ) + ".space" ).c_str() );
    }

    void EditorWidgets::drawToolbarSeparator()
    {
        ImGui::SameLine();
        ImGui::SeparatorEx( ImGuiSeparatorFlags_Vertical );
        ImGui::SameLine();
    }

    bool EditorWidgets::drawToggleButton( const utf8* pLabel, bool bActive )
    {
        const Color& color = bActive ? SW_THEME_COLOR( "Base", "ToggleActive" ) : SW_THEME_COLOR( "Base", "ToggleInactive" );
        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( color ) );
        const bool bClicked = ImGui::Button( pLabel );
        ImGui::PopStyleColor();
        return bClicked;
    }

    bool EditorWidgets::drawIconToggle( const utf8* pID, const utf8* pIcon, bool bActive, const utf8* pTooltip )
    {
        // 툴바 아이콘은 상자 없이 둔다(언리얼 · 유니티 뷰포트 툴바) — 켜진 것만 중립 단계 바탕이 깔린다.
        const float32 side = ImGui::GetFrameHeight();
        ImGui::PushStyleColor( ImGuiCol_Button, bActive ? EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "ToggleActive" ) ) : ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f } );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushID( pID );
        const bool bPressed = ImGui::Button( pIcon, ImVec2{ side, side } );
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        drawTooltip( pTooltip );
        return bPressed;
    }

    bool EditorWidgets::drawToggleIconButton( const utf8* pID, bool bOn, const utf8* pIconOn, const utf8* pIconOff, const utf8* pTooltipOn,
                                              const utf8* pTooltipOff )
    {
        // 아이콘 자체가 상태를 보이므로 상자 없이 그린다.
        const float32 side = ImGui::GetFrameHeight();
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f } );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushID( pID );
        const bool bPressed = ImGui::Button( bOn ? pIconOn : pIconOff, ImVec2{ side, side } );
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", bOn ? pTooltipOn : pTooltipOff );
        return bPressed;
    }

    bool EditorWidgets::drawRowIconToggle( const utf8* pID, bool bOn, bool bDefaultOn, const utf8* pIconOn, const utf8* pIconOff, const utf8* pTooltipOn,
                                           const utf8* pTooltipOff )
    {
        const bool bDefault = bOn == bDefaultOn;
        if ( bDefault )
            EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Base", "TextMuted" ) );
        const bool bPressed = drawToggleIconButton( pID, bOn, pIconOn, pIconOff, pTooltipOn, pTooltipOff );
        if ( bDefault )
            EditorThemeUtil::popTextColor();
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
        // 안내문은 패널 폭에서 줄바꿈한다. 한 줄로 두면 좁은 도크에서 다음 행동을 알려 주는 뒷부분이 잘린다(UX 감사).
        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
        ImGui::TextWrapped( "%s", pText );
        ImGui::PopStyleColor();
    }

    void EditorWidgets::drawNoSearchResultHint( string_view filter )
    {
        EditorWidgetsInternal::HintState& hintState = getEditorState<EditorWidgetsInternal::HintState>();
        ++hintState._noSearchResultHintCount;
        if ( filter.empty() )
        {
            drawEmptyHint( SW_LOCKEY( "EdCommon", "NoMatches" ) );
            return;
        }
        hintState._filteredHintFrame = static_cast<uint32>( ImGui::GetFrameCount() );

        // 필터를 서식 **인자**로 넘긴다. 그래서 검색어에 '{' · '%' 가 들어와도 서식으로 해석되지 않는다.
        const string text = SW_LOCKEYFORMAT( "EdCommon", "NoMatchesForFilter", TextArgumentList().addText( "filter", filter ) );
        ImGui::TextDisabled( "%s", text.c_str() );
    }

    uint32 EditorWidgets::getNoSearchResultHintCount()
    {
        return getEditorState<EditorWidgetsInternal::HintState>()._noSearchResultHintCount;
    }

    bool EditorWidgets::wasFilteredNoResultHintDrawnRecently()
    {
        const uint32 frame = getEditorState<EditorWidgetsInternal::HintState>()._filteredHintFrame;
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

    string EditorWidgets::makeStatusText( const Error& error )
    {
        if ( error.detail().empty() )
            return string( error.message() );
        StringBuilder<constant::buffer::kMax256> text;
        text.appendFormat( "%#: %#", error.message(), error.detail() );
        return string( text.view() );
    }

    bool EditorWidgets::drawSearchField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, float32 width,
                                         bool bShowClear, const utf8* pMark )
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
            const float32     avail          = ImGui::GetContentRegionAvail().x;
            const float32     clearWidth     = bShowClear ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
            constexpr float32 kMinFieldWidth = 4.0f; ///< 지우기 단추를 빼고도 입력란에 남아야 하는 최소 폭
            ImGui::SetNextItemWidth( ( avail > clearWidth + kMinFieldWidth ) ? ( avail - clearWidth ) : avail );
        }

        const utf8* pHintText = ( pHint != nullptr ) ? pHint : SW_LOCKEY( "EdCommon", "SearchHint" );
        bool        bChanged  = ImGui::InputTextWithHint( "##search", pHintText, pBuffer, bufferBytes );
        if ( pMark != nullptr )
            EditorSelfTestMarks::record( pMark );
        if ( bShowClear )
        {
            ImGui::SameLine();
            // 글자 "X" 대신 닫기 아이콘을 상자 없이(아이콘 토글과 같은 모양) 둔다.
            if ( drawToggleIconButton( "##clear", true, SW_EDITOR_ICON( "Close" ), SW_EDITOR_ICON( "Close" ), SW_LOCKEY( "EdCommon", "SearchClear" ), SW_LOCKEY( "EdCommon", "SearchClear" ) ) &&
                 pBuffer[0] != '\0' )
            {
                pBuffer[0] = '\0';
                bChanged   = true;
            }
        }
        ImGui::PopID();
        return bChanged;
    }

    void EditorWidgets::drawChip( const utf8* pLabel, const Color& color )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( color ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( color ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorColorUtil::toImVec4( color ) );
        ImGui::SmallButton( pLabel );
        ImGui::PopStyleColor( 3 );
    }

    void EditorWidgets::drawPropertyRowLabel( const utf8* pLabel, const utf8* pTooltip )
    {
        const float32 columnWidth = SW_THEME_METRIC( "Layout", "PropertyLabelWidth" ) * EditorThemeUtil::getDPIScale(); // 예전 고정 120px 은 DPI 를 몰랐다
        const utf8*   pText       = pLabel != nullptr ? pLabel : "";
        ImGui::AlignTextToFramePadding();
        // 라벨은 열 폭 안에서 말줄임으로 자른다 — 길면 입력 칸 위에 겹쳐 그려졌다(uiaudit). 잘렸을 때만 전체 라벨을 툴팁에 더한다.
        const float32 labelWidth = columnWidth - ImGui::GetStyle().ItemSpacing.x;
        const ImVec2  textSize   = ImGui::CalcTextSize( pText );
        const bool    bClipped   = textSize.x > labelWidth;
        const ImVec2  cursor     = ImGui::GetCursorScreenPos();
        const float32 textY      = cursor.y + ImGui::GetStyle().FramePadding.y;
        const ImVec2  textMax{ cursor.x + labelWidth, textY + textSize.y };
        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
        ImGui::RenderTextEllipsis( ImGui::GetWindowDrawList(), ImVec2{ cursor.x, textY }, textMax, textMax.x, pText, nullptr, &textSize );
        ImGui::PopStyleColor();
        ImGui::Dummy( ImVec2{ bClipped ? labelWidth : textSize.x, ImGui::GetFrameHeight() } );
        if ( bClipped || StringUtil::isNullOrEmpty( pTooltip ) == false )
        {
            if ( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "TooltipWrapEm" ) );
                if ( bClipped )
                    ImGui::TextUnformatted( pText );
                if ( StringUtil::isNullOrEmpty( pTooltip ) == false )
                    ImGui::TextDisabled( "%s", pTooltip );
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::SameLine( columnWidth );
        ImGui::SetNextItemWidth( -1.0f );
    }

    void EditorWidgets::drawInlineLabel( const utf8* pLabel, const utf8* pTooltip )
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled( "%s", pLabel );
        drawTooltip( pTooltip );
        ImGui::SameLine();
    }

    bool EditorWidgets::drawEmptyState( const utf8* pText, const utf8* pActionLabel )
    {
        drawEmptyHint( pText );
        if ( StringUtil::isNullOrEmpty( pActionLabel ) )
            return false;
        return ImGui::Button( pActionLabel );
    }

    bool EditorWidgets::beginAdvancedSection( const utf8* pID )
    {
        // 머리글 이름표는 "Advanced" 하나로 모든 패널이 같다(용어 일관성). 펼침 상태 키는 @p pID 라 언어를 바꿔도 유지된다.
        return ImGui::CollapsingHeader( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "Advanced" ), pID != nullptr ? pID : "advanced" ), ImGuiTreeNodeFlags_None );
    }

    bool EditorWidgets::drawFilterBar( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, uint32 visible, uint32 total, const utf8* pUnit, const utf8* pMark )
    {
        // 건수 글을 먼저 재고, 검색 칸이 그 폭을 뺀 남은 폭을 쓴다. 건수는 줄 오른쪽 끝에 붙는다.
        utf8       countText[64];
        const bool bHasUnit = StringUtil::isNullOrEmpty( pUnit ) == false;
        if ( total == 0 && bHasUnit )
            formatstring( countText, sizeof( countText ), "%u %s", visible, pUnit );
        else if ( total == 0 )
            formatstring( countText, sizeof( countText ), "%u", visible );
        else if ( bHasUnit )
            formatstring( countText, sizeof( countText ), "%u / %u %s", visible, total, pUnit );
        else
            formatstring( countText, sizeof( countText ), "%u / %u", visible, total );
        const float32 countWidth = ImGui::CalcTextSize( countText ).x;
        const float32 spacing    = ImGui::GetStyle().ItemSpacing.x;
        const float32 avail      = ImGui::GetContentRegionAvail().x;
        const float32 fieldWidth = avail - countWidth - spacing;
        const bool    bChanged   = drawSearchField( pID, pBuffer, bufferBytes, pHint, fieldWidth > 0.0f ? fieldWidth : -1.0f, false, pMark );
        if ( fieldWidth > 0.0f )
        {
            ImGui::SameLine( 0.0f, spacing );
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled( "%s", countText );
        }
        return bChanged;
    }

    bool EditorWidgets::drawTextField( const utf8* pLabel, string& text, float32 width )
    {
        if ( width < 0.0f )
            ImGui::SetNextItemWidth( -1.0f );
        else if ( width > 0.0f )
            ImGui::SetNextItemWidth( width );

        // 버퍼로 `string` 을 그대로 넘긴다. 모자라면 아래 콜백이 늘려 주므로 길이 상한이 없다. id 는 이름표 전체(`###` 뒤만 — 언어와 무관)다.
        const utf8* pID = pLabel != nullptr ? pLabel : "##text";
        EditorWidgetsInternal::drawLeadingLabel( pID );
        ImGui::PushID( pID );
        const bool bChanged = ImGui::InputText( "##value", text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
                                                &EditorWidgetsInternal::resizeStringCallback, &text );
        ImGui::PopID();
        return bChanged;
    }

    bool EditorWidgets::drawTextFieldMultiline( const utf8* pLabel, string& text, const uint32 lineCount, const utf8* pHint, const int32 flags )
    {
        const ImVec2 size( -1.0f, ImGui::GetTextLineHeight() * static_cast<float32>( lineCount ) + ImGui::GetStyle().FramePadding.y * 2.0f );
        const utf8*  pID = pLabel != nullptr ? pLabel : "##text";
        EditorWidgetsInternal::drawLeadingLabel( pID );
        ImGui::PushID( pID );
        const bool bChanged = ImGui::InputTextMultiline( "##value", text.data(), text.capacity() + 1, size, static_cast<ImGuiInputTextFlags>( flags ) | ImGuiInputTextFlags_CallbackResize,
                                                         &EditorWidgetsInternal::resizeStringCallback, &text );
        if ( StringUtil::isNullOrEmpty( pHint ) == false && text.empty() && ImGui::IsItemActive() == false )
        {
            const ImVec2 hintPos{ ImGui::GetItemRectMin().x + ImGui::GetStyle().FramePadding.x, ImGui::GetItemRectMin().y + ImGui::GetStyle().FramePadding.y };
            ImGui::GetWindowDrawList()->AddText( hintPos, ImGui::GetColorU32( ImGuiCol_TextDisabled ), pHint );
        }
        ImGui::PopID();
        return bChanged;
    }

    bool EditorWidgets::beginCombo( const utf8* pLabel, const utf8* pPreview, int32 flags )
    {
        EditorWidgetsInternal::drawLeadingLabel( pLabel );
        // 보이는 이름표를 감춘 같은 id: "##" 를 앞에 붙인다. `###` 가 있으면 ImGui 가 그 뒤만 해시하므로 언어를 바꿔도 id 가 같다.
        fixed_string<constant::buffer::kMax128> hiddenLabel{ "##" };
        hiddenLabel.append( pLabel );
        return ImGui::BeginCombo( hiddenLabel.c_str(), pPreview, static_cast<ImGuiComboFlags>( flags ) );
    }

    EditorWidgets::HintFieldResult EditorWidgets::drawHintField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, const utf8* pMark,
                                                                 bool bSubmitOnEnter )
    {
        HintFieldResult result;
        if ( pBuffer == nullptr || bufferBytes == 0 )
            return result;
        const bool bEnter             = ImGui::InputTextWithHint( pID, pHint != nullptr ? pHint : "", pBuffer, bufferBytes,
                                                      bSubmitOnEnter ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_None );
        result._bSubmitted            = bSubmitOnEnter && bEnter;
        result._bChanged              = ImGui::IsItemEdited();
        result._bDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        if ( pMark != nullptr )
            EditorSelfTestMarks::record( pMark );
        return result;
    }

    EditorWidgets::HintFieldResult EditorWidgets::drawAssetPathField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, const utf8* pExpectedExt,
                                                                      const utf8* pMark )
    {
        HintFieldResult result = drawHintField( pID, pBuffer, bufferBytes, pHint, nullptr );
        string          droppedPath;
        const bool      bDropped = pBuffer != nullptr && acceptAssetDrop( droppedPath, pExpectedExt ) && droppedPath.size() < bufferBytes;
        if ( bDropped )
        {
            Memory::copy( pBuffer, droppedPath.c_str(), droppedPath.size() + 1 );
            result._bChanged              = SW_TRUE;
            result._bDeactivatedAfterEdit = true;
        }
        if ( pMark != nullptr )
            EditorSelfTestMarks::record( pMark );
        return result;
    }

    bool EditorWidgets::drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt )
    {
        ImGui::PushID( pLabel );
        bool bChanged{ false };

        if ( StringUtil::isNullOrEmpty( pLabel ) == false )
            drawPropertyRowLabel( pLabel );

        const float32 availWidth    = ImGui::GetContentRegionAvail().x;
        const float32 clearBtnWidth = 24.0f;
        const float32 inputWidth    = ( assetPath.empty() == false ) ? ( availWidth - clearBtnWidth - 4.0f ) : availWidth;

        const utf8* pDisplayPath = assetPath.empty() ? "(None / Drop Asset)" : assetPath.c_str();
        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( EditorWidgetsInternal::kAssetSlotButton ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( EditorWidgetsInternal::kAssetSlotButtonHovered ) );
        ImGui::Button( pDisplayPath, ImVec2{ inputWidth, 0.0f } );
        ImGui::PopStyleColor( 2 );
        drawTooltip( SW_LOCKEY( "EdCommon", "AssetSlotDrop" ) );
        const ImGuiLastItemData slotItem = ImGui::GetCurrentContext()->LastItemData;

        string droppedPath;
        // 필터는 쉼표 · 공백으로 나눈 여러 확장자다(".png,.jpg"). 한 개짜리도 같은 규칙이다.
        if ( acceptAssetDrop( droppedPath, pExpectedExt ) )
        {
            assetPath = droppedPath;
            bChanged  = true;
        }

        if ( assetPath.empty() == false )
        {
            ImGui::SameLine();
            if ( ImGui::Button( "x", ImVec2{ clearBtnWidth, 0.0f } ) )
            {
                assetPath.clear();
                bChanged = true;
            }
            drawTooltip( SW_LOCKEY( "EdCommon", "AssetSlotClear" ) );
        }

        ImGui::PopID();
        // 이 묶음의 "마지막 항목" 은 슬롯이다(BeginMenu 가 메뉴 머리로 되돌리는 것과 같다). 지우기 단추로 두면 경로가 있을 때만 뒤의 표시 · 툴팁이 단추로 옮겨 간다.
        ImGui::GetCurrentContext()->LastItemData = slotItem;
        return bChanged;
    }

    bool EditorWidgets::drawColorEdit( const utf8* pLabel, Color& color )
    {
        ImGui::PushID( pLabel );
        if ( StringUtil::isNullOrEmpty( pLabel ) == false )
            drawPropertyRowLabel( pLabel );
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
        // 경로 문자열을 싣지 않는다 — 끌기는 `{Asset, id}` 한 형식이고 경로는 id 표가 든다(받는 쪽이 이름을 비교하지 않는다).
        const EditorDragPayload payload{ EditorPayloadKind::Asset, EditorInputGUI::registerPayloadName( EditorPayloadKind::Asset, pRelativePath ) };
        (void)EditorInputGUI::beginDragSource( payload, pRelativePath, bAllowNullID ); // 끌기가 시작됐는지는 쓰지 않는다
    }

    bool EditorWidgets::acceptAssetDrop( string& outPath )
    {
        EditorDragPayload payload{};
        return EditorInputGUI::acceptDrop( EditorPayloadKindSet{ EditorPayloadKind::Asset }, payload ) &&
               EditorInputGUI::findPayloadName( EditorPayloadKind::Asset, payload._id, outPath );
    }

    bool EditorWidgets::acceptAssetDrop( string& outPath, const utf8* pExpectedExt )
    {
        return acceptAssetDrop( outPath ) && ( StringUtil::isNullOrEmpty( pExpectedExt ) || EditorAssetFilter::matches( outPath, pExpectedExt ) );
    }

    bool EditorWidgets::previewAssetDropInRect( const float2& minPos, const float2& maxPos, const utf8* pTargetID, string& outPath, bool& outbDelivered )
    {
        EditorDragPayload payload{};
        return EditorInputGUI::previewDropInRect( minPos, maxPos, pTargetID, EditorPayloadKindSet{ EditorPayloadKind::Asset }, payload, outbDelivered ) &&
               EditorInputGUI::findPayloadName( EditorPayloadKind::Asset, payload._id, outPath );
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

        if ( EditorInputGUI::wasEditKeyPressed( EditorEditKey::Down, bRepeat ) )
        {
            ++selectedIndex;
            if ( selectedIndex >= itemCount )
                selectedIndex = itemCount - 1;
        }
        if ( EditorInputGUI::wasEditKeyPressed( EditorEditKey::Up, bRepeat ) )
        {
            --selectedIndex;
            if ( selectedIndex < 0 )
                selectedIndex = 0;
        }

        const bool bValidSelection = ( 0 <= selectedIndex && selectedIndex < itemCount );
        if ( bValidSelection == false )
            return false;
        return EditorInputGUI::wasEditKeyPressed( EditorEditKey::Enter, bRepeat );
    }

    bool EditorWidgets::beginCenteredModal( const utf8* pPopupID )
    {
        // 팝업이 닫혀 있으면 BeginPopupModal 이 이 위치 요청을 버린다.
        ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f } );
        return ImGui::BeginPopupModal( pPopupID, nullptr, ImGuiWindowFlags_AlwaysAutoResize );
    }

    EditorUnsavedChoice EditorWidgets::drawUnsavedChangesModal( const utf8* pPopupID, const utf8* pMessage )
    {
        if ( StringUtil::isNullOrEmpty( pPopupID ) )
            return EditorUnsavedChoice::None;

        if ( beginCenteredModal( pPopupID ) == false )
            return EditorUnsavedChoice::None;

        ImGui::TextUnformatted( pMessage != nullptr ? pMessage : "You have unsaved changes." );
        EditorUnsavedChoice choice = EditorUnsavedChoice::None;
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            choice = EditorUnsavedChoice::Save;
        EditorSelfTestMarks::record( "unsaved.save" );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdText", "DonTSave" ) ) )
            choice = EditorUnsavedChoice::Discard;
        EditorSelfTestMarks::record( "unsaved.discard" );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) ) )
            choice = EditorUnsavedChoice::Cancel;
        EditorSelfTestMarks::record( "unsaved.cancel" );
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
            ImGui::PushTextWrapPos( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "TooltipWrapEm" ) );
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
