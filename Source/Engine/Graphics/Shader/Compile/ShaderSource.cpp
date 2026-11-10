#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/Shader/Compile/ShaderCookStamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    optional<string_view> ShaderSourceID::getShadersFolder() const
    {
        // 칸 단위로 본다 — `myshaders/` 나 `shadersx/` 는 `shaders` 칸이 아니다(예전 글자 검색 `shaders/` 는 앞을, `/shaders/` 는 둘 다 놓쳤다).
        const string_view id     = _id.view();
        size_t            begin  = id.find( '/' ); // 첫 칸은 도메인이다 - `shaders` 가 맨 앞이면 도메인이 없는 id 라 받지 않는다
        const string_view folder = constant::path::kShaderFolder;
        while ( begin != string_view::npos )
        {
            const size_t      componentBegin = begin + 1;
            const size_t      componentEnd   = id.find( '/', componentBegin );
            const string_view component      = id.substr( componentBegin, componentEnd == string_view::npos ? string_view::npos : componentEnd - componentBegin );
            if ( componentEnd == string_view::npos )
                return nullopt; // 마지막 칸은 파일 이름이다
            if ( StringUtil::equals( component, folder, true ) )
                return id.substr( 0, componentEnd );
            begin = componentEnd;
        }
        return nullopt;
    }

    string ShaderSourceID::getStemLower() const
    {
        return StringUtil::toLower( FileUtil::removeExtension( FileUtil::getFileNamePart( _id.view() ) ).c_str() );
    }

    optional<ShaderBinaryLocation> ShaderBinaryLocation::make( const ShaderSourceID& source, ShaderTargetFormat format )
    {
        const optional<string_view> shadersFolder = source.getShadersFolder();
        if ( shadersFolder.has_value() == false )
            return nullopt;
        return makeUnderShadersFolder( *shadersFolder, format );
    }

    ShaderBinaryLocation ShaderBinaryLocation::makeForDomain( string_view domain, ShaderTargetFormat format )
    {
        return makeUnderShadersFolder( FileUtil::joinPath( domain, constant::path::kShaderFolder ), format );
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

    optional<AbsolutePath> ShaderIncludeResolver::findInclude( const ShaderCompileInput& input, string_view includeName, vector<uint8>& outBytes, vector<string>& outListAbsentCandidate ) const
    {
        for ( const AbsolutePath& root : input._listIncludeRoot )
        {
            const string                 candidate = FileUtil::normalizeSeparators( FileUtil::joinPath( root.str(), includeName ) );
            const optional<AbsolutePath> found     = readInclude( candidate, outBytes );
            if ( found.has_value() )
                return found;
            if ( isAbsentCandidate( candidate ) )
                outListAbsentCandidate.push_back( candidate );
        }
        return nullopt;
    }

    bool ShaderIncludeResolver::isAbsentCandidate( string_view candidatePath )
    {
        return FileUtil::isAbsolutePath( candidatePath ) && FileUtil::exists( candidatePath ) == false;
    }

    optional<AbsolutePath> ShaderIncludeResolver::readInclude( string_view candidatePath, vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        const optional<AbsolutePath> candidate = AbsolutePath::tryMake( candidatePath );
        if ( candidate.has_value() == false || FileUtil::isRegularFile( candidate->str() ) == false )
            return nullopt;
        if ( FileUtil::readFile( candidate->str(), outBytes ) == false ) // 실패 로그는 readFile 이 남긴다
            return nullopt;
        return candidate;
    }

    ShaderSourceResolver::ShaderSourceResolver( const AssetPaths& assetPaths, ShaderIncludeResolver includeResolver )
        : _pAssetPaths{ &assetPaths }
        , _includeResolver{ std::move( includeResolver ) }
    {
    }

    optional<ShaderSourceResolver> ShaderSourceResolver::make( const AssetPaths& assetPaths )
    {
        vector<string> listDomain;
        if ( ShaderCookStamp::collectIncludeRootDomains( assetPaths.getSearchPriority(), listDomain ) == false )
            return nullopt; // 오류는 위 함수가 남겼다
        vector<ShaderIncludeRootDomain> listRootDomain;
        for ( string& domain : listDomain )
        {
            const optional<AbsolutePath> folder = assetPaths.toDiskPath( FileUtil::joinPath( domain, constant::path::kShaderFolder ) );
            if ( folder.has_value() == false || FileUtil::isDirectory( folder->str() ) == false )
            {
                SW_LOG_ERROR_ID( "ShaderSource.ShaderIncludeRootNo", "Shader include root '%#/%#' has no folder on disk - the engine headers cannot be found", domain, constant::path::kShaderFolder );
                return nullopt;
            }
            listRootDomain.push_back( ShaderIncludeRootDomain{ std::move( domain ), *folder } );
        }
        return ShaderSourceResolver{ assetPaths, ShaderIncludeResolver{ std::move( listRootDomain ) } };
    }

    expected<ShaderCompileInput> ShaderSourceResolver::resolve( const ShaderSourceID& source ) const
    {
        const optional<string_view> shadersFolderID = source.getShadersFolder();
        if ( shadersFolderID.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Shader source id has no domain 'shaders' folder", source.str() } );
        const optional<AbsolutePath> sourceFile = _pAssetPaths->findLooseFile( source.str() );
        if ( sourceFile.has_value() == false || FileUtil::isRegularFile( sourceFile->str() ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Shader source file not found", source.str() } );
        const optional<AbsolutePath> shadersFolder = _pAssetPaths->toDiskPath( *shadersFolderID );
        const bool                   bInsideFolder = shadersFolder.has_value() &&
                                   FileUtil::isPathAtOrUnderRoot( shadersFolder->str(), sourceFile->str() );
        if ( bInsideFolder == false )
        {
            // 스탬프 키와 가림 검사는 소스가 자기 `shaders` 폴더 아래라는 데 기댄다 - 다른 자리로 풀린 소스(덮어쓴 도메인)를 고르지 않고 멈춘다.
            return unexpected( Error{ ErrorKind::InvalidArgument, "Shader source resolves outside its 'shaders' folder", sourceFile->str() } );
        }

        const optional<AbsolutePath> sourceDir = AbsolutePath::tryMake( FileUtil::getDirectoryPart( sourceFile->str() ) );
        if ( sourceDir.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Shader source folder cannot be resolved", sourceFile->str() } );
        vector<AbsolutePath> listIncludeRoot{ *sourceDir };
        for ( const ShaderIncludeRootDomain& rootDomain : _includeResolver.getRootDomains() )
        {
            listIncludeRoot.push_back( rootDomain._shadersFolder );
        }
        return ShaderCompileInput{ *sourceFile, *shadersFolder, std::move( listIncludeRoot ) };
    }

    optional<AbsolutePath> ShaderSourceResolver::findDomainShadersFolder( string_view domain ) const
    {
        const optional<AbsolutePath> folder = _pAssetPaths->toDiskPath( FileUtil::joinPath( domain, constant::path::kShaderFolder ) );
        return folder.has_value() && FileUtil::isDirectory( folder->str() ) ? folder : nullopt;
    }
} // namespace sw
