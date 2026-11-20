#include "pch.h"

#include "DevTools/Material/MaterialGraphGeneratedPaths.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Material/MaterialGraph.h"
#include "DevTools/Material/MaterialGraphCompileResult.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetGUID.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h"

namespace sw
{
    namespace
    {
        struct MaterialGraphCookInternal
        {
            static constexpr string_view kMaterialsSegment = "/materials/";
            static constexpr string_view kShaderSuffix     = ".hlsl";
            static constexpr uint32      kHashDigitCount   = 8;

            /** @brief `\r` 를 뺀 글입니다. 체크아웃이 줄끝을 바꿔도 생성물을 낡았다고 보지 않는다. */
            [[nodiscard]] static string normalizeLineEndings( string_view text )
            {
                string normalized;
                normalized.reserve( text.size() );
                for ( const utf8 c : text )
                {
                    if ( c != '\r' )
                        normalized.push_back( c );
                }
                return normalized;
            }

            /** @brief 디스크의 글이 @p text 와 다르면 씁니다. */
            [[nodiscard]] static bool writeIfChanged( const AssetPaths& assetPaths, string_view assetID, string_view text, uint32* pOutWrittenCount )
            {
                const optional<AbsolutePath> diskPath = assetPaths.toDiskPath( assetID );
                if ( diskPath.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "MaterialGraph.GeneratedPathUnresolved", "Generated material graph file '%#' has no disk path", assetID );
                    return false;
                }
                string     current;
                const bool bHasCurrent = FileUtil::exists( diskPath->str() ) && FileUtil::readTextFile( diskPath->str(), current );
                // 산출물 머티리얼도 정체성(머리 GUID)을 든다 — 다시 지을 때 있던 머리를 옮겨 쓴다(지으면 GUID 가 바뀌는 일이 없게).
                const string desired = bHasCurrent ? AssetGUIDHeader::carryOver( normalizeLineEndings( current ), text ) : string( text );
                if ( bHasCurrent && normalizeLineEndings( current ) == desired )
                    return true;
                if ( FileUtil::ensureParentDirectoryExists( diskPath->str() ) == false || FileUtil::writeTextFile( diskPath->str(), desired ) == false )
                {
                    SW_LOG_ERROR_ID( "MaterialGraph.GeneratedWriteFailed", "Could not write generated material graph file '%#'", assetID );
                    return false;
                }
                if ( pOutWrittenCount != nullptr )
                    ++*pOutWrittenCount;
                return true;
            }

            /** @brief 파일 이름이 같은 줄기의 해시 셰이더(`<줄기>_<16 진 8 자>.hlsl`)이면 true 입니다 — 줄기가 `a` 일 때 `a_b.hlsl` 을 지우지 않게 모양까지 본다. */
            [[nodiscard]] static bool isHashedShaderOfStem( string_view fileName, string_view stem )
            {
                const size_t expected = stem.size() + 1 + kHashDigitCount + kShaderSuffix.size();
                if ( fileName.size() != expected || StringUtil::startsWith( fileName, stem ) == false || fileName[stem.size()] != '_' || StringUtil::endsWith( fileName, kShaderSuffix ) == false )
                    return false;
                for ( const utf8 c : fileName.substr( stem.size() + 1, kHashDigitCount ) )
                {
                    const bool bHexDigit = ( '0' <= c && c <= '9' ) || ( 'a' <= c && c <= 'f' );
                    if ( bHexDigit == false )
                        return false;
                }
                return true;
            }

            /**
             * @brief 옛 셰이더 곁의 서술(`.meta`)을 지금 셰이더로 옮깁니다 — 생성 셰이더의 정체성(GUID)은 해시 이름이 바뀌어도 그 그래프의 셰이더 하나로 남는다
             *        (생성 머티리얼이 머리를 옮겨 쓰는 것과 같은 규칙). 지금 셰이더에 서술이 이미 있으면 옛 것만 지운다.
             */
            static void carryStaleMeta( const string& staleShaderPath, const string& currentShaderPath )
            {
                const string staleMetaPath = AssetMeta::makePath( staleShaderPath );
                if ( FileUtil::exists( staleMetaPath ) == false )
                    return;
                const string currentMetaPath = AssetMeta::makePath( currentShaderPath );
                if ( FileUtil::exists( currentMetaPath ) == false )
                {
                    string                    metaText;
                    AssetMeta                 meta;
                    const optional<AssetGUID> guid = FileUtil::readTextFile( staleMetaPath, metaText ) ? meta.parseXML( metaText, staleMetaPath ) : nullopt;
                    if ( guid.has_value() == false || FileUtil::writeTextFile( currentMetaPath, AssetMeta::makeText( currentShaderPath, *guid, meta._listImportOverride ) ) == false )
                    {
                        SW_LOG_WARNING_ID( "MaterialGraph.ShaderMetaNotCarried", "Could not carry '%#' to the new generated shader '%#' - it keeps a new identity", staleMetaPath, currentShaderPath );
                        return; // 옛 서술을 남겨 손으로 옮길 수 있게 한다
                    }
                }
                if ( FileUtil::removeFile( staleMetaPath ) == false )
                    SW_LOG_WARNING_ID( "MaterialGraph.StaleShaderNotRemoved", "Could not remove old generated shader '%#'", staleMetaPath );
            }

            /** @brief 같은 줄기의 옛 해시 셰이더를 지웁니다. 남겨 두면 그 내용을 아무도 안 쓰는데 쿠킹 입력과 저장소에 쌓인다. 곁의 서술은 지금 셰이더로 옮긴다(`carryStaleMeta`). */
            static void removeStaleShaders( const AssetPaths& assetPaths, const MaterialGraphGeneratedPaths& paths, string_view currentShaderPath )
            {
                const optional<AbsolutePath> folder = assetPaths.toDiskPath( paths._shadersFolder );
                if ( folder.has_value() == false || FileUtil::isDirectory( folder->str() ) == false )
                    return;
                vector<string> listFile;
                if ( FileUtil::collectFiles( folder->str(), kShaderSuffix, listFile, false ) == false )
                    return;
                const string currentName = FileUtil::getFileNamePart( currentShaderPath );
                for ( const string& filePath : listFile )
                {
                    const string fileName = FileUtil::getFileNamePart( filePath );
                    if ( fileName == currentName || isHashedShaderOfStem( fileName, paths._stem ) == false )
                        continue;
                    carryStaleMeta( filePath, folder->str() + "/" + currentName );
                    if ( FileUtil::removeFile( filePath ) == false )
                        SW_LOG_WARNING_ID( "MaterialGraph.StaleShaderNotRemoved", "Could not remove old generated shader '%#'", filePath );
                }
            }
        };

        /** @brief 머티리얼 그래프 컴파일러를 쿠킹 전 셰이더 소스 생성기로 겁니다 — 셰이더 쿠커(`ShaderCookDriver`)는 이 창구만 부르고 컴파일러를 모른다. */
        class MaterialGraphSourceGenerator final : public IShaderSourceGenerator
        {
        public:
            const utf8* getName() const override { return "MaterialGraph"; }
            bool        refreshAll( const AssetPaths& assetPaths, string_view contentRoot, uint32& outWrittenCount ) const override
            {
                return MaterialGraphGeneratedPaths::refreshAll( assetPaths, contentRoot, &outWrittenCount );
            }
        };

        const MaterialGraphSourceGenerator         s_materialGraphSourceGenerator;
        const IShaderSourceGenerator::Registration s_materialGraphSourceGeneratorRegistration{ s_materialGraphSourceGenerator };
    } // namespace

    bool MaterialGraphGeneratedPaths::makeGeneratedPaths( string_view graphID, MaterialGraphGeneratedPaths& outPaths )
    {
        using Internal          = MaterialGraphCookInternal;
        const size_t segment    = graphID.find( Internal::kMaterialsSegment );
        const bool   bGraphFile = StringUtil::endsWith( graphID, MaterialGraphDef::kExtension, true );
        if ( segment == string_view::npos || bGraphFile == false )
        {
            SW_LOG_ERROR_ID( "MaterialGraph.PathNotInMaterials", "Material graph '%#' must be a %# file under a materials folder - the generated shader and material are placed from that path", graphID, MaterialGraphDef::kExtension );
            return false;
        }
        const string_view domain   = graphID.substr( 0, segment );
        const size_t      start    = segment + Internal::kMaterialsSegment.size();
        const string_view relative = graphID.substr( start, graphID.size() - start - MaterialGraphDef::kExtension.size() );
        outPaths._shadersFolder    = string{ domain };
        outPaths._shadersFolder.append( "/shaders/matgraph" );
        outPaths._stem = string{ relative };
        for ( utf8& c : outPaths._stem )
        {
            if ( c == '/' )
                c = '_';
        }
        outPaths._materialPath = string{ graphID.substr( 0, graphID.size() - MaterialGraphDef::kExtension.size() ) };
        outPaths._materialPath.append( kGeneratedMaterialSuffix );
        return true;
    }

    MaterialGraphGeneratedPaths MaterialGraphGeneratedPaths::makePreviewPaths( const MaterialGraphGeneratedPaths& paths )
    {
        MaterialGraphGeneratedPaths preview;
        preview._shadersFolder = string{ AssetPaths::kScratchDomain };
        preview._shadersFolder.append( "/shaders/matgraph" );
        preview._stem         = paths._stem;
        preview._materialPath = string{ AssetPaths::kScratchDomain };
        preview._materialPath.append( "/materials/matgraph/" ).append( paths._stem ).append( ".preview.material" );
        return preview;
    }

    string MaterialGraphGeneratedPaths::makeShaderPath( const MaterialGraphGeneratedPaths& paths, const MaterialGraphCompileResult& result )
    {
        StringBuilder<constant::buffer::kMax16> hash;
        hash.appendFormat( "%#", Fmt( result._contentHash, Format( MaterialGraphCookInternal::kHashDigitCount, Format::Padding::Zero ).hex() ) );
        string path = paths._shadersFolder;
        path.append( "/" ).append( paths._stem ).append( "_" ).append( hash.view() ).append( MaterialGraphCookInternal::kShaderSuffix );
        return path;
    }

    bool MaterialGraphGeneratedPaths::writeGenerated( const AssetPaths& assetPaths, string_view graphID, const MaterialGraphGeneratedPaths& paths, const MaterialGraphCompileResult& result, uint32* pOutWrittenCount )
    {
        using Internal          = MaterialGraphCookInternal;
        const string shaderPath = makeShaderPath( paths, result );
        // 셰이더를 먼저 쓴다 — 머티리얼이 가리키는 파일이 늘 있어야 감시가 먼저 머티리얼을 다시 읽어도 컴파일이 된다.
        if ( Internal::writeIfChanged( assetPaths, shaderPath, result._hlsl, pOutWrittenCount ) == false )
            return false;
        const string materialText = MaterialGraphCompileResult::makeMaterialText( result, paths._stem, shaderPath, graphID );
        if ( Internal::writeIfChanged( assetPaths, paths._materialPath, materialText, pOutWrittenCount ) == false )
            return false;
        Internal::removeStaleShaders( assetPaths, paths, shaderPath );
        return true;
    }

    bool MaterialGraphGeneratedPaths::refreshGraph( const AssetPaths& assetPaths, string_view graphID, uint32* pOutWrittenCount )
    {
        MaterialGraphDef graph;
        if ( graph.loadFromAsset( AssetID{ graphID } ) == false )
            return false;
        if ( MaterialGraphCompileResult::isSubgraph( graph ) )
            return true; // 쓰는 그래프를 컴파일할 때 함께 검사된다
        MaterialGraphGeneratedPaths paths;
        if ( makeGeneratedPaths( graphID, paths ) == false )
            return false;
        MaterialGraphCompileResult result;
        if ( MaterialGraphCompileResult::compile( graph, graphID, result ) == false )
        {
            for ( const MaterialGraphIssue& issue : result._listIssue )
            {
                SW_LOG_ERROR_ID( "MaterialGraph.CompileFailed", "%#: node '%#': [%#] %#", graphID, issue._nodeName, issue._id, issue._message );
            }
            return false;
        }
        return writeGenerated( assetPaths, graphID, paths, result, pOutWrittenCount );
    }

    bool MaterialGraphGeneratedPaths::refreshAll( const AssetPaths& assetPaths, string_view contentRoot, uint32* pOutWrittenCount )
    {
        vector<string> listFile;
        if ( FileUtil::isDirectory( contentRoot ) && AssetPaths::collectContentFiles( contentRoot, MaterialGraphDef::kExtension, listFile ) == false )
            return false;
        bool bAllRefreshed = true;
        for ( const string& filePath : listFile )
        {
            string relativePath;
            if ( FileUtil::makeRelativePath( contentRoot, filePath, relativePath ) == false )
            {
                SW_LOG_ERROR_ID( "MaterialGraph.PathOutsideContent", "Material graph '%#' is not under the content root '%#'", filePath, contentRoot );
                bAllRefreshed = false;
                continue;
            }
            bAllRefreshed = refreshGraph( assetPaths, FileUtil::normalizeSeparators( relativePath ), pOutWrittenCount ) && bAllRefreshed;
        }
        return bAllRefreshed;
    }
} // namespace sw
