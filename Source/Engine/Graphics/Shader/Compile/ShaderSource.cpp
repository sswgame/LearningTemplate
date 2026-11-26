#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    namespace
    {
        struct ShaderSourceInternal
        {
            /** @brief 문제 한 줄을 서식으로 만들어 더합니다. */
            template <typename... Args>
            static void appendProblem( vector<string>& outListProblem, const utf8* pFormat, const Args&... args )
            {
                StringBuilder<constant::buffer::kMax512> sb;
                sb.appendFormat( pFormat, args... );
                outListProblem.emplace_back( sb.c_str(), sb.size() );
            }

            /** @brief 소스 절대 경로를 @p shadersDir 기준 소문자 상대 경로로 바꿉니다. 그 아래가 아니면 빈 글입니다. */
            static string makeRelativeKey( string_view absSourcePath, const string& shadersDir )
            {
                const string norm = FileUtil::normalizeSeparators( absSourcePath );
                if ( norm.size() <= shadersDir.size() + 1 )
                    return string{};
                return StringUtil::toLower( norm.substr( shadersDir.size() + 1 ).c_str() );
            }

            /** @brief 도메인 발견의 기준인 우선순위 설정이 쓸 수 있으면 true 입니다. 비었거나 도메인 종류(engine · common)가 빠졌으면 오류를 남깁니다. */
            [[nodiscard]] static bool validatePriority( const vector<string>& listPriority )
            {
                if ( listPriority.empty() )
                {
                    SW_LOG_ERROR_ID( "ShaderSource.ShaderDomainsCannotFound", "Shader domains cannot be found: _listResourcePriority (EngineConfig.json) is empty" );
                    return false;
                }
                for ( const utf8* pKind : { constant::path::kEnginePack, constant::path::kCommonPack } )
                {
                    if ( VectorUtil::contains( listPriority, string( pKind ) ) == false )
                    {
                        SW_LOG_ERROR_ID( "ShaderSource.ShaderDomainsCannotFound2", "Shader domains cannot be found: _listResourcePriority (EngineConfig.json) has no '%#' (the shader include root kind)", pKind );
                        return false;
                    }
                }
                return true;
            }

            /** @brief @p shadersDir(`<도메인>/shaders`)의 `.hlsli` 가 include 루트 도메인의 `.hlsli` 와 같은 상대 경로이면 그 쌍을 적습니다. 문제가 없으면 true 입니다. */
            [[nodiscard]] static bool findShadowedInDir( const vector<ShaderIncludeRootDomain>& listRootDomain, const DiskPath& shadersDir, vector<string>& outListProblem )
            {
                vector<DiskPath> listOwn;
                if ( FileUtil::isDirectory( shadersDir ) && FileUtil::collectFiles( shadersDir, ".hlsli", listOwn, true ) == false )
                {
                    appendProblem( outListProblem, "%#: the headers could not all be listed - shadowing is not known", shadersDir.str() );
                    return false;
                }
                unordered_set<string> setOwnKey;
                for ( const DiskPath& ownPath : listOwn )
                {
                    const string normOwn = FileUtil::normalizeSeparators( ownPath.str() );
                    if ( normOwn.find( "/bin/" ) == string::npos )
                        setOwnKey.insert( makeRelativeKey( normOwn, shadersDir.str() ) );
                }

                const string ownDirLower = StringUtil::toLower( shadersDir.c_str() );
                bool         bClean      = true;
                for ( const ShaderIncludeRootDomain& rootDomain : listRootDomain )
                {
                    const DiskPath rootDir = rootDomain._shadersFolder;
                    if ( StringUtil::toLower( rootDir.c_str() ) == ownDirLower )
                        continue;
                    vector<DiskPath> listRootHeader;
                    if ( FileUtil::collectFiles( rootDir, ".hlsli", listRootHeader, true ) == false )
                    {
                        appendProblem( outListProblem, "%#: the headers could not all be listed - shadowing is not known", rootDir.str() );
                        bClean = false;
                        continue;
                    }
                    for ( const DiskPath& headerPath : listRootHeader )
                    {
                        const string normHeader = FileUtil::normalizeSeparators( headerPath.str() );
                        const string key        = normHeader.find( "/bin/" ) == string::npos ? makeRelativeKey( normHeader, rootDir.str() ) : string{};
                        if ( key.empty() == false && setOwnKey.find( key ) != setOwnKey.end() )
                        {
                            appendProblem( outListProblem, "%#: header '%#' has the same name as '%#/shaders/%#' and would hide it (rename it)", shadersDir.str(), key, rootDomain._domain, key );
                            bClean = false;
                        }
                    }
                }
                return bClean;
            }
        };
    } // namespace
} // namespace sw

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

    ShaderIncludeCache::ShaderIncludeCache()
        : _mutex{}
        , _mapFile{}
    {
    }

    shared_ptr<const ShaderIncludeCache::File> ShaderIncludeCache::read( const DiskPath& candidate )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapFile.find( candidate.str() );
            if ( iter != _mapFile.end() )
                return iter->second;
        }
        // 디스크는 잠금 밖에서 읽는다 — 두 워커가 함께 읽어도 먼저 넣은 것을 둘 다 쓴다.
        auto pFile = sw::make_shared<File>();
        if ( FileUtil::isRegularFile( candidate ) == false )
        {
            pFile->_input = CookJob::makeDiskInput( candidate, 0 );
        }
        else if ( FileUtil::readFile( candidate, pFile->_bytes ) ) // 실패 로그는 readFile 이 남긴다
        {
            pFile->_input     = CookJob::makeDiskInput( candidate, ContentHash::computeBytes( pFile->_bytes ) );
            pFile->_bReadable = pFile->_input.has_value() ? SW_TRUE : SW_FALSE;
        }
        ScopedLock<mutex> lock{ _mutex };
        return _mapFile.emplace( candidate.str(), std::move( pFile ) ).first->second;
    }

    ShaderIncludeResolver::ShaderIncludeResolver( vector<ShaderIncludeRootDomain> listRootDomain, shared_ptr<ShaderIncludeCache> pCache )
        : _listRootDomain{ std::move( listRootDomain ) }
        , _pCache{ std::move( pCache ) }
    {
    }

    optional<DiskPath> ShaderIncludeResolver::findInclude( const ShaderCompileInput& input, string_view includeName, vector<uint8>& outBytes ) const
    {
        for ( const DiskPath& root : input._listIncludeRoot )
        {
            const optional<AbsolutePath> candidate = AbsolutePath::isStrictAbsolute( includeName ) ? nullopt : AbsolutePath::tryResolve( includeName, root );
            if ( candidate.has_value() == false )
                continue;
            if ( const optional<DiskPath> found = readInclude( *candidate, outBytes ); found.has_value() )
                return found;
        }
        return nullopt;
    }

    optional<DiskPath> ShaderIncludeResolver::readInclude( const DiskPath& candidate, vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        CookJob* const pJob = CookJob::findCurrent();
        if ( _pCache != nullptr && pJob != nullptr )
        {
            const shared_ptr<const ShaderIncludeCache::File> pFile = _pCache->read( candidate );
            if ( pFile->_input.has_value() )
                pJob->recordInput( *pFile->_input );
            if ( pFile->_bReadable == SW_FALSE )
                return nullopt;
            outBytes = pFile->_bytes;
            return candidate;
        }
        if ( FileUtil::isRegularFile( candidate ) == false )
        {
            CookJob::allowMissingDiskRead( candidate ); // 작업이 없으면 적지 않는다
            return nullopt;
        }
        // 실패 로그는 readFile 이, 입력으로 적지 못한 까닭은 작업이 남긴다.
        if ( FileUtil::readFile( candidate, outBytes ) == false || CookJob::allowDiskRead( candidate, outBytes.data(), outBytes.size() ) == false )
            return nullopt;
        return candidate;
    }

    bool ShaderIncludeResolver::findShadowedHeaders( const DiskPath& shadersFolder, vector<string>& outListProblem ) const
    {
        return ShaderSourceInternal::findShadowedInDir( _listRootDomain, shadersFolder, outListProblem );
    }

    ShaderSourceResolver::ShaderSourceResolver( const AssetPaths& assetPaths, ShaderIncludeResolver includeResolver )
        : _pAssetPaths{ &assetPaths }
        , _includeResolver{ std::move( includeResolver ) }
    {
    }

    bool ShaderSourceResolver::collectIncludeRootDomains( const vector<string>& listPriority, vector<string>& outListDomain )
    {
        outListDomain.clear();
        if ( ShaderSourceInternal::validatePriority( listPriority ) == false )
            return false;
        outListDomain.emplace_back( constant::path::kEnginePack );
        outListDomain.emplace_back( constant::path::kCommonPack );
        return true;
    }

    optional<ShaderSourceResolver> ShaderSourceResolver::make( const AssetPaths& assetPaths, shared_ptr<ShaderIncludeCache> pCache )
    {
        vector<string> listDomain;
        if ( collectIncludeRootDomains( assetPaths.getSearchPriority(), listDomain ) == false )
            return nullopt; // 오류는 위 함수가 남겼다
        vector<ShaderIncludeRootDomain> listRootDomain;
        for ( string& domain : listDomain )
        {
            const optional<DiskPath> folder = assetPaths.toDiskPath( FileUtil::joinPath( domain, constant::path::kShaderFolder ) );
            if ( folder.has_value() == false || FileUtil::isDirectory( *folder ) == false )
            {
                SW_LOG_ERROR_ID( "ShaderSource.ShaderIncludeRootNo", "Shader include root '%#/%#' has no folder on disk - the engine headers cannot be found", domain, constant::path::kShaderFolder );
                return nullopt;
            }
            listRootDomain.push_back( ShaderIncludeRootDomain{ std::move( domain ), *folder } );
        }
        return ShaderSourceResolver{
            assetPaths, ShaderIncludeResolver{ std::move( listRootDomain ), std::move( pCache ) }
        };
    }

    expected<ShaderCompileInput> ShaderSourceResolver::resolve( const ShaderSourceID& source ) const
    {
        const optional<string_view> shadersFolderID = source.getShadersFolder();
        if ( shadersFolderID.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Shader source id has no domain 'shaders' folder", source.str() } );
        const optional<DiskPath> sourceFile = _pAssetPaths->findLooseFile( source.str() );
        if ( sourceFile.has_value() == false || FileUtil::isRegularFile( *sourceFile ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Shader source file not found", source.str() } );
        const optional<DiskPath> shadersFolder = _pAssetPaths->toDiskPath( *shadersFolderID );
        const bool               bInsideFolder = shadersFolder.has_value() &&
                                   FileUtil::isPathAtOrUnderRoot( shadersFolder->str(), sourceFile->str() );
        if ( bInsideFolder == false )
        {
            // 스탬프 키와 가림 검사는 소스가 자기 `shaders` 폴더 아래라는 데 기댄다 - 다른 자리로 풀린 소스(덮어쓴 도메인)를 고르지 않고 멈춘다.
            return unexpected( Error{ ErrorKind::InvalidArgument, "Shader source resolves outside its 'shaders' folder", sourceFile->str() } );
        }

        const optional<AbsolutePath> sourceDir = AbsolutePath::tryMake( FileUtil::getDirectoryPart( sourceFile->str() ) );
        if ( sourceDir.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Shader source folder cannot be resolved", sourceFile->str() } );
        vector<DiskPath> listIncludeRoot{ *sourceDir };
        for ( const ShaderIncludeRootDomain& rootDomain : _includeResolver.getRootDomains() )
        {
            listIncludeRoot.push_back( rootDomain._shadersFolder );
        }
        return ShaderCompileInput{ *sourceFile, *shadersFolder, std::move( listIncludeRoot ) };
    }

    optional<DiskPath> ShaderSourceResolver::findDomainShadersFolder( string_view domain ) const
    {
        const optional<DiskPath> folder = _pAssetPaths->toDiskPath( FileUtil::joinPath( domain, constant::path::kShaderFolder ) );
        return folder.has_value() && FileUtil::isDirectory( *folder ) ? folder : nullopt;
    }
} // namespace sw
