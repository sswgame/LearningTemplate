#include "pch.h"

#include "DevTools/Cook/PackPatchBuilder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/Pack/PackReader.h"

namespace sw
{
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
            [[nodiscard]] static bool scanPacks( const AbsolutePath& packsDirectory, vector<string>& outListPackPath, vector<Source>& outListSource )
            {
                outListPackPath.clear();
                outListSource.clear();
                if ( packsDirectory.empty() || FileUtil::isDirectory( packsDirectory ) == false )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.PackFolderDoesNot", "Pack folder '%#' does not exist", packsDirectory.str() );
                    return false;
                }
                vector<AbsolutePath> listFile;
                if ( FileUtil::collectFiles( packsDirectory, "", listFile, false ) == false )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.PackFolderCouldNot", "Pack folder '%#' could not be listed completely", packsDirectory.str() );
                    return false;
                }
                VectorUtil::sort( listFile );
                for ( const AbsolutePath& file : listFile )
                {
                    if ( StringUtil::endsWith( file.str(), PackPatchManifest::kManifestExtension, true ) )
                    {
                        SW_LOG_ERROR_ID( "PackPatchBuilder.PackFolderHoldsPatch", "Pack folder '%#' holds the patch %# - give the base packs of the build, not a patched install", packsDirectory.str(), file.str() );
                        return false;
                    }
                    if ( StringUtil::endsWith( file.str(), kPackExtension, true ) )
                        outListPackPath.push_back( file.str() );
                }
                unordered_map<string, size_t> mapFullPathToPack;
                for ( size_t packIndex = 0; packIndex < outListPackPath.size(); ++packIndex )
                {
                    PackReader reader;
                    if ( reader.open( outListPackPath[packIndex] ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackPatchBuilder.PackCouldNotOpened", "Pack '%#' could not be opened", outListPackPath[packIndex] );
                        return false;
                    }
                    // 도메인은 팩이 적은 마운트 도메인이다(예전에는 파일 이름 `game_<팩>` 에서 추측했다 - 런타임 마운트와 같은 사실을 따로 셈했다).
                    const string domain = reader.getMountDomain();
                    if ( domain.empty() )
                    {
                        SW_LOG_ERROR_ID( "PackPatchBuilder.PackNoMountDomain", "Pack '%#' has no mount domain - it is a patch pack, give the base packs of the build", outListPackPath[packIndex] );
                        return false;
                    }
                    vector<string> listPathInPack;
                    reader.collectAllEntryPaths( listPathInPack );
                    for ( const string& pathInPack : listPathInPack )
                    {
                        vector<uint8> bytes;
                        if ( reader.readFile( pathInPack, bytes ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackPatchBuilder.PackCouldNotRead", "Pack '%#' could not read '%#'", outListPackPath[packIndex], pathInPack );
                            return false;
                        }
                        Source                                   source;
                        StringBuilder<constant::buffer::kMax256> fullPath;
                        fullPath.appendFormat( "%#/%#", domain, pathInPack );
                        source._fullPath    = string( fullPath.view() );
                        source._contentHash = PackContentEntry::computeHash( bytes );
                        source._packIndex   = packIndex;
                        source._pathInPack  = pathInPack;
                        if ( mapFullPathToPack.emplace( source._fullPath, packIndex ).second == false )
                        {
                            SW_LOG_ERROR_ID( "PackPatchBuilder.PathMoreOnePack", "Path '%#' is in more than one pack of '%#' - a build lists each path once", source._fullPath, packsDirectory.str() );
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
                    SW_LOG_ERROR_ID( "PackPatchBuilder.MakingPatchNeedsNew", "Making a patch needs the new pack folder, the previous build record and an output folder" );
                    return false;
                }
                if ( PackPatchBuilder::isValidBuildName( params._newBuild ) == false )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.BuildNameNotUsable", "Build name '%#' is not usable - use letters, digits, '.', '_' and '-'", params._newBuild );
                    return false;
                }
                string previousText;
                if ( FileUtil::readTextFile( params._previousRecordPath, previousText ) == false )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.PreviousBuildRecordCould", "Previous build record '%#' could not be read", params._previousRecordPath.str() );
                    return false;
                }
                if ( PackBuildRecord::parse( previousText, params._previousRecordPath.str(), outPrevious ) == false )
                    return false;
                if ( outPrevious._patching == PackPatching::None )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.PreviousBuildRecordSays", "Previous build record '%#' says patching=\"none\" - a build that takes no patches has no content list to patch from", params._previousRecordPath.str() );
                    return false;
                }
                if ( outPrevious._build == params._newBuild )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.NewBuildSamePrevious", "The new build '%#' is the same as the previous build - a patch goes from one build to another", params._newBuild );
                    return false;
                }
                return true;
            }

            /**
             * @brief 바뀐 파일을 새 빌드의 팩에서 하나씩 읽어 전체 경로로 @p packPath 에 스트림으로 씁니다(경로 해시 순). 팩을 열지 못하거나 파일을 읽지 못하거나
             *        쓰지 못하면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool writeChangedFiles( const PackPatchBuildParams& params, const vector<string>& listPackPath, const vector<Source>& listSource, const PackDelta& delta,
                                                         const AbsolutePath& packPath, PackWriteResult& outResult )
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
                        SW_LOG_ERROR_ID( "PackPatchBuilder.PackCouldNotOpened", "Pack '%#' could not be opened", listPackPath[packIndex] );
                        return false;
                    }
                }
                vector<string> listChangedPath = delta._listChangedPath;
                VectorUtil::sort( listChangedPath, []( const string& left, const string& right )
                { return PackStreamWriter::makePathHash( left ) < PackStreamWriter::makePathHash( right ); } );
                // 서명하는 패치는 블록 해시 표를 쓰고 색인 요약을 센다 — 서명이 이 요약에 걸린다(팩을 다시 읽지 않는다).
                PackWriterDesc writerDesc   = params._writerDesc;
                writerDesc._pDigestProvider = params._signer._pProvider;
                PackStreamWriter writer;
                expected<void>   step = writer.open( packPath, writerDesc );
                for ( size_t index = 0; index < listChangedPath.size() && step.has_value(); ++index )
                {
                    const string& changedPath = listChangedPath[index];
                    const auto    sourceIt    = mapFullPathToSource.find( changedPath );
                    if ( sourceIt == mapFullPathToSource.end() )
                    {
                        SW_LOG_ERROR_ID( "PackPatchBuilder.ChangedPathNotPacks", "Changed path '%#' is not in the packs of '%#' (internal mismatch)", changedPath, params._packsDirectory.str() );
                        return false;
                    }
                    const Source& source = listSource[sourceIt->second];
                    vector<uint8> bytes;
                    PackFileEntry sourceEntry;
                    if ( listReader[source._packIndex].getFileEntry( source._pathInPack, sourceEntry ) == false || listReader[source._packIndex].readFile( source._pathInPack, bytes ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackPatchBuilder.PackCouldNotRead", "Pack '%#' could not read '%#'", listPackPath[source._packIndex], source._pathInPack );
                        return false;
                    }
                    step = writer.append( changedPath, sourceEntry._storage, std::move( bytes ) ); // 저장 방식은 새 빌드의 팩이 정한 그대로
                }
                const expected<PackWriteResult> closed = step.has_value() ? writer.close() : expected<PackWriteResult>( unexpected( step.error() ) );
                if ( closed.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "PackPatchBuilder.PatchPackCouldNot", "Patch pack could not be written to '%#': %# '%#'", packPath.str(), closed.error().message(), closed.error().detail() );
                    return false;
                }
                outResult = *closed;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PackPatchSigner::~PackPatchSigner()
    {
        Memory::wipe( _privateKeyPem.data(), _privateKeyPem.size() );
    }

    string PackPatchBuilder::makePatchPackFileName( string_view build )
    {
        StringBuilder<constant::buffer::kMax128> builder;
        builder.appendFormat( "%#%#%#", PackPatchBuilderInternal::kPatchPackPrefix, build, PackPatchBuilderInternal::kPackExtension );
        return string( builder.view() );
    }

    string PackPatchBuilder::makeManifestFileName( string_view build )
    {
        StringBuilder<constant::buffer::kMax128> builder;
        builder.appendFormat( "%#%#%#", PackPatchBuilderInternal::kPatchPackPrefix, build, PackPatchManifest::kManifestExtension );
        return string( builder.view() );
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

    bool PackPatchBuilder::collectBuildEntries( const AbsolutePath& packsDirectory, vector<PackContentEntry>& outListEntry )
    {
        vector<string>                           listPackPath;
        vector<PackPatchBuilderInternal::Source> listSource;
        outListEntry.clear();
        if ( PackPatchBuilderInternal::scanPacks( packsDirectory, listPackPath, listSource ) == false )
            return false;
        outListEntry = PackPatchBuilderInternal::makeEntries( listSource );
        return true;
    }

    bool PackPatchBuilder::writeBuildRecord( const AbsolutePath& packsDirectory, string_view build, const AbsolutePath& recordPath )
    {
        if ( isValidBuildName( build ) == false )
        {
            SW_LOG_ERROR_ID( "PackPatchBuilder.BuildNameNotUsable", "Build name '%#' is not usable - use letters, digits, '.', '_' and '-'", build );
            return false;
        }
        PackBuildRecord record;
        record._build = string( build );
        if ( collectBuildEntries( packsDirectory, record._listEntry ) == false )
            return false;
        if ( FileUtil::writeTextFile( recordPath, PackBuildRecord::makeText( record ) ) == false )
        {
            SW_LOG_ERROR_ID( "PackPatchBuilder.BuildRecordCouldNot", "Build record '%#' could not be written", recordPath.str() );
            return false;
        }
        SW_LOG_INFO_ID( "PackPatchBuilder.PackBuildRecordBuild", "Pack build record: build %# lists %# files -> %#", build, record._listEntry.size(), recordPath.str() );
        return true;
    }

    bool PackPatchBuilder::writeNoPatchingRecord( const AbsolutePath& recordPath )
    {
        PackBuildRecord record;
        record._patching = PackPatching::None;
        if ( FileUtil::writeTextFile( recordPath, PackBuildRecord::makeText( record ) ) == false )
        {
            SW_LOG_ERROR_ID( "PackPatchBuilder.BuildRecordCouldNot", "Build record '%#' could not be written", recordPath.str() );
            return false;
        }
        SW_LOG_INFO_ID( "PackPatchBuilder.PackBuildRecordBuild2", "Pack build record: this build takes no patches (patching=\"none\") -> %#", recordPath.str() );
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
        if ( PackDelta::compute( previous._listEntry, current._listEntry, delta ) == false )
            return false;
        if ( delta._listChangedPath.empty() && delta._listRemovedPath.empty() )
        {
            SW_LOG_ERROR_ID( "PackPatchBuilder.BuildsSameContentNothing", "Builds '%#' and '%#' have the same content - there is nothing to patch", previous._build, params._newBuild );
            return false;
        }

        PackPatchManifest manifest;
        manifest._fromBuild                                   = previous._build;
        manifest._toBuild                                     = params._newBuild;
        manifest._packFileName                                = makePatchPackFileName( params._newBuild );
        manifest._listRemovedPath                             = delta._listRemovedPath;
        const AbsolutePath                       packPath     = params._outputDirectory.join( manifest._packFileName );
        const AbsolutePath                       manifestPath = params._outputDirectory.join( makeManifestFileName( params._newBuild ) );
        StringBuilder<constant::buffer::kMax128> recordFileName;
        recordFileName.appendFormat( "%#%#", params._newBuild, PackBuildRecord::kExtension );
        const AbsolutePath recordPath = params._outputDirectory.join( recordFileName.view() );
        PackWriteResult    writeResult;
        if ( Internal::writeChangedFiles( params, listPackPath, listSource, delta, packPath, writeResult ) == false )
            return false;
        // 서명은 쓰기가 센 색인 요약에 건다 — 색인은 블록 요약을 담아 페이로드까지 묶는다. 팩을 다시 읽지 않는다.
        if ( params._signer._pProvider != nullptr )
        {
            if ( writeResult._indexDigest.has_value() == false )
            {
                SW_LOG_ERROR_ID( "PackPatchBuilder.PatchPackNoIndexDigest", "Patch pack '%#' has no index digest to sign", packPath.str() );
                return false;
            }
            if ( PackPatchManifest::sign( *params._signer._pProvider, params._signer._privateKeyPem, *writeResult._indexDigest, manifest ) == false )
                return false;
        }
        const bool bWritten = FileUtil::writeTextFile( manifestPath, PackPatchManifest::makeText( manifest ) ) &&
                              FileUtil::writeTextFile( recordPath, PackBuildRecord::makeText( current ) );
        if ( bWritten == false )
        {
            SW_LOG_ERROR_ID( "PackPatchBuilder.PatchFilesCouldNot", "Patch files could not be written to '%#'", params._outputDirectory.str() );
            return false;
        }
        SW_LOG_INFO_ID( "PackPatchBuilder.PackPatchChangedRemoved", "Pack patch %# -> %#: %# changed, %# removed -> %#", previous._build, params._newBuild, delta._listChangedPath.size(), delta._listRemovedPath.size(), packPath.str() );
        return true;
    }
} // namespace sw
