#include "pch.h"

#include "Editor/Common/Asset/ModelImportConfig.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImportConfig" );

    ModelImportConfig::ModelImportConfig()
        : _listRule{}
    {
    }

    bool ModelImportConfig::loadFromFile( string_view configPath )
    {
        string text;
        if ( FileUtil::readTextFile( configPath, text ) == false || text.empty() )
        {
            SW_LOG_WARNING( "Failed to read ModelImportConfig file: %#", configPath );
            return false;
        }
        return loadFromJsonString( text );
    }

    bool ModelImportConfig::loadFromJsonString( string_view jsonString )
    {
        _listRule.clear();

        JsonDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR( "Failed to parse ModelImportConfig JSON." );
            return false;
        }
        const JsonValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR( "ModelImportConfig root is not an object." );
            return false;
        }

        // 규칙이 조용히 기본값이 되면 임포트 결과가 말없이 바뀐다 — 망가진 규칙은 설정 전체를 거부한다.
        const JsonValue rulesValue = root.get( "rules" );
        const size_t    ruleCount  = rulesValue.isArray() ? rulesValue.size() : 0;
        for ( size_t ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex )
        {
            const JsonValue ruleValue = rulesValue.at( ruleIndex );
            if ( ruleValue.isObject() == false )
            {
                SW_LOG_ERROR( "ModelImportConfig rule %# is not an object.", ruleIndex );
                _listRule.clear();
                return false;
            }

            ModelImportRule rule;
            if ( ruleValue.has( "name" ) )
                rule._name = ruleValue.get( "name" ).asString();
            rule._filter.parse( ruleValue );
            if ( ruleValue.has( "translation" ) )
            {
                const JsonValue translationValue = ruleValue.get( "translation" );
                const bool      bThreeNumbers    = translationValue.isArray() && translationValue.size() == 3 && translationValue.at( 0 ).isNumber() &&
                                           translationValue.at( 1 ).isNumber() && translationValue.at( 2 ).isNumber();
                if ( bThreeNumbers == false )
                {
                    SW_LOG_ERROR( "ModelImportConfig rule '%#': translation must be an array of three numbers.", rule._name.c_str() );
                    _listRule.clear();
                    return false;
                }
                for ( size_t axis = 0; axis < 3; ++axis )
                {
                    rule._arrTranslation[axis] = static_cast<float32>( translationValue.at( axis ).asFloat() );
                }
            }
            if ( ruleValue.has( "recenter" ) )
            {
                const string recenterText = ruleValue.get( "recenter" ).asString();
                if ( parseRecenter( recenterText, rule._recenter ) == false )
                {
                    SW_LOG_ERROR( "ModelImportConfig rule '%#': unknown recenter '%#' (none | xz | bottom-center).", rule._name.c_str(), recenterText.c_str() );
                    _listRule.clear();
                    return false;
                }
            }
            _listRule.push_back( rule );
        }
        return true;
    }

    ModelImportRule ModelImportConfig::findMatchingRule( string_view relativePath ) const
    {
        for ( const ModelImportRule& rule : _listRule )
        {
            if ( rule._filter.matchesPath( relativePath ) )
                return rule;
        }
        return ModelImportRule{};
    }

    bool ModelImportConfig::parseRecenter( string_view text, ModelRecenter& outRecenter )
    {
        if ( text == "none" )
            outRecenter = ModelRecenter::None;
        else if ( text == "xz" )
            outRecenter = ModelRecenter::Xz;
        else if ( text == "bottom-center" )
            outRecenter = ModelRecenter::BottomCenter;
        else
            return false;
        return true;
    }

    string ModelImportConfig::makeDefaultConfigPath()
    {
        const EditorToolDefaults defaults{};
        const string             projectRoot = EditorUtil::getProjectRootPath();
        if ( projectRoot.empty() )
            return {};

        const string configDir = FileUtil::joinPath( FileUtil::joinPath( projectRoot, defaults._configFolder ), defaults._editorConfigFolder );
        return FileUtil::joinPath( configDir, defaults._modelImportConfigFile );
    }
} // namespace sw::editor
