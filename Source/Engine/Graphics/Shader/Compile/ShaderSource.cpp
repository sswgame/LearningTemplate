#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderSource" );

    std::optional<string_view> ShaderSourceID::getShadersFolder() const
    {
        // 칸 단위로 본다 — `myshaders/` 나 `shadersx/` 는 `shaders` 칸이 아니다(예전 글자 검색 `shaders/` 는 앞을, `/shaders/` 는 둘 다 놓쳤다).
        const string_view id     = _id.view();
        size_t            begin  = id.find( '/' ); // 첫 칸은 도메인이다 - `shaders` 가 맨 앞이면 도메인이 없는 id 라 받지 않는다
        const string_view folder = path::kShaderFolder;
        while ( begin != string_view::npos )
        {
            const size_t      componentBegin = begin + 1;
            const size_t      componentEnd   = id.find( '/', componentBegin );
            const string_view component      = id.substr( componentBegin, componentEnd == string_view::npos ? string_view::npos : componentEnd - componentBegin );
            if ( componentEnd == string_view::npos )
                return std::nullopt; // 마지막 칸은 파일 이름이다
            if ( StringUtil::equals( component, folder, true ) )
                return id.substr( 0, componentEnd );
            begin = componentEnd;
        }
        return std::nullopt;
    }

    string ShaderSourceID::getStemLower() const
    {
        return StringUtil::toLower( FileUtil::removeExtension( FileUtil::getFileNamePart( _id.view() ) ).c_str() );
    }

    std::optional<ShaderBinaryLocation> ShaderBinaryLocation::make( const ShaderSourceID& source, ShaderTargetFormat format )
    {
        const std::optional<string_view> shadersFolder = source.getShadersFolder();
        if ( shadersFolder.has_value() == false )
            return std::nullopt;
        return makeUnderShadersFolder( *shadersFolder, format );
    }

    ShaderBinaryLocation ShaderBinaryLocation::makeForDomain( string_view domain, ShaderTargetFormat format )
    {
        return makeUnderShadersFolder( FileUtil::joinPath( domain, path::kShaderFolder ), format );
    }

    ShaderBinaryLocation ShaderBinaryLocation::makeUnderShadersFolder( string_view shadersFolder, ShaderTargetFormat format )
    {
        ShaderBinaryLocation location;
        location._binDirectory = string( shadersFolder );
        location._binDirectory += "/bin/";
        location._binDirectory += ShaderCooker::getSubfolderForFormat( format );
        return location;
    }

    AssetID ShaderBinaryLocation::makeFileID( string_view fileName ) const
    {
        string id = _binDirectory;
        id += '/';
        id += fileName;
        return AssetID{ id };
    }

    ShaderIncludeResolver::ShaderIncludeResolver( vector<ShaderIncludeRootDomain> listRootDomain )
        : _listRootDomain{ std::move( listRootDomain ) }
    {
    }

    std::optional<AbsolutePath> ShaderIncludeResolver::findInclude( const ShaderCompileInput& input, string_view includeName, vector<uint8>& outBytes ) const
    {
        for ( const AbsolutePath& root : input._listIncludeRoot )
        {
            const std::optional<AbsolutePath> found = readInclude( FileUtil::joinPath( root.str(), includeName ), outBytes );
            if ( found.has_value() )
                return found;
        }
        return std::nullopt;
    }

    std::optional<AbsolutePath> ShaderIncludeResolver::readInclude( string_view candidatePath, vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        const std::optional<AbsolutePath> candidate = AbsolutePath::tryMake( candidatePath );
        if ( candidate.has_value() == false || FileUtil::isRegularFile( candidate->str() ) == false )
            return std::nullopt;
        if ( FileUtil::readFile( candidate->str(), outBytes ) == false ) // 실패 로그는 readFile 이 남긴다
            return std::nullopt;
        return candidate;
    }

    ShaderSourceResolver::ShaderSourceResolver( const AssetPaths& assetPaths, ShaderIncludeResolver includeResolver )
        : _pAssetPaths{ &assetPaths }
        , _includeResolver{ std::move( includeResolver ) }
    {
    }

    std::optional<ShaderSourceResolver> ShaderSourceResolver::make( const AssetPaths& assetPaths )
    {
        vector<string> listDomain;
        if ( ShaderCooker::collectIncludeRootDomains( assetPaths.getSearchPriority(), listDomain ) == false )
            return std::nullopt; // 오류는 위 함수가 남겼다
        vector<ShaderIncludeRootDomain> listRootDomain;
        for ( string& domain : listDomain )
        {
            const std::optional<AbsolutePath> folder = assetPaths.toDiskPath( FileUtil::joinPath( domain, path::kShaderFolder ) );
            if ( folder.has_value() == false || FileUtil::isDirectory( folder->str() ) == false )
            {
                SW_LOG_ERROR( "Shader include root '%#/%#' has no folder on disk - the engine headers cannot be found", domain, path::kShaderFolder );
                return std::nullopt;
            }
            listRootDomain.push_back( ShaderIncludeRootDomain{ std::move( domain ), *folder } );
        }
        return ShaderSourceResolver{ assetPaths, ShaderIncludeResolver{ std::move( listRootDomain ) } };
    }

    std::optional<ShaderCompileInput> ShaderSourceResolver::resolve( const ShaderSourceID& source, string& outError ) const
    {
        const std::optional<string_view> shadersFolderID = source.getShadersFolder();
        if ( shadersFolderID.has_value() == false )
        {
            outError = "Shader source id has no domain 'shaders' folder: " + source.str();
            return std::nullopt;
        }
        const std::optional<AbsolutePath> sourceFile = _pAssetPaths->findLooseFile( source.str() );
        if ( sourceFile.has_value() == false || FileUtil::isRegularFile( sourceFile->str() ) == false )
        {
            outError = "Shader source file not found: " + source.str();
            return std::nullopt;
        }
        const std::optional<AbsolutePath> shadersFolder = _pAssetPaths->toDiskPath( *shadersFolderID );
        const bool                        bInsideFolder = shadersFolder.has_value() &&
                                   FileUtil::startsWithPathComponent( StringUtil::toLower( sourceFile->c_str() ), StringUtil::toLower( shadersFolder->c_str() ) );
        if ( bInsideFolder == false )
        {
            // 스탬프 키와 가림 검사는 소스가 자기 `shaders` 폴더 아래라는 데 기댄다 - 다른 자리로 풀린 소스(덮어쓴 도메인)를 고르지 않고 멈춘다.
            outError = "Shader source '" + source.str() + "' resolves to '" + sourceFile->str() + "', outside its 'shaders' folder";
            return std::nullopt;
        }

        const std::optional<AbsolutePath> sourceDir = AbsolutePath::tryMake( FileUtil::getDirectoryPart( sourceFile->str() ) );
        if ( sourceDir.has_value() == false )
        {
            outError = "Shader source folder cannot be resolved: " + sourceFile->str();
            return std::nullopt;
        }
        vector<AbsolutePath> listIncludeRoot{ *sourceDir };
        for ( const ShaderIncludeRootDomain& rootDomain : _includeResolver.getRootDomains() )
        {
            listIncludeRoot.push_back( rootDomain._shadersFolder );
        }
        return ShaderCompileInput{ *sourceFile, *shadersFolder, std::move( listIncludeRoot ) };
    }

    std::optional<AbsolutePath> ShaderSourceResolver::findDomainShadersFolder( string_view domain ) const
    {
        const std::optional<AbsolutePath> folder = _pAssetPaths->toDiskPath( FileUtil::joinPath( domain, path::kShaderFolder ) );
        return folder.has_value() && FileUtil::isDirectory( folder->str() ) ? folder : std::nullopt;
    }
} // namespace sw
