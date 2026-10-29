#include "pch.h"

#include "Editor/Panels/Settings/Model/GlobalVariablesModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"

namespace sw::editor
{
    namespace
    {
        struct GlobalVariablesModelInternal
        {
            /// @brief 프리셋 파일의 접미사입니다.
            static constexpr string_view kPresetSuffix = ".gvpreset.xml";

            static bool compareVariableInfo( const GlobalVariableInfo* pLeft, const GlobalVariableInfo* pRight )
            {
                if ( pLeft->_moduleName != pRight->_moduleName )
                    return pLeft->_moduleName < pRight->_moduleName;
                return pLeft->_name < pRight->_name;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "GlobalVariablesPanel" );

    GlobalVariablesModel::GlobalVariablesModel()
        : _uniquePinnedVar{}
        , _pendingPinToggle{}
        , _presetJob{}
        , _listPresetFile{}
        , _listFilteredVariable{}
        , _listFilterText{}
        , _listGeneration{ 0 }
        , _totalVariableCount{ 0 }
        , _listRebuildCount{ 0 }
        , _bPresetListDirty{ true }
    {
    }

    const vector<GlobalVariableInfo*>& GlobalVariablesModel::refreshVariableList( const GlobalVariableManager& gvm, string_view filterText )
    {
        if ( _pendingPinToggle.empty() == false )
        {
            if ( isPinned( _pendingPinToggle ) )
                _uniquePinnedVar.erase( _pendingPinToggle );
            else
                _uniquePinnedVar.insert( _pendingPinToggle );
            _pendingPinToggle.clear();
        }

        const bool bListStale = _listGeneration != gvm.getRegistrationGeneration() || string_view{ _listFilterText } != filterText;
        if ( bListStale == false )
            return _listFilteredVariable;

        // 세대를 먼저 읽는다 — 채우는 사이에 등록이 바뀌면 다음 프레임에 다시 짓는다.
        _listGeneration = gvm.getRegistrationGeneration();
        _listFilterText = filterText;
        ++_listRebuildCount;

        const EditorListFilter       filter{ _listFilterText.c_str() };
        vector<GlobalVariableInfo*>& listAll = _listFilteredVariable;
        gvm.collectVariables( listAll );
        _totalVariableCount = 0;
        // 테스트용(`SW_TEST_GLOBAL_VARIABLE`)은 벤치와 자동화 스위치이므로 여기 두지 않는다. 실행 인자로만 준다. 개수에서도 뺀다.
        (void)VectorUtil::removeIf( listAll, [this, &filter]( const GlobalVariableInfo* pInfo ) // 지운 수는 쓰지 않는다 — 남은 목록이 결과다
        {
            if ( pInfo->_bTestOnly )
                return true;
            ++_totalVariableCount;
            return filter.matchesAny( { pInfo->_name, pInfo->_description, pInfo->_moduleName } ) == false;
        } );

        // 포인터를 담은 컨테이너를 정렬하지만 비교는 포인터 값이 아니라 (모듈, 이름) 으로 한다.
        // 전역 변수 이름은 GlobalVariableManager 의 키이므로 유일하고, 순서가 매번 같다.
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
        VectorUtil::sort( listAll, GlobalVariablesModelInternal::compareVariableInfo );
        return _listFilteredVariable;
    }

    bool GlobalVariablesModel::savePreset( string_view presetName )
    {
        if ( presetName.empty() )
            return false;
        const string name{ presetName };
        const string presetPath = FileUtil::joinPath( EditorGlobalVariableCommands::getPresetFolderPath(), name + string{ GlobalVariablesModelInternal::kPresetSuffix } );
        // 이름 붙인 프리셋 저장은 세션 문서의 저장이 아니다. 세션의 dirty 는 건드리지 않는다.
        if ( EditorGlobalVariableCommands::savePreset( presetPath, name.c_str() ) == false )
        {
            SW_LOG_ERROR( "Could not save preset '%#'", presetPath.c_str() );
            return false;
        }
        _bPresetListDirty = true;
        return true;
    }

    void GlobalVariablesModel::loadPreset( const string& presetFile )
    {
        // 읽은 프리셋이 현재 값이 된다. 저장된 세션과 다르므로 dirty 다. 읽지 못하면 값이 그대로다.
        if ( EditorGlobalVariableCommands::loadPreset( presetFile ) == false )
            SW_LOG_ERROR( "Could not load preset '%#'", presetFile.c_str() );
    }

    bool GlobalVariablesModel::pollPresetFiles()
    {
        vector<string>       listNewPresetFile;
        const EditorTaskPoll presetPoll = _presetJob.take( listNewPresetFile );
        if ( presetPoll == EditorTaskPoll::Ready )
        {
            _listPresetFile   = std::move( listNewPresetFile );
            _bPresetListDirty = false;
        }

        bool bPresetScanning = presetPoll == EditorTaskPoll::Running;
        if ( _bPresetListDirty && presetPoll != EditorTaskPoll::Running )
        {
            _presetJob.request( EditorGlobalVariableCommands::getPresetFolderPath(), GlobalVariablesModelInternal::kPresetSuffix, false );
            bPresetScanning = true;
        }
        return bPresetScanning;
    }

    string GlobalVariablesModel::makePresetDisplayName( string_view presetFile )
    {
        string displayName = FileUtil::getFileNamePart( presetFile );
        if ( StringUtil::endsWith( displayName, GlobalVariablesModelInternal::kPresetSuffix, true ) )
            displayName = displayName.substr( 0, displayName.size() - GlobalVariablesModelInternal::kPresetSuffix.size() );
        return displayName;
    }
} // namespace sw::editor
