#include "pch.h"

#include "Editor/Common/Input/CommandPalettePopup.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/Widgets/EditorVirtualList.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_POPUP( CommandPalettePopup, 200 );

    // ------------------------------------------------------------------------------
    // 생성자
    // ------------------------------------------------------------------------------
    CommandPalettePopup::CommandPalettePopup()
        : IEditorPopup{ false }
        , _listItem{}
        , _assetIndexJob{}
        , _searchBuffer{}
        , _selectedIndex{ 0 }
        , _bJustOpened{ false }
    {
    }

    // ------------------------------------------------------------------------------
    // 정적 함수
    // ------------------------------------------------------------------------------
    void CommandPalettePopup::open()
    {
        EditorPopupManager::getInEditorContext().openPopup( kPopupID );
    }

    void CommandPalettePopup::close()
    {
        EditorPopupManager::getInEditorContext().closePopup( kPopupID );
    }

    void CommandPalettePopup::toggle()
    {
        EditorPopupManager::getInEditorContext().togglePopup( kPopupID );
    }

    bool CommandPalettePopup::isOpen()
    {
        return EditorPopupManager::getInEditorContext().isPopupOpen( kPopupID );
    }

    // ------------------------------------------------------------------------------
    // 인스턴스 함수
    // ------------------------------------------------------------------------------
    void CommandPalettePopup::onOpen()
    {
        _bJustOpened   = true;
        _selectedIndex = 0;
        _searchBuffer.clear();
        // 팔레트의 제공자 순서: 명령 → 창 → 씬 오브젝트 → 에셋(Quick Open 은 오브젝트 · 에셋이 먼저다)
        _listItem.clear();
        EditorSearchItem::appendCommands( _listItem );
        EditorSearchItem::appendPanels( _listItem );
        EditorSearchItem::appendSceneObjects( _listItem );
        _assetIndexJob.request();
    }

    void CommandPalettePopup::drawContent()
    {
        constexpr float32 kDetailColumnCount = 24.0f; ///< 오른쪽 끝의 설명 열이 차지하는 글자 칸 수

        vector<EditorAssetIndexEntry> listAssetEntry;
        if ( _assetIndexJob.take( listAssetEntry ) == EditorTaskPoll::Ready )
            EditorSearchItem::appendAssets( listAssetEntry, _listItem );

        editor::EditorSearchOverlayDesc overlayDesc{};
        overlayDesc._pID          = "##CommandPalette";
        overlayDesc._pOpen        = &_bOpen;
        overlayDesc._size         = EditorTextExtent::ofText( 77.0f, 19.0f );
        overlayDesc._pFocusOnOpen = &_bJustOpened;

        if ( EditorChrome::beginSearchOverlay( overlayDesc ) )
        {
            EditorWidgets::drawSearchField( "##PaletteSearch", _searchBuffer,
                                            SW_LOCKEY( "EdCommon", "CommandPaletteHint" ), -1.0f, false );

            ImGui::Separator();

            vector<const EditorSearchItem*> listFiltered;
            EditorSearchItem::filter( { &_listItem }, _searchBuffer.view(), listFiltered );

            const bool bExecuteSelected = EditorWidgets::updateListSelection( _selectedIndex, static_cast<int32>( listFiltered.size() ) );

            editor::EditorSectionDesc resultsDesc{};
            resultsDesc._pID                 = "##PaletteResults";
            resultsDesc._kind                = editor::EditorSectionKind::Child;
            resultsDesc._flags               = editor::EditorSectionFlags::Border;
            const EditorSearchItem* pExecute = nullptr;
            if ( EditorChrome::beginSection( resultsDesc ) )
            {
                EditorVirtualList virtualList( static_cast<int32>( listFiltered.size() ) ); // 명령 · 에셋 후보 전부 — 보이는 줄만 그린다
                virtualList.includeRow( _selectedIndex );                                   // 키로 고른 줄은 화면 밖이어도 그린다(Enter 가 그 줄을 실행한다)
                while ( pExecute == nullptr && virtualList.step() )
                {
                    for ( size_t itemIndex = static_cast<size_t>( virtualList.getBegin() ); itemIndex < static_cast<size_t>( virtualList.getEnd() ); ++itemIndex )
                    {
                        const EditorSearchItem& item        = *listFiltered[itemIndex];
                        const bool              bIsSelected = ( _selectedIndex == static_cast<int32>( itemIndex ) );

                        ImGui::PushID( static_cast<int32>( itemIndex ) );

                        fixed_string<constant::buffer::kMax256> labelBuf;
                        formatstring( labelBuf.data(), labelBuf.capacity(), "[%#] %#", item._category.c_str(), item._title.c_str() );

                        if ( ImGui::Selectable( labelBuf.c_str(), bIsSelected ) || ( bIsSelected && bExecuteSelected ) )
                        {
                            pExecute = &item;
                            ImGui::PopID();
                            break; // 바깥 `while` 도 `pExecute` 를 보고 멈춘다
                        }

                        if ( item._detail.empty() == false )
                        {
                            ImGui::SameLine();
                            ImGui::SetCursorPosX( EditorChrome::resolveTextExtent( overlayDesc._size )._x - kDetailColumnCount * ImGui::CalcTextSize( "0" ).x );
                            ImGui::TextDisabled( "%s", item._detail.c_str() );
                        }

                        ImGui::PopID();
                    }
                }
                if ( listFiltered.empty() )
                    EditorWidgets::drawEmptyHint( _assetIndexJob.isPending() ? SW_LOCKEY( "EdCommon", "QuickOpenIndexing" ) : SW_LOCKEY( "EdCommon", "QuickOpenNoMatches" ) );
            }
            EditorChrome::endSection();
            if ( pExecute != nullptr )
            {
                const EditorSearchItem item = *pExecute; // 실행이 목록을 다시 만들 수 있다(씬 열기) — 사본으로 부른다
                _bOpen                      = false;
                EditorSearchItem::execute( item );
            }
        }
        EditorChrome::endSearchOverlay();
    }
} // namespace sw::editor
