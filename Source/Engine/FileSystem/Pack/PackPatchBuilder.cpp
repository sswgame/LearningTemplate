#include "pch.h"

#include "Engine/FileSystem/Pack/PackPatchBuilder.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/FileSystem/Pack/PackReader.h"

namespace sw
{
    SW_LOG_CALLER( "PackPatchBuilder" );

    namespace
    {
        struct PackPatchBuilderInternal
        {
            static constexpr string_view kPatchPackPrefix = "patch_";
            static constexpr string_view kPackExtension   = ".pack";

            /** @brief 팩 하나와 그 안의 파일 하나가 전체 가상 경로로 무엇인지입니다. */
            struct Source
            {
                string _fullPath{};
                uint64 _contentHash{ 0 };
                size_t _packIndex{ 0 };
                string _pathInPack{};
            };

            /** @brief 폴더의 기본 팩을 모두 열어 파일마다 전체 경로 · 내용 해시 · 있는 팩을 모읍니다. 패치 목록이 있는 폴더는 거절합니다. */
            [[nodiscard]] static bool scanPacks( string_view packsDirectory, vector<string>& outListPackPath, vector<Source>& outListSource )
            {
                outListPackPath.clear();
                outListSource.clear();
                if ( packsDirectory.empty() || FileUtil::isDirectory( packsDirectory ) == false )
                {
                    SW_LOG_ERROR( "Pack folder '%#' does not exist", packsDirectory );
                    return false;
                }
                vector<string> listFile;
                if ( FileUtil::collectFiles( packsDirectory, "", listFile, false ) == false )
                {
                    SW_LOG_ERROR( "Pack folder '%#' could not be listed completely", packsDirectory );
                    return false;
                }
                VectorUtil::sort( listFile );
                for ( const string& file : listFile )
                {
                    if ( StringUtil::endsWith( file, PackPatchUtil::kManifestExtension, true ) )
                    {
                        SW_LOG_ERROR( "Pack folder '%#' holds the patch %# - give the base packs of the build, not a patched install", packsDirectory, file );
                        return false;
                    }
                    if ( StringUtil::endsWith( file, kPackExtension, true ) )
                        outListPackPath.push_back( file );
                }
                unordered_map<string, size_t> mapFullPathToPack;
                for ( size_t packIndex = 0; packIndex < outListPackPath.size(); ++packIndex )
                {
                    PackReader reader;
                    if ( reader.open( outListPackPath[packIndex] ) == false )
                    {
                        SW_LOG_ERROR( "Pack '%#' could not be opened", outListPackPath[packIndex] );
                        return false;
                    }
                    // 도메인은 팩이 적은 마운트 도메인이다(예전에는 파일 이름 `game_<팩>` 에서 추측했다 - 런타임 마운트와 같은 사실을 따로 셈했다).
                    const string domain = reader.getMountDomain();
                    if ( domain.empty() )
                    {
                        SW_LOG_ERROR( "Pack '%#' has no mount domain - it is a patch pack, give the base packs of the build", outListPackPath[packIndex] );
                        return false;
                    }
                    vector<string> listPathInPack;
                    reader.collectAllEntryPaths( listPathInPack );
                    for ( const string& pathInPack : listPathInPack )
                    {
                        vector<uint8> bytes;
                        if ( reader.readFile( pathInPack, bytes ) == false )
                        {
                            SW_LOG_ERROR( "Pack '%#' could not read '%#'", outListPackPath[packIndex], pathInPack );
                            return false;
                        }
                        Source source;
                        source._fullPath    = domain + "/" + pathInPack;
                        source._contentHash = PackPatchUtil::computeEntryHash( bytes );
                        source._packIndex   = packIndex;
                        source._pathInPack  = pathInPack;
                        if ( mapFullPathToPack.emplace( source._fullPath, packIndex ).second == false )
                        {
                            SW_LOG_ERROR( "Path '%#' is in more than one pack of '%#' - a build lists each path once", source._fullPath, packsDirectory );
                            return false;
                        }
                        outListSource.push_back( std::move( source ) );
                    }
                }
                VectorUtil::sort( outListSource, []( const Source& left, const Source& right )
                { return left._fullPath < right._fullPath; } );
                return true;
            }

            static vector<PackContentEntry> makeEntries( const vector<Source>& listSource )
            {
                vector<PackContentEntry> listEntry;
                listEntry.reserve( listSource.size() );
                for ( const Source& source : listSource )
                {
                    listEntry.push_back( PackContentEntry{ source._fullPath, source._contentHash } );
                }
                return listEntry;
            }

            /**
             * @brief 패치를 만들 입력(폴더 · 빌드 이름)을 검사하고, 이전 빌드 기록을 읽습니다.
             * @return 입력이 모자라거나, 기록을 읽지 못하거나, 패치 없는 빌드이거나, 새 빌드가 이전 빌드와 같으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool readPreviousRecord( const PackPatchBuildParams& params, PackBuildRecord& outPrevious )
            {
                const bool bMissingParam = params._packsDirectory.empty() || params._previousRecordPath.empty() || params._outputDirectory.empty();
                if ( bMissingParam )
                {
                    SW_LOG_ERROR( "Making a patch needs the new pack folder, the previous build record and an output folder" );
                    return false;
                }
                if ( PackPatchBuilder::isValidBuildName( params._newBuild ) == false )
                {
                    SW_LOG_ERROR( "Build name '%#' is not usable - use letters, digits, '.', '_' and '-'", params._newBuild );
                    return false;
                }
                string previousText;
                if ( FileUtil::readTextFile( params._previousRecordPath, previousText ) == false )
                {
                    SW_LOG_ERROR( "Previous build record '%#' could not be read", params._previousRecordPath );
                    return false;
                }
                if ( PackPatchUtil::parseBuildRecord( previousText, params._previousRecordPath, outPrevious ) == false )
                    return false;
                if ( outPrevious._patching == PackPatching::None )
                {
                    SW_LOG_ERROR( "Previous build record '%#' says patching=\"none\" - a build that takes no patches has no content list to patch from", params._previousRecordPath );
                    return false;
                }
                if ( outPrevious._build == params._newBuild )
                {
                    SW_LOG_ERROR( "The new build '%#' is the same as the previous build - a patch goes from one build to another", params._newBuild );
                    return false;
                }
                return true;
            }

            /** @brief 바뀐 파일을 새 빌드의 팩에서 읽어 전체 경로로 @p inoutWriter 에 담습니다. 팩을 열지 못하거나 파일을 읽지 못하면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool addChangedFiles( const PackPatchBuildParams& params, const vector<string>& listPackPath, const vector<Source>& listSource, const PackDelta& delta,
                                                       PackWriter& inoutWriter )
            {
                unordered_map<string, size_t> mapFullPathToSource;
                for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
                {
                    mapFullPathToSource.emplace( listSource[sourceIndex]._fullPath, sourceIndex );
                }
                vector<PackReader> listReader( listPackPath.size() );
                for ( size_t packIndex = 0; packIndex < listPackPath.size(); ++packIndex )
                {
                    if ( listReader[packIndex].open( listPackPath[packIndex] ) == false )
                    {
                        SW_LOG_ERROR( "Pack '%#' could not be opened", listPackPath[packIndex] );
                        return false;
                    }
                }
                for ( const string& changedPath : delta._listChangedPath )
                {
                    const auto sourceIt = mapFullPathToSource.find( changedPath );
                    if ( sourceIt == mapFullPathToSource.end() )
                    {
                        SW_LOG_ERROR( "Changed path '%#' is not in the packs of '%#' (internal mismatch)", changedPath, params._packsDirectory );
                        return false;
                    }
                    const Source& source = listSource[sourceIt->second];
                    vector<uint8> bytes;
                    if ( listReader[source._packIndex].readFile( source._pathInPack, bytes ) == false )
                    {
                        SW_LOG_ERROR( "Pack '%#' could not read '%#'", listPackPath[source._packIndex], source._pathInPack );
                        return false;
                    }
                    inoutWriter.addBytes( changedPath, std::move( bytes ) );
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PackPatchBuilder::makePatchPackFileName( string_view build )
    {
        return string( PackPatchBuilderInternal::kPatchPackPrefix ) + string( build ) + string( PackPatchBuilderInternal::kPackExtension );
    }

    string PackPatchBuilder::makeManifestFileName( string_view build )
    {
        return string( PackPatchBuilderInternal::kPatchPackPrefix ) + string( build ) + string( PackPatchUtil::kManifestExtension );
    }

    bool PackPatchBuilder::isValidBuildName( string_view build )
    {
        if ( build.empty() )
            return false;
        for ( const utf8 character : build )
        {
            const bool bLetterOrDigit  = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' );
            const bool bOtherCharacter = bLetterOrDigit == false && character != '.' && character != '_' && character != '-';
            if ( bOtherCharacter )
                return false;
        }
        return build != "." && build != "..";
    }

    bool PackPatchBuilder::collectBuildEntries( string_view packsDirectory, vector<PackContentEntry>& outListEntry )
    {
        vector<string>                           listPackPath;
        vector<PackPatchBuilderInternal::Source> listSource;
        outListEntry.clear();
        if ( PackPatchBuilderInternal::scanPacks( packsDirectory, listPackPath, listSource ) == false )
            return false;
        outListEntry = PackPatchBuilderInternal::makeEntries( listSource );
        return true;
    }

    bool PackPatchBuilder::writeBuildRecord( string_view packsDirectory, string_view build, string_view recordPath )
    {
        if ( isValidBuildName( build ) == false )
        {
            SW_LOG_ERROR( "Build name '%#' is not usable - use letters, digits, '.', '_' and '-'", build );
            return false;
        }
        PackBuildRecord record;
        record._build = string( build );
        if ( collectBuildEntries( packsDirectory, record._listEntry ) == false )
            return false;
        if ( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( recordPath ) ) == false || FileUtil::writeTextFile( recordPath, PackPatchUtil::makeBuildRecordText( record ) ) == false )
        {
            SW_LOG_ERROR( "Build record '%#' could not be written", recordPath );
            return false;
        }
        SW_LOG_INFO( "Pack build record: build %# lists %# files -> %#", build, record._listEntry.size(), recordPath );
        return true;
    }

    bool PackPatchBuilder::writeNoPatchingRecord( string_view recordPath )
    {
        PackBuildRecord record;
        record._patching = PackPatching::None;
        if ( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( recordPath ) ) == false || FileUtil::writeTextFile( recordPath, PackPatchUtil::makeBuildRecordText( record ) ) == false )
        {
            SW_LOG_ERROR( "Build record '%#' could not be written", recordPath );
            return false;
        }
        SW_LOG_INFO( "Pack build record: this build takes no patches (patching=\"none\") -> %#", recordPath );
        return true;
    }

    bool PackPatchBuilder::makePatch( const PackPatchBuildParams& params )
    {
        using Internal = PackPatchBuilderInternal;
        PackBuildRecord previous;
        if ( Internal::readPreviousRecord( params, previous ) == false )
            return false;

        vector<string>           listPackPath;
        vector<Internal::Source> listSource;
        if ( Internal::scanPacks( params._packsDirectory, listPackPath, listSource ) == false )
            return false;
        PackBuildRecord current;
        current._build     = params._newBuild;
        current._listEntry = Internal::makeEntries( listSource );
        PackDelta delta;
        if ( PackPatchUtil::computeDelta( previous._listEntry, current._listEntry, delta ) == false )
            return false;
        if ( delta._listChangedPath.empty() && delta._listRemovedPath.empty() )
        {
            SW_LOG_ERROR( "Builds '%#' and '%#' have the same content - there is nothing to patch", previous._build, params._newBuild );
            return false;
        }

        PackWriter writer( params._writerDesc );
        if ( Internal::addChangedFiles( params, listPackPath, listSource, delta, writer ) == false )
            return false;

        PackPatchManifest manifest;
        manifest._fromBuild       = previous._build;
        manifest._toBuild         = params._newBuild;
        manifest._packFileName    = makePatchPackFileName( params._newBuild );
        manifest._listRemovedPath = delta._listRemovedPath;
        const string packPath     = FileUtil::joinPath( params._outputDirectory, manifest._packFileName );
        const string manifestPath = FileUtil::joinPath( params._outputDirectory, makeManifestFileName( params._newBuild ) );
        const string recordPath   = FileUtil::joinPath( params._outputDirectory, params._newBuild + string( PackPatchUtil::kBuildRecordExtension ) );
        if ( FileUtil::ensureDirectoryExists( params._outputDirectory ) == false || writer.writePackFile( packPath ) == false )
        {
            SW_LOG_ERROR( "Patch pack could not be written to '%#'", packPath );
            return false;
        }
        // 서명은 디스크에 쓴 팩의 바이트에 건다 — 배포되는 바이트의 해시다(메모리의 사본과 갈릴 길이 없다).
        if ( params._signer._pProvider != nullptr )
        {
            vector<uint8> packBytes;
            if ( FileUtil::readFile( packPath, packBytes ) == false )
            {
                SW_LOG_ERROR( "Patch pack '%#' could not be read back for signing", packPath );
                return false;
            }
            if ( PackPatchUtil::signManifest( *params._signer._pProvider, params._signer._privateKeyPem, packBytes, manifest ) == false )
                return false;
        }
        const bool bWritten = FileUtil::writeTextFile( manifestPath, PackPatchUtil::makeManifestText( manifest ) ) &&
                              FileUtil::writeTextFile( recordPath, PackPatchUtil::makeBuildRecordText( current ) );
        if ( bWritten == false )
        {
            SW_LOG_ERROR( "Patch files could not be written to '%#'", params._outputDirectory );
            return false;
        }
        SW_LOG_INFO( "Pack patch %# -> %#: %# changed, %# removed -> %#", previous._build, params._newBuild, delta._listChangedPath.size(), delta._listRemovedPath.size(), packPath );
        return true;
    }
} // namespace sw
