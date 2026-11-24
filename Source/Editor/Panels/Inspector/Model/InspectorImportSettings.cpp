#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorImportSettings.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Asset/ModelImport/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImport/ModelImportUtil.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw::editor
{
    namespace
    {
        struct InspectorImportSettingsInternal
        {
            /** @brief 규칙을 고르는 칸 — 예외로 쓸 수 없고 표에도 보이지 않는다(임포터가 거절하는 키와 같다). */
            static bool isSelectorKey( string_view key ) { return key == "name" || key == "filter" || key == "inherits"; }

            /** @brief 원본 @p assetID 의 서술 디스크 경로입니다. 리소스 id 가 아니면 빈 글입니다. */
            static string findMetaPath( string_view assetID )
            {
                const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().toDiskPath( assetID );
                return diskPath.has_value() ? AssetMeta::makePath( diskPath->str() ).str() : string{};
            }

            /** @brief 서술 글을 읽습니다. 없거나 읽지 못하면 false 입니다. */
            static bool readMetaText( string_view assetID, string& outText )
            {
                const string metaPath = findMetaPath( assetID );
                return metaPath.empty() == false && FileUtil::isRegularFile( FileUtil::toDiskPath( metaPath ) ) && FileUtil::readTextFile( FileUtil::toDiskPath( metaPath ), outText );
            }

            /** @brief 서술 글을 쓰고 그 원본을 다시 임포트합니다(원본 해시에 해석한 규칙이 들어 예외가 바뀐 원본만 다시 임포트된다). */
            static bool writeMetaText( string_view assetID, string_view text )
            {
                const string metaPath = findMetaPath( assetID );
                if ( metaPath.empty() || FileUtil::writeTextFile( FileUtil::toDiskPath( metaPath ), text ) == false )
                {
                    SW_LOG_ERROR_ID( "InspectorImportSettings.WriteFailed", "Import settings: could not write the .meta of '%#'", string{ assetID }.c_str() );
                    return false;
                }
                AssetHotReload* pHotReload = editor::findSubsystem<AssetHotReload>();
                if ( pHotReload != nullptr )
                    pHotReload->applySavedAsset( assetID );
                return true;
            }
        };
    } // namespace

    InspectorImportSettings::SourceKind InspectorImportSettings::findSourceKind( string_view assetID )
    {
        if ( StringUtil::endsWith( assetID, AssetMeta::kExtension ) )
            return SourceKind::None;
        fixed_string<constant::buffer::kMax64> modelSegment;
        formatstring( modelSegment.data(), modelSegment.capacity(), "/%#/", ModelImportUtil::kRawModelFolder );
        fixed_string<constant::buffer::kMax64> textureSegment;
        formatstring( textureSegment.data(), textureSegment.capacity(), "/%#/", ModelImportUtil::kRawTextureFolder );
        if ( assetID.find( modelSegment.view() ) != string_view::npos )
            return SourceKind::Model;
        if ( assetID.find( textureSegment.view() ) != string_view::npos )
            return SourceKind::Texture;
        return SourceKind::None;
    }

    bool InspectorImportSettings::refresh( string_view assetID )
    {
        const SourceKind kind = findSourceKind( assetID );
        string           metaText;
        if ( kind == SourceKind::None || InspectorImportSettingsInternal::readMetaText( assetID, metaText ) == false )
        {
            _assetID.clear();
            _metaText.clear();
            _listRow.clear();
            _kind = SourceKind::None;
            return false;
        }
        if ( _assetID == assetID && _metaText == metaText )
            return true;
        _assetID  = string{ assetID };
        _metaText = std::move( metaText );
        _kind     = kind;
        AssetMeta meta;
        if ( meta.parseXML( _metaText, _assetID ).has_value() == false )
        {
            _listRow.clear();
            _ruleName.clear();
            _error = "The .meta of this asset cannot be read (see the log).";
            return true;
        }
        rebuild( meta._listImportOverride );
        return true;
    }

    string InspectorImportSettings::findResolveError( const vector<AssetImportOverride>& listOverride ) const
    {
        if ( _kind == SourceKind::Model )
        {
            ModelImportConfig config;
            if ( config.loadFromFile( ModelImportConfig::makeDefaultConfigPath() ) == false )
                return "The model import rules cannot be read (see the log).";
            return config.resolveRule( _assetID, listOverride )._error;
        }
        TextureImportConfig config;
        if ( config.loadFromFile( TextureImporter::makeDefaultImportConfigPath() ) == false )
            return "The texture import rules cannot be read (see the log).";
        return config.resolveRule( _assetID, listOverride )._error;
    }

    void InspectorImportSettings::rebuild( const vector<AssetImportOverride>& listOverride )
    {
        _listRow.clear();
        _ruleName.clear();
        _error.clear();
        if ( _kind == SourceKind::Model )
        {
            ModelImportConfig config;
            if ( config.loadFromFile( ModelImportConfig::makeDefaultConfigPath() ) == false )
            {
                _error = "The model import rules cannot be read (see the log).";
                return;
            }
            const ModelImportRuleMatch match = config.resolveRule( _assetID, listOverride );
            _error                           = match._error;
            _ruleName                        = match._bMatched ? match._rule._name : string{};
            appendRows( *ModelImportRule::StaticType(), &match._rule, listOverride );
            return;
        }
        TextureImportConfig config;
        if ( config.loadFromFile( TextureImporter::makeDefaultImportConfigPath() ) == false )
        {
            _error = "The texture import rules cannot be read (see the log).";
            return;
        }
        const TextureImportRuleMatch match = config.resolveRule( _assetID, listOverride );
        _error                             = match._error;
        _ruleName                          = match._bMatched ? match._rule._name : string{};
        appendRows( *TextureImportRule::StaticType(), &match._rule, listOverride );
    }

    void InspectorImportSettings::appendRows( const TypeInfo& ruleType, const void* pRule, const vector<AssetImportOverride>& listOverride )
    {
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        for ( const PropertyInfo& prop : ruleType.getPropertiesWithBase() )
        {
            InspectorImportRow row;
            row._key = XMLFields::getAttributeName( prop );
            if ( InspectorImportSettingsInternal::isSelectorKey( row._key ) )
                continue;
            row._valueText = SerializerUtil::formatPropertyText( prop, ObjectAddress{ pRule }, context );
            _listRow.push_back( std::move( row ) );
        }
        // 예외 칸은 서술의 글 그대로 보인다(고칠 때 같은 꼴로 친다). 규칙 구조체에 칸이 없는 예외(손으로 읽는 `lods`)는 줄을 더한다.
        for ( const AssetImportOverride& entry : listOverride )
        {
            InspectorImportRow* pRow = nullptr;
            for ( InspectorImportRow& row : _listRow )
            {
                if ( row._key == entry._key )
                    pRow = &row;
            }
            if ( pRow == nullptr )
            {
                _listRow.push_back( InspectorImportRow{ entry._key, string{}, 0 } );
                pRow = &_listRow.back();
            }
            pRow->_valueText = entry._value;
            pRow->_bOverride = SW_TRUE;
        }
    }

    bool InspectorImportSettings::writeOverrides( const vector<AssetImportOverride>& listOverride, string_view undoLabel )
    {
        AssetMeta meta;
        if ( _kind == SourceKind::None || meta.parseXML( _metaText, _assetID ).has_value() == false )
            return false;
        // 임포터와 같은 해석으로 먼저 본다 — 모르는 키 · 틀린 값은 서술에 쓰지 않는다(쓰면 그 원본의 임포트가 진다).
        const string resolveError = findResolveError( listOverride );
        if ( resolveError.empty() == false )
        {
            _error = resolveError;
            return false;
        }
        meta._listImportOverride = listOverride;
        const string beforeText  = _metaText;
        const string afterText   = meta.toXML();
        if ( afterText == beforeText )
            return true;
        if ( InspectorImportSettingsInternal::writeMetaText( _assetID, afterText ) == false )
            return false;
        // 씬이 아니라 에셋 서술의 편집이다 — 작업 공간 스택에 두되 씬 dirty 는 표시하지 않는다(문서 스택 길의 `push`).
        CommandStack* pStack = editor::findSubsystem<CommandStack>();
        if ( pStack != nullptr )
        {
            EditorTransaction transaction;
            transaction.push( SW_UNDO_STEP( InspectorImportSettings::restoreMetaText, _assetID, beforeText ),
                              SW_UNDO_STEP( InspectorImportSettings::restoreMetaText, _assetID, afterText ), undoLabel, *pStack );
        }
        _metaText.clear(); // 다음 `refresh` 가 쓴 서술로 다시 짓는다
        return true;
    }

    bool InspectorImportSettings::setOverride( string_view key, string_view valueText )
    {
        AssetMeta meta;
        if ( _kind == SourceKind::None || meta.parseXML( _metaText, _assetID ).has_value() == false || InspectorImportSettingsInternal::isSelectorKey( key ) )
            return false;
        vector<AssetImportOverride> listOverride = meta._listImportOverride;
        bool                        bFound       = false;
        for ( AssetImportOverride& entry : listOverride )
        {
            if ( string_view{ entry._key } == key )
            {
                entry._value = string{ valueText };
                bFound       = true;
            }
        }
        if ( bFound == false )
        {
            AssetImportOverride entry;
            entry._key   = string{ key };
            entry._value = string{ valueText };
            listOverride.push_back( std::move( entry ) );
        }
        return writeOverrides( listOverride, "Edit Import Setting" );
    }

    bool InspectorImportSettings::clearOverride( string_view key )
    {
        AssetMeta meta;
        if ( _kind == SourceKind::None || meta.parseXML( _metaText, _assetID ).has_value() == false )
            return false;
        vector<AssetImportOverride> listOverride = meta._listImportOverride;
        const size_t                countBefore  = listOverride.size();
        (void)VectorUtil::removeIf( listOverride, [key]( const AssetImportOverride& entry )
        { return string_view{ entry._key } == key; } ); // 지운 수는 아래 크기 비교가 본다
        if ( listOverride.size() == countBefore )
            return true;
        return writeOverrides( listOverride, "Revert Import Setting" );
    }

    void InspectorImportSettings::restoreMetaText( string assetID, string metaText )
    {
        (void)InspectorImportSettingsInternal::writeMetaText( assetID, metaText ); // 지면 오류 로그가 남는다 — 되돌리기 하나가 빠졌다
    }
} // namespace sw::editor
