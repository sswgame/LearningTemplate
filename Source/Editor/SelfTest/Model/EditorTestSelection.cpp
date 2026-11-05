#include "pch.h"

#include "Editor/SelfTest/Model/EditorTestSelection.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/Model/EditorTestOutputParser.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTestSelectionInternal
        {
            /** @brief 시나리오 파일 꼬리입니다. */
            static constexpr string_view kScenarioSuffix = ".scenario.xml";
            /** @brief 시나리오가 있는 폴더 이름입니다(`<영역>/automation/`). */
            static constexpr string_view kAutomationFolder = "/automation/";

            /** @brief 패턴에 이름 하나를 잇습니다. 자체 테스트 패턴과 `--test-filter` 가 모두 쉼표로 가른다. */
            static void appendToPattern( string& inoutPattern, const string& name )
            {
                if ( inoutPattern.empty() == false )
                    inoutPattern += ',';
                inoutPattern += name;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorTestSelection::setCases( vector<string> listCase )
    {
        _listCase = std::move( listCase );
        rebuildVisible();
    }

    void EditorTestSelection::setFilter( string_view filter )
    {
        if ( filter == string_view( _filter ) )
            return;
        _filter = string( filter );
        rebuildVisible();
    }

    void EditorTestSelection::setSelected( string_view name, bool bSelected )
    {
        if ( bSelected )
            _uniqueSelected.insert( string( name ) );
        else
            _uniqueSelected.erase( string( name ) );
        rebuildSelectedPattern();
    }

    string_view EditorTestSelection::findLeafName( string_view name )
    {
        const string_view suite = EditorTestOutputParser::findSuiteName( name );
        return name.size() > suite.size() + 1 ? name.substr( suite.size() + 1 ) : name;
    }

    void EditorTestSelection::rebuildVisible()
    {
        _listGroup.clear();
        _visiblePattern.clear();
        const EditorListFilter filter{ string_view( _filter ) };
        for ( uint32 index = 0; index < static_cast<uint32>( _listCase.size() ); ++index )
        {
            const string& name = _listCase[index];
            if ( filter.matches( name ) == false )
                continue;
            EditorTestSelectionInternal::appendToPattern( _visiblePattern, name );

            // 뒤에서부터 찾는다 — 스위트는 대개 붙어 있어 바로 맞는다.
            const string_view suite  = EditorTestOutputParser::findSuiteName( name );
            size_t            group  = _listGroup.size();
            bool              bFound = false;
            while ( group > 0 && bFound == false )
            {
                --group;
                bFound = string_view( _listGroup[group]._name ) == suite;
            }
            if ( bFound == false )
            {
                group = _listGroup.size();
                _listGroup.push_back( EditorTestGroup{ string( suite ), {} } );
            }
            _listGroup[group]._listCaseIndex.push_back( index );
        }
        rebuildSelectedPattern();
    }

    void EditorTestSelection::rebuildSelectedPattern()
    {
        _selectedPattern.clear();
        for ( const EditorTestGroup& group : _listGroup )
        {
            for ( const uint32 index : group._listCaseIndex )
            {
                if ( isSelected( _listCase[index] ) )
                    EditorTestSelectionInternal::appendToPattern( _selectedPattern, _listCase[index] );
            }
        }
    }
} // namespace sw::editor

namespace sw::editor
{
    void EditorScenarioList::setFromFiles( const vector<string>& listFilePath, string_view root )
    {
        _listPath.clear();
        for ( const string& filePath : listFilePath )
        {
            string relative;
            if ( FileUtil::tryMakePathUnderRoot( root, filePath, relative ) == false )
                continue;
            const bool bScenario = StringUtil::endsWith( relative, EditorTestSelectionInternal::kScenarioSuffix ) &&
                                   string_view( relative ).find( EditorTestSelectionInternal::kAutomationFolder ) != string_view::npos;
            if ( bScenario )
                _listPath.push_back( relative );
        }
        VectorUtil::sort( _listPath );
        if ( VectorUtil::contains( _listPath, _chosen ) == false )
            _chosen.clear();
    }
} // namespace sw::editor
