#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelText.h"

#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Localization/EditorDataText.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    namespace
    {
        constexpr const utf8* kPanelTextAsset = "editor/panels/editor.panels.xml";

        /** @brief 읽은 표와 지금 언어로 푼 이름입니다. 에디터 범위의 상태라 에디터 컨텍스트와 함께 처음으로 돌아간다. */
        struct PanelTextState
        {
            vector<EditorPanelText> _listRow{};           ///< `_label` 은 지금 언어로 푼 글이다
            vector<string>          _listReported{};      ///< 줄이 없다고 이미 알린 제목(프레임마다 같은 오류를 남기지 않는다)
            uint32                  _revision{ 0 };       ///< 푼 때의 현지화 글 판
            uint8                   _bLoaded{ SW_FALSE }; ///< 표를 읽었다(실패해도 다시 읽지 않는다 — 오류는 한 번)
        };

        const EditorPanelText* findRow( const utf8* pTitle )
        {
            PanelTextState& state    = getEditorState<PanelTextState>();
            const uint32    revision = EditorDataText::getRevision();
            if ( state._bLoaded == SW_FALSE || state._revision != revision )
            {
                EditorPanelTextTable table;
                state._listRow.clear();
                if ( XMLSerializer::loadFromAsset( AssetID{ kPanelTextAsset }, &table, *EditorPanelTextTable::StaticType() ) == false )
                    SW_LOG_ERROR_ID( "EditorPanel.TextCannotRead", "Editor panel names '%#' could not be read strictly - tabs show their titles", kPanelTextAsset );
                for ( EditorPanelText& row : table._listPanel )
                {
                    row._label = EditorDataText::localize( row._label );
                    state._listRow.push_back( std::move( row ) );
                }
                state._revision = revision;
                state._bLoaded  = SW_TRUE;
            }
            const EditorPanelText* pRow = VectorUtil::findBy<&EditorPanelText::_title>( state._listRow, string_view( pTitle ) );
            if ( pRow == nullptr && VectorUtil::contains( state._listReported, string( pTitle ) ) == false )
            {
                state._listReported.push_back( string( pTitle ) );
                SW_LOG_ERROR_ID( "EditorPanel.NoName", "Panel '%#' has no row in %# - its tab shows the title", pTitle, kPanelTextAsset );
            }
            return pRow;
        }
    } // namespace

    EditorWindowName EditorPanelText::makeWindowName( const utf8* pTitle )
    {
        const EditorPanelText* pRow = findRow( pTitle );
        EditorWindowName       name;
        name.append( pRow != nullptr ? pRow->_label.c_str() : pTitle );
        name.append( "###" );
        name.append( pTitle );
        return name;
    }

    EditorWindowName EditorPanelText::makeWindowID( const utf8* pTitle )
    {
        EditorWindowName name;
        name.append( "###" );
        name.append( pTitle );
        return name;
    }
} // namespace sw::editor
