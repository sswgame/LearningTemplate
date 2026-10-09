#include "pch.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct AssetImportStampInternal
         * @brief `import.stamp` 읽기 · 쓰기 · 대조와 원본 폴더 하나의 일괄 임포트입니다.
         */
        struct AssetImportStampInternal
        {
            /** @brief 원본 폴더마다 하나 두는 스탬프 파일 이름입니다. */
            static constexpr string_view kImportStampFileName = "import.stamp";

            /** @brief 스탬프 한 줄입니다. */
            struct StampEntry
            {
                uint64 _sourceHash{ 0 };
                uint64 _importedHash{ 0 };
            };

            /** @brief 경로를 `.../<rawFolderName>` 과 그 아래 상대 경로로 나눕니다. 폴더 이름은 경로 구간 경계에서만 찾습니다. */
            static bool splitRawPath( string_view path, string_view rawFolderName, string& outRawFolder, string& outRelativePath )
            {
                const string      normalized = FileUtil::normalizeSeparators( path );
                const string_view normalizedView{ normalized };
                for ( size_t pos = normalizedView.find( rawFolderName ); pos != string_view::npos; pos = normalizedView.find( rawFolderName, pos + 1 ) )
                {
                    const size_t endPos         = pos + rawFolderName.size();
                    const bool   bStartsSegment = pos == 0 || normalized[pos - 1] == '/';
                    const bool   bEndsSegment   = endPos + 1 < normalized.size() && normalized[endPos] == '/';
                    if ( bStartsSegment && bEndsSegment )
                    {
                        outRawFolder    = normalized.substr( 0, endPos );
                        outRelativePath = normalized.substr( endPos + 1 );
                        return true;
                    }
                }
                return false;
            }

            static string formatHash( uint64 hash )
            {
                StringBuilder<constant::kMaxBuffer32> text;
                text.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                return string( text.c_str(), text.size() );
            }

            /** @brief 스탬프를 읽습니다. 없거나 머리 줄이 다르면 빈 표입니다(= 모든 원본이 어긋남). 형식이 아닌 줄은 건너뜁니다. */
            static void readStamp( const string& stampPath, string_view header, map<string, StampEntry>& outMapEntry )
            {
                outMapEntry.clear();
                string text;
                if ( FileUtil::exists( stampPath ) == false || FileUtil::readTextFile( stampPath, text ) == false )
                    return;

                bool   bHeaderSeen = false;
                size_t lineStart   = 0;
                while ( lineStart < text.size() )
                {
                    size_t lineEnd = text.find( '\n', lineStart );
                    if ( lineEnd == string::npos )
                        lineEnd = text.size();
                    const string_view line = StringUtil::trim( string_view( text ).substr( lineStart, lineEnd - lineStart ) );
                    lineStart              = lineEnd + 1;

                    if ( bHeaderSeen == false )
                    {
                        if ( line != header )
                            return;
                        bHeaderSeen = true;
                        continue;
                    }

                    const size_t firstSpace  = line.find( ' ' );
                    const size_t secondSpace = firstSpace == string_view::npos ? string_view::npos : line.find( ' ', firstSpace + 1 );
                    if ( secondSpace == string_view::npos || secondSpace + 1 >= line.size() )
                        continue;

                    StampEntry entry;
                    const bool bSourceParsed   = StringUtil::parseUint64( line.substr( 0, firstSpace ), entry._sourceHash, 16 );
                    const bool bImportedParsed = StringUtil::parseUint64( line.substr( firstSpace + 1, secondSpace - firstSpace - 1 ), entry._importedHash, 16 );
                    if ( bSourceParsed && bImportedParsed )
                        outMapEntry[string( line.substr( secondSpace + 1 ) )] = entry;
                }
            }

            /** @brief 스탬프 본문입니다. 줄은 경로 순이라 같은 표는 같은 바이트입니다. */
            static string makeStampText( string_view header, const map<string, StampEntry>& mapEntry )
            {
                string text = string( header ) + "\n";
                for ( const auto& [relativePath, entry] : mapEntry )
                {
                    text += formatHash( entry._sourceHash );
                    text += ' ';
                    text += formatHash( entry._importedHash );
                    text += ' ';
                    text += relativePath;
                    text += '\n';
                }
                return text;
            }

            /** @brief 원본 하나가 스탬프와 어긋난 이유입니다. 맞으면 빈 문자열입니다. */
            static string findDriftReason( const StampEntry* pStamped, const StampEntry& current, string_view importedLabel )
            {
                if ( current._sourceHash == 0 )
                    return "원본을 읽지 못했습니다";
                if ( current._importedHash == 0 )
                    return string( importedLabel ) + " 가 없습니다";
                if ( pStamped == nullptr )
                    return "스탬프에 없습니다 (임포트된 적이 없습니다)";
                if ( pStamped->_sourceHash != current._sourceHash )
                    return "원본이나 규칙이 바뀌었는데 다시 임포트하지 않았습니다";
                if ( pStamped->_importedHash != current._importedHash )
                    return string( importedLabel ) + " 가 임포트된 결과와 다릅니다 (손으로 바꿨습니다)";
                return {};
            }

            /** @brief 스탬프를 씁니다. 내용이 같으면 쓰지 않습니다 — 맞는 트리에서 임포트를 돌려도 작업 트리가 더러워지지 않습니다. */
            static void writeStampIfChanged( const string& stampPath, string_view header, const map<string, StampEntry>& mapCurrent, AssetImportSummary& inoutSummary )
            {
                const bool bStampExists = FileUtil::exists( stampPath );
                if ( mapCurrent.empty() && bStampExists == false )
                    return;
                const string stampText = makeStampText( header, mapCurrent );
                string       existingText;
                if ( bStampExists && FileUtil::readTextFile( stampPath, existingText ) )
                {
                    // 체크아웃이 줄 끝을 CRLF 로 바꿔 둘 수 있으므로 CR 은 빼고 비교한다.
                    existingText.erase( std::remove( existingText.begin(), existingText.end(), '\r' ), existingText.end() );
                    if ( existingText == stampText )
                        return;
                }
                if ( FileUtil::writeTextFile( stampPath, stampText ) == false )
                    inoutSummary._listProblem.push_back( "스탬프를 쓰지 못했습니다: " + stampPath );
            }

            /** @brief 원본 폴더 하나를 그 폴더의 스탬프와 대조하고, ImportStale 이면 어긋난 것을 임포트하고 스탬프를 다시 씁니다. */
            static void importRawFolder( const string& rootDir, const string& rawFolder, const IRawAssetImporter& importer, AssetImportMode mode,
                                         AssetImportSummary& inoutSummary )
            {
                const string            stampPath = FileUtil::joinPath( rawFolder, kImportStampFileName );
                map<string, StampEntry> mapStamped;
                readStamp( stampPath, importer.getStampHeader(), mapStamped );

                vector<string> listFile;
                (void)FileUtil::collectFiles( rawFolder, "", listFile, true ); // 폴더는 방금 훑어서 찾았다
                std::sort( listFile.begin(), listFile.end() );

                map<string, StampEntry> mapCurrent;
                for ( const string& file : listFile )
                {
                    const string sourcePath = FileUtil::normalizeSeparators( file );
                    if ( importer.isSourceFile( sourcePath ) == false || sourcePath.size() <= rawFolder.size() + 1 || sourcePath.size() <= rootDir.size() + 1 )
                        continue;

                    const string relativePath = sourcePath.substr( rawFolder.size() + 1 );
                    const string resourcePath = sourcePath.substr( rootDir.size() + 1 );
                    ++inoutSummary._sourceCount;

                    const utf8* const pUnsupportedReason = importer.findUnsupportedReason( sourcePath );
                    if ( pUnsupportedReason != nullptr )
                    {
                        inoutSummary._listProblem.push_back( resourcePath + ": " + pUnsupportedReason );
                        continue;
                    }

                    const string importedPath = importer.makeImportedPath( sourcePath );
                    StampEntry   current;
                    current._sourceHash   = importer.computeSourceHash( sourcePath, resourcePath );
                    current._importedHash = importer.computeImportedHash( importedPath );

                    const auto   itStamped = mapStamped.find( relativePath );
                    const string reason    = findDriftReason( itStamped != mapStamped.end() ? &itStamped->second : nullptr, current, importer.getImportedLabel() );
                    if ( reason.empty() )
                    {
                        mapCurrent[relativePath] = current;
                        continue;
                    }

                    if ( mode == AssetImportMode::CheckOnly )
                    {
                        inoutSummary._listProblem.push_back( resourcePath + ": " + reason );
                        continue;
                    }

                    if ( importer.importSource( sourcePath, importedPath, resourcePath ) == false )
                    {
                        inoutSummary._listProblem.push_back( resourcePath + ": 임포트하지 못했습니다" );
                        continue;
                    }

                    current._importedHash    = importer.computeImportedHash( importedPath );
                    mapCurrent[relativePath] = current;
                    ++inoutSummary._importedCount;
                }

                // 원본이 사라진 줄. 임포트된 결과는 다른 것이 참조할 수 있어 지우지 않는다 — 줄만 지운다.
                for ( const auto& [relativePath, entry] : mapStamped )
                {
                    if ( mapCurrent.find( relativePath ) != mapCurrent.end() )
                        continue;
                    const string sourcePath = FileUtil::joinPath( rawFolder, relativePath );
                    if ( FileUtil::exists( sourcePath ) )
                        continue; // 원본은 있다 — 위에서 어긋남 · 실패로 이미 보고했다
                    if ( mode == AssetImportMode::CheckOnly )
                        inoutSummary._listProblem.push_back( sourcePath.substr( rootDir.size() + 1 ) + ": 원본이 없는데 스탬프에 남아 있습니다" );
                }

                if ( mode == AssetImportMode::CheckOnly )
                    return;
                writeStampIfChanged( stampPath, importer.getStampHeader(), mapCurrent, inoutSummary );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint64 IRawAssetImporter::computeImportedHash( string_view importedPath ) const
    {
        return AssetImportStampUtil::computeFileHash( importedPath );
    }

    AssetImportSummary AssetImportStampUtil::importAll( string_view resourceRoot, const IRawAssetImporter& importer, AssetImportMode mode )
    {
        AssetImportSummary summary;
        const string       rootDir = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );

        vector<string> listFolder;
        if ( rootDir.empty() || FileUtil::collectFolders( rootDir, listFolder, true ) == false )
        {
            summary._listProblem.push_back( "리소스 루트를 훑지 못했습니다: " + rootDir );
            return summary;
        }

        std::sort( listFolder.begin(), listFolder.end() );
        for ( const string& folder : listFolder )
        {
            const string normalizedFolder = FileUtil::normalizeSeparators( folder );
            if ( FileUtil::getFileNamePart( normalizedFolder ) == importer.getRawFolderName() )
                AssetImportStampInternal::importRawFolder( rootDir, normalizedFolder, importer, mode, summary );
        }
        return summary;
    }

    string AssetImportStampUtil::makeImportedPath( string_view rawPath, string_view rawFolderName, string_view importedFolderName, string_view importedExtension )
    {
        string rawFolder;
        string relativePath;
        if ( AssetImportStampInternal::splitRawPath( rawPath, rawFolderName, rawFolder, relativePath ) == false )
            return {};

        const string domainFolder   = FileUtil::getDirectoryPart( rawFolder );
        const string importedFolder = domainFolder.empty() ? string( importedFolderName ) : FileUtil::joinPath( domainFolder, importedFolderName );
        return FileUtil::replaceExtension( FileUtil::joinPath( importedFolder, relativePath ), importedExtension );
    }

    uint64 AssetImportStampUtil::computeFileHash( string_view path )
    {
        vector<uint8> bytes;
        if ( FileUtil::exists( path ) == false || FileUtil::readFile( path, bytes ) == false || bytes.empty() )
            return 0;
        return StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
    }
} // namespace sw::editor
