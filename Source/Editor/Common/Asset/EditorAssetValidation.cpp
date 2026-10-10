#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorExternalToolJob.h"
#include "Editor/Common/EditorUtil.h"

namespace sw::editor
{
    SW_LOG_CALLER( "AssetValidation" );

    namespace
    {
        struct EditorAssetValidationInternal
        {
            /** @brief 검증 스크립트(저장소 루트 기준)입니다. */
            static constexpr string_view kScriptRelativePath = "Scripts/qa/ValidateAssets.py";

            /** @brief 결과 줄의 심각도 낱말(대문자, 출력 형식 `Finding.format`)을 소문자 이름으로 바꿉니다. */
            [[nodiscard]] static bool parseSeverity( string_view word, string& outSeverity )
            {
                static constexpr string_view kArrSeverity[] = { "error", "warning", "info" };
                for ( const string_view severity : kArrSeverity )
                {
                    if ( StringUtil::equals( word, severity, true ) )
                    {
                        outSeverity = string{ severity };
                        return true;
                    }
                }
                return false;
            }
        };
    } // namespace

    EditorAssetValidation::EditorAssetValidation()
        : _pJob{ make_unique<EditorExternalToolJob>() }
        , _listPendingPath{}
        , _projectRoot{ EditorUtil::getProjectRootPath() }
        , _bDisabled{ SW_FALSE }
    {
    }

    EditorAssetValidation::~EditorAssetValidation() = default;

    void EditorAssetValidation::requestValidation( string_view resourceRelativePath )
    {
        if ( _bDisabled == SW_TRUE || resourceRelativePath.empty() )
            return;
        const string path = FileUtil::normalizeSeparators( resourceRelativePath );
        for ( const string& pending : _listPendingPath )
        {
            if ( pending == path )
                return;
        }
        _listPendingPath.push_back( path );
    }

    void EditorAssetValidation::update()
    {
        EditorExternalToolResult result;
        if ( _pJob->take( result ) )
        {
            AssetValidationFinding finding;
            for ( const string& line : result._listLine )
            {
                if ( parseFindingLine( line, finding ) == false )
                    continue;
                if ( finding._severity == "error" )
                    SW_LOG_ERROR( "%#: [%#] %#", finding._path.c_str(), finding._rule.c_str(), finding._message.c_str() );
                else if ( finding._severity == "warning" )
                    SW_LOG_WARNING( "%#: [%#] %#", finding._path.c_str(), finding._rule.c_str(), finding._message.c_str() );
            }
            if ( result._bLaunched == false )
            {
                SW_LOG_WARNING( "Asset validation could not start Python - validation on save is off for this session" );
                _bDisabled = SW_TRUE;
            }
            else if ( result._exitCode == 2 )
            {
                SW_LOG_ERROR( "Asset validation rules could not be read (Config/Editor/AssetValidationRules.json)" );
            }
        }

        if ( _bDisabled == SW_TRUE || _listPendingPath.empty() || _pJob->isPending() )
            return;
        if ( _projectRoot.empty() ||
             FileUtil::exists( FileUtil::joinPath( _projectRoot, EditorAssetValidationInternal::kScriptRelativePath ) ) == false )
        {
            SW_LOG_INFO( "Asset validation script is not here (%#) - validation on save is off", _projectRoot.c_str() );
            _bDisabled = SW_TRUE;
            _listPendingPath.clear();
            return;
        }
        if ( _pJob->request( makeCommand( _projectRoot, _listPendingPath ), _projectRoot ) )
            _listPendingPath.clear();
    }

    string EditorAssetValidation::makeCommand( string_view projectRoot, const vector<string>& listResourcePath )
    {
        string command{ EditorUtil::kPythonCommand };
        command += " \"";
        command += FileUtil::joinPath( projectRoot, EditorAssetValidationInternal::kScriptRelativePath );
        command += "\" --severity warning --files";
        for ( const string& path : listResourcePath )
        {
            command += " \"";
            command += path;
            command += "\"";
        }
        return command;
    }

    bool EditorAssetValidation::parseFindingLine( string_view line, AssetValidationFinding& outFinding )
    {
        const string_view trimmed = StringUtil::trim( line );
        const size_t      wordEnd = trimmed.find( ' ' );
        const size_t      pathEnd = trimmed.find( ": [" );
        const size_t      ruleEnd = pathEnd == string_view::npos ? string_view::npos : trimmed.find( "] ", pathEnd );
        if ( wordEnd == string_view::npos || pathEnd == string_view::npos || ruleEnd == string_view::npos || pathEnd < wordEnd )
            return false;
        if ( EditorAssetValidationInternal::parseSeverity( trimmed.substr( 0, wordEnd ), outFinding._severity ) == false )
            return false;
        outFinding._path    = string{ StringUtil::trim( trimmed.substr( wordEnd, pathEnd - wordEnd ) ) };
        outFinding._rule    = string{ trimmed.substr( pathEnd + 3, ruleEnd - pathEnd - 3 ) };
        outFinding._message = string{ trimmed.substr( ruleEnd + 2 ) };
        return outFinding._path.empty() == false && outFinding._rule.empty() == false;
    }
} // namespace sw::editor
