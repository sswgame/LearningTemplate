#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/PlatformFileSystem.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/string_splitter.h"

namespace sw
{
    SW_LOG_CALLER( "FileUtil" );

    namespace
    {
        /** @brief 프로세스에 하나인 파일 쓰기 카운터 묶음입니다(정적 가변 변수 한 칸으로 묶어 둔다 — `FileUtil` 은 정적 함수 묶음이라 소유 객체가 없다).
         *  @details 감싸는 클래스 밖에 둔다 — 안에 두면 기본 멤버 초기화가 감싸는 클래스 정의가 끝나기 전에 필요해 `static inline` 멤버를 만들 수 없다. */
        struct FileWriteCounters
        {
            /** @brief 임시 파일 이름을 겹치지 않게 하는 번호입니다(같은 프로세스의 여러 스레드가 같은 파일을 동시에 저장해도 서로 밟지 않게). */
            atomic<uint32> _tempFileSerial{ 0 };
            /** @brief 파일 · 폴더를 만든 횟수입니다(`FileUtil::getCreationEpoch`). */
            atomic<uint64> _creationEpoch{ 0 };
            /** @brief 파일 · 폴더를 지운 횟수입니다(`FileUtil::getRemovalEpoch`). */
            atomic<uint64> _removalEpoch{ 0 };
            /** @brief 쓰기 입구 게이트입니다(`FileUtil::setWriteGate`). 함수 포인터 하나라 상수 초기화된다. */
            atomic<FileUtil::WriteGate> _writeGate{ nullptr };
        };

        struct FileUtilInternal
        {
            static inline FileWriteCounters s_writeCounters{};

            /**
             * @brief 폴더 @p directory 안에서 이름이 @p component 와 대소문자만 다른 항목의 디스크 표기를 찾습니다.
             * @details 이름만 대소문자가 다른 항목이 여럿이어도 같은 답이 나오게, 소문자가 같은 항목 중 정렬 순으로 앞선 것을 고른다.
             * @return 찾으면 true 이고 @p outName 이 디스크 표기입니다. 폴더를 열지 못했거나 항목이 없으면 false 입니다.
             */
            [[nodiscard]] static bool findEntryIgnoringCase( const string& directory, string_view component, string& outName )
            {
                const string lowerComponent = FileUtil::normalizePath( component );
                outName.clear();
                (void)FileUtil::forEachDirectoryEntry( directory, false, [&outName, &lowerComponent]( const DirectoryEntry& entry )
                {
                    const size_t      nameStart = entry._path.rfind( '/' );
                    const string_view name      = nameStart == string_view::npos ? entry._path : entry._path.substr( nameStart + 1 );
                    if ( FileUtil::normalizePath( name ) == lowerComponent && ( outName.empty() || name < string_view( outName ) ) )
                        outName = string( name );
                    return true;
                } ); // 폴더를 열지 못하면 항목이 없는 것과 같다 — 아래가 outName 이 비었는지로 가른다
                return outName.empty() == false;
            }

            /** @brief 실패한 쓰기의 임시 파일을 지웁니다. 지우지 못하면 경고합니다 — 원본 옆에 `.tmp<pid>_<번호>` 가 남습니다. */
            static void discardTempFile( const string& tempPath )
            {
                if ( FileUtil::removeFile( tempPath ) == false )
                    SW_LOG_WARNING( "Failed to remove the temporary file %# - delete it by hand", tempPath.c_str() );
            }

            /**
             * @brief @p fileName 을 **원자적으로** 씁니다: 같은 폴더의 임시 파일에 다 쓰고, 쓰기 · 닫기 결과를 확인한 뒤 원본 경로로 교체합니다.
             * @details 주의: 원본을 "wb" 로 열면 그 순간 길이 0 이 됩니다. 그 파일에 바로 쓰면서 `fwrite` · `fclose` 결과를 보지 않으면, 디스크가
             *          차거나 백신이 파일을 잡거나 쓰는 도중 죽을 때 씬 · 에셋 · `.meta` 가 빈 파일로 남는데 저장은 성공으로 보입니다. 4 KB 보다
             *          작은 쓰기는 stdio 버퍼에 머물다 `fclose` 에서야 디스크로 가므로, 가득 찬 디스크는 `fclose` 만 알려 줍니다.
             *          언리얼 `FFileHelper::SaveArrayToFile` 과 같은 방식(임시 파일 → 이름 바꾸기)입니다.
             */
            [[nodiscard]] static bool writeAtomically( string_view fileName, const void* pData, size_t size )
            {
                const string filePath = FileUtil::normalizeSeparators( fileName );
                if ( FileUtil::isWriteAllowed( filePath ) == false )
                    return false;
                const uint64 processID = static_cast<uint64>( Process::getCurrentProcessID() );
                // 같은 폴더에 둔다. 다른 볼륨(시스템 임시 폴더)이면 이름 바꾸기가 원자적이지 않고 복사가 된다.
                StringBuilder<constant::kMaxPathSize> tempPathBuilder;
                tempPathBuilder.append( filePath.c_str() )
                    .append( ".tmp" )
                    .append( processID )
                    .append( '_' )
                    .append( s_writeCounters._tempFileSerial.fetch_add( 1, std::memory_order_relaxed ) );
                const string tempPath{ tempPathBuilder.c_str() };

                FILE* pFile = PlatformFileUtil::openFile( tempPath.c_str(), "wb" );
                if ( pFile == nullptr )
                {
                    SW_LOG_ERROR_ID( "File.WriteFailed", "Failed to open temporary file for writing: %#", tempPath.c_str() );
                    return false;
                }

                const size_t written          = ( size > 0 ) ? std::fwrite( pData, 1, size, pFile ) : 0;
                const bool   bFlushed         = std::fflush( pFile ) == 0;
                const bool   bClosed          = std::fclose( pFile ) == 0;
                const bool   bWriteIncomplete = written != size || bFlushed == false || bClosed == false;
                if ( bWriteIncomplete )
                {
                    SW_LOG_ERROR_ID( "File.WriteFailed", "Failed to write %# bytes to %# (wrote %#) — the original file was left untouched", size, filePath.c_str(), written );
                    discardTempFile( tempPath );
                    return false;
                }

                // 다른 프로세스(파일 감시 · 백신 · 에디터)가 대상을 잠깐 쥐고 있으면 Windows 의 바꿔치기가 실패한다. 잠깐 기다렸다 다시 한다.
                constexpr uint32 kReplaceAttemptCount = 10;
                for ( uint32 attemptIndex = 0; attemptIndex < kReplaceAttemptCount; ++attemptIndex )
                {
                    if ( PlatformFileUtil::replaceFile( tempPath.c_str(), filePath.c_str() ) )
                    {
                        FileUtil::noteFileCreated();
                        return true;
                    }
                    ThreadUtil::sleepForMilliseconds( 20 );
                }
                SW_LOG_ERROR_ID( "File.WriteFailed", "Failed to replace %# with the newly written file — the original file was left untouched", filePath.c_str() );
                discardTempFile( tempPath );
                return false;
            }

            /**
             * @brief 파일의 [offset, offset + maxReadCount) 를 @p outBuffer 에 읽습니다(버퍼 크기는 실제로 읽은 만큼입니다). 실패하면 로그를 남기고 false 입니다.
             * @details `readFile` · `readTextFile` 의 본체입니다. OS 핸들로 열기 · 크기 · 위치 지정 읽기 · 닫기다(`PlatformFileUtil` 의 네이티브 핸들 - 비동기 IO 와 같은 입구).
             *          stdio(`fopen` → 끝으로 이동 → 위치 질의 → 되감기 → `fread`)보다 시스템 호출이 적고, Windows 는 UTF-16 으로 연다(좁은 문자 `fopen` 은 ANSI 코드 페이지로 읽는다).
             *          공유는 읽기 · 쓰기 · 지우기를 막지 않는다 - 읽는 동안에도 저장(`writeAtomically`)이 이 파일을 새 파일로 바꿔 끼울 수 있다.
             */
            template <typename BufferType>
            [[nodiscard]] static OptionalFileRead readRange( string_view fileName, uint64 offset, uint64 maxReadCount, BufferType& outBuffer, bool bMissingIsError )
            {
                const string           filePath = FileUtil::normalizeSeparators( fileName );
                const NativeFileHandle handle   = PlatformFileUtil::openNativeFileForRead( filePath.c_str() );
                if ( handle == kInvalidNativeFileHandle )
                {
                    // 열지 못한 뒤 없으면 "없음" 이다(그 사이 지워진 것도 없음이다). 있는데 못 열었으면(권한 · 잠금) 늘 오류다.
                    if ( bMissingIsError == false && FileUtil::exists( filePath ) == false )
                        return OptionalFileRead::Missing;
                    SW_LOG_ERROR_ID( "File.NotFound", "File not found: %#", fileName );
                    return OptionalFileRead::Failed;
                }

                const int64 fileSize = PlatformFileUtil::getNativeFileSize( handle );
                if ( fileSize < 0 )
                {
                    PlatformFileUtil::closeNativeFile( handle );
                    SW_LOG_ERROR( "Failed to query size of: %#", fileName );
                    return OptionalFileRead::Failed;
                }
                const uint64 uFileSize = static_cast<uint64>( fileSize );
                if ( offset > uFileSize )
                {
                    PlatformFileUtil::closeNativeFile( handle );
                    SW_LOG_ERROR_ID( "File.ReadPastEnd", "Read offset %# exceeds size %# of: %#", offset, uFileSize, fileName );
                    return OptionalFileRead::Failed;
                }

                const uint64 dataSize = MathUtil::min( uFileSize - offset, maxReadCount );
                outBuffer.resize( static_cast<size_t>( dataSize ) );
                size_t     readBytes = 0;
                const bool bRead     = PlatformFileUtil::readNativeFileAt( handle, offset, outBuffer.data(), static_cast<size_t>( dataSize ), readBytes );
                PlatformFileUtil::closeNativeFile( handle );
                // 측정한 크기만큼 읽지 못했으면 실패다: 읽기 오류이거나, 읽는 도중 누가 파일을 줄였다(에디터 저장 · 핫 리로드). 버퍼를 줄이고
                // true 를 돌려주면 잘린 JSON · XML · 설정이 "정상으로 읽은 것" 이 되고, 체크섬이 없는 호출부는 알 길이 없다.
                if ( bRead == false || readBytes != static_cast<size_t>( dataSize ) )
                {
                    outBuffer.clear();
                    SW_LOG_ERROR( "Short read (%# of %# bytes) from: %#", readBytes, dataSize, fileName );
                    return OptionalFileRead::Failed;
                }
                return OptionalFileRead::Read;
            }

            /** @brief `readTextFile` · `readOptionalTextFile` 의 본체입니다 - 읽고, 연 횟수를 세고, UTF-8 BOM 을 뗀다. @p bMissingIsError 만 다르다. */
            [[nodiscard]] static OptionalFileRead readText( string_view fileName, string& outText, bool bMissingIsError )
            {
                const OptionalFileRead result = readRange( fileName, 0, std::numeric_limits<uint64>::max(), outText, bMissingIsError );
                if ( result != OptionalFileRead::Read )
                    return result;
                PlatformFileUtil::noteFileOpenedForRead( fileName, outText.size() ); // 디스크 흉내 지연은 위치 지정 읽기가 이미 냈다

                // BOM 판정의 기준은 skipUTF8Bom 이다. 여기서 바이트를 따로 세면 한쪽만 고쳤을 때 읽기 경로와 질의 경로가 서로 다른 답을 준다.
                const string_view withoutBom = FileUtil::skipUTF8Bom( outText );
                if ( withoutBom.size() != outText.size() )
                    outText.erase( 0, outText.size() - withoutBom.size() );
                return OptionalFileRead::Read;
            }
            /** @brief 재귀 순회가 폴더마다 쓰는 상태입니다. */
            struct WalkContext
            {
                string                _directory; ///< `/` 구분, 끝에 구분자 없음
                bool                  _bRecursive{ false };
                DirectoryEntryVisitFn _visit{ nullptr };
                void*                 _pContext{ nullptr };
                bool                  _bStopped{ false }; ///< 콜백이 멈췄다
            };

            /** @brief 폴더 안 이름들을 모으는 상태입니다. */
            struct CollectContext
            {
                vector<string> _listName;
                vector<uint8>  _listDirectory; ///< `_listName` 과 같은 순서, 링크가 아닌 폴더면 1
            };

            /** @brief 경로를 `/` 구분으로 맞추고 끝의 구분자를 지웁니다(루트 `/` · `C:/` 는 남긴다). */
            static string trimTrailingSeparators( string_view path )
            {
                string result = FileUtil::normalizeSeparators( path );
                while ( result.size() > 1 && result.back() == '/' && result.back() != result[result.size() - 2] && result[result.size() - 2] != ':' )
                    result.pop_back();
                return result;
            }

            /** @brief 경로를 구간으로 나눕니다(빈 구간 제외). 글을 가리키는 뷰라 @p path 가 살아 있는 동안만 유효합니다. */
            static vector<string_view> splitComponents( const string& path )
            {
                vector<string_view> listComponent;
                size_t              begin = 0;
                while ( begin < path.size() )
                {
                    size_t end = path.find( '/', begin );
                    if ( end == string::npos )
                        end = path.size();
                    if ( end > begin )
                        listComponent.push_back( string_view{ path.data() + begin, end - begin } );
                    begin = end + 1;
                }
                return listComponent;
            }

            /** @brief 두 경로 구간이 같은지입니다(Windows 는 대소문자 무시 — 영문자만 접는다). */
            static bool isSameComponent( string_view left, string_view right )
            {
                if ( left.size() != right.size() )
                    return false;
                for ( size_t index = 0; index < left.size(); ++index )
                {
                    if ( PlatformFileSystem::kPathsAreCaseInsensitive )
                    {
                        if ( StringUtil::toLowerChar( left[index] ) != StringUtil::toLowerChar( right[index] ) )
                            return false;
                    }
                    else if ( left[index] != right[index] )
                    {
                        return false;
                    }
                }
                return true;
            }

            /** @brief 폴더 하나와 그 아래를 만듭니다. 이 호출이 하나라도 만들었으면 true 입니다(이미 있거나 다른 스레드가 먼저 만들었으면 false). */
            static bool createDirectoryTree( string_view path )
            {
                if ( PlatformFileSystem::createDirectory( path ) )
                    return true;
                if ( FileUtil::isDirectory( path ) )
                    return false;
                const string parent = FileUtil::getDirectoryPart( path );
                if ( parent.empty() || parent == path )
                    return false;
                const bool bParentCreated = createDirectoryTree( parent );
                return PlatformFileSystem::createDirectory( path ) || bParentCreated;
            }

            static bool collectChild( void* pContext, string_view name, bool bDirectory, bool bLink )
            {
                CollectContext& context = *static_cast<CollectContext*>( pContext );
                context._listName.push_back( string{ name } );
                context._listDirectory.push_back( bDirectory && bLink == false ? 1 : 0 );
                return true;
            }

            /** @brief 폴더 안을 비우고 폴더 자신도 지웁니다(링크는 따라가지 않고 링크만 지운다). 지운 항목 수를 돌려줍니다. */
            static size_t removeTree( const string& directory )
            {
                CollectContext collected;
                (void)PlatformFileSystem::forEachChild( directory, collectChild, &collected ); // 못 열면 폴더 지우기가 실패로 알린다 - 이유는 호출한 쪽이 로그
                size_t removedCount = 0;
                for ( size_t index = 0; index < collected._listName.size(); ++index )
                {
                    string child = directory;
                    child.push_back( '/' );
                    child.append( collected._listName[index] );
                    if ( collected._listDirectory[index] != 0 )
                        removedCount += removeTree( child );
                    else if ( PlatformFileSystem::removeEntry( child ) )
                        ++removedCount;
                }
                if ( PlatformFileSystem::removeEntry( directory ) )
                    ++removedCount;
                return removedCount;
            }

            static bool walkChild( void* pContext, string_view name, bool bDirectory, bool bLink )
            {
                WalkContext& walk = *static_cast<WalkContext*>( pContext );
                string       path = walk._directory;
                path.push_back( '/' );
                path.append( name.data(), name.size() );

                const DirectoryEntry entry{ string_view{ path }, bDirectory };
                if ( walk._visit( walk._pContext, entry ) == false )
                {
                    walk._bStopped = true;
                    return false;
                }
                if ( walk._bRecursive && bDirectory && bLink == false )
                {
                    WalkContext child;
                    child._directory  = path;
                    child._bRecursive = true;
                    child._visit      = walk._visit;
                    child._pContext   = walk._pContext;
                    // 들어갈 수 없는 하위 폴더는 멈추지 않고 건너뛴다(권한 없음).
                    (void)PlatformFileSystem::forEachChild( path, walkChild, &child );
                    if ( child._bStopped )
                    {
                        walk._bStopped = true;
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    void FileUtil::splitPath( string_view fullPath, string_view& outDirectoryPath, string_view& outFileName )
    {
        const size_t found = fullPath.find_last_of( "/\\" );
        if ( found == string_view::npos )
        {
            outDirectoryPath = {};
            outFileName      = fullPath;
            return;
        }
        outDirectoryPath = fullPath.substr( 0, found + 1 );
        outFileName      = fullPath.substr( found + 1 );
    }

    void FileUtil::getFileNamePart( string_view fullPath, string_view& outFileName )
    {
        if ( fullPath.empty() )
        {
            outFileName = {};
            return;
        }

        const size_t found = fullPath.find_last_of( "/\\" );
        if ( found == string_view::npos )
        {
            outFileName = fullPath;
            return;
        }

        outFileName = fullPath.substr( found + 1 );
    }

    string FileUtil::getFileNamePart( string_view fullPath )
    {
        string_view fileView;
        getFileNamePart( fullPath, fileView );
        return string{ fileView };
    }

    void FileUtil::getDirectoryPart( string_view fullPath, string_view& outDirectoryPath )
    {
        if ( fullPath.empty() )
        {
            outDirectoryPath = {};
            return;
        }

        string_view v = fullPath;
        while ( v.size() > 1 && ( v.back() == '/' || v.back() == '\\' ) )
        {
            v.remove_suffix( 1 );
        }

        if ( v.empty() )
        {
            outDirectoryPath = {};
            return;
        }

        const size_t found = v.find_last_of( "/\\" );
        if ( found == string_view::npos )
        {
            outDirectoryPath = {};
            return;
        }

        outDirectoryPath = v.substr( 0, found );
    }

    string FileUtil::getDirectoryPart( string_view fullPath )
    {
        string_view dirView;
        getDirectoryPart( fullPath, dirView );
        return string{ dirView };
    }

    void FileUtil::getExtension( string_view fileName, string_view& outExtension )
    {
        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );
        if ( dot == string_view::npos || dot < start )
        {
            outExtension = {};
            return;
        }
        outExtension = fileName.substr( dot );
    }

    string FileUtil::getExtension( string_view fileName )
    {
        string_view extView;
        getExtension( fileName, extView );
        return string{ extView };
    }

    bool FileUtil::hasExtension( string_view fileName, string_view extension )
    {
        if ( extension.empty() || fileName.empty() )
            return false;

        string_view expected = extension;
        if ( expected.front() == '.' )
            expected.remove_prefix( 1 );

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );
        if ( dot == string_view::npos || dot < start )
            return false;

        // 두 단계 이상 접미사(".modifiers.json")는 마지막 점 뒤만 보면 영영 안 맞는다 — 파일 이름이 "." + 접미사로 끝나는지 본다.
        if ( expected.find( '.' ) != string_view::npos )
        {
            const string_view name = fileName.substr( start );
            if ( name.size() <= expected.size() )
                return false;
            const string_view tail = name.substr( name.size() - expected.size() - 1 );
            return tail.front() == '.' && StringUtil::equals( tail.substr( 1 ), expected, true );
        }

        const string_view actual = fileName.substr( dot + 1 );
        return StringUtil::equals( actual, expected, true );
    }

    bool FileUtil::hasAnyExtension( string_view fileName, std::initializer_list<string_view> listExtension )
    {
        for ( string_view extension : listExtension )
        {
            if ( hasExtension( fileName, extension ) )
                return true;
        }
        return false;
    }

    string FileUtil::replaceExtension( string_view fileName, string_view extension )
    {
        if ( fileName.empty() )
            return string( extension );

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );

        string_view base = fileName;
        if ( dot != string_view::npos && dot >= start )
            base = fileName.substr( 0, dot );

        string_view ext = extension;
        if ( ext.empty() )
            return string( base );

        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( base );
        if ( ext.front() != '.' )
            sb.append( '.' );
        sb.append( ext );
        return string( sb.view() );
    }

    void FileUtil::removeExtension( string_view fileName, string_view& outFileName )
    {
        if ( fileName.empty() )
        {
            outFileName = {};
            return;
        }

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );

        if ( dot != string_view::npos && dot >= start )
        {
            outFileName = fileName.substr( 0, dot );
            return;
        }

        outFileName = fileName;
    }

    string FileUtil::removeExtension( string_view fileName )
    {
        string_view stemView;
        removeExtension( fileName, stemView );
        return string{ stemView };
    }

    bool FileUtil::isAbsolutePath( string_view path )
    {
        if ( path.empty() )
            return false;

        const bool bHasDriveLetterPrefix = path.size() >= 2 && ( ( 'a' <= path[0] && path[0] <= 'z' ) || ( 'A' <= path[0] && path[0] <= 'Z' ) ) && path[1] == ':';
        if ( bHasDriveLetterPrefix )
            return true;

        return path[0] == '/' || path[0] == '\\';
    }

    string FileUtil::normalizePath( string_view path )
    {
        string outResult{ path };
        for ( utf8& ch : outResult )
        {
            if ( ch == '\\' )
                ch = '/';
            else
                ch = StringUtil::toLowerChar( ch );
        }
        return outResult;
    }

    string FileUtil::normalizeSeparators( string_view path )
    {
        string outResult{ path };
        StringUtil::replaceChar( outResult, '\\', '/' );
        return outResult;
    }

    string FileUtil::toNativeSeparators( string_view path )
    {
        string outResult{ path };
        StringUtil::replaceChar( outResult, PlatformFileSystem::kNativeSeparator == '/' ? '\\' : '/', PlatformFileSystem::kNativeSeparator );
        return outResult;
    }

    bool FileUtil::pathsEqualNormalized( string_view lhs, string_view rhs )
    {
        const string aNorm = normalizePath( trimTrailingSlashes( lhs ) );
        const string bNorm = normalizePath( trimTrailingSlashes( rhs ) );
        return aNorm == bNorm;
    }

    string FileUtil::trimTrailingSlashes( string_view path )
    {
        string_view v = path;
        while ( v.size() > 1 && ( v.back() == '/' || v.back() == '\\' ) )
        {
            v.remove_suffix( 1 );
        }
        return string{ v };
    }

    string FileUtil::joinPath( string_view root, string_view relative )
    {
        if ( root.empty() )
            return {};

        string_view r = root;
        while ( r.size() > 1 && ( r.back() == '/' || r.back() == '\\' ) )
        {
            r.remove_suffix( 1 );
        }

        if ( relative.empty() )
            return normalizeSeparators( r );

        string_view rel = relative;
        while ( rel.empty() == false && ( rel.front() == '/' || rel.front() == '\\' ) )
        {
            rel.remove_prefix( 1 );
        }

        StringBuilder<constant::kMaxBuffer512> sb;
        (void)sb.ensureCapacity( static_cast<uint32>( r.size() + 1 + rel.size() ) ); // 미리 잡기일 뿐이다 — 못 잡으면 append 가 저마다 다시 잡는다
        for ( const utf8 ch : r )
        {
            sb.append( ( ch == '\\' ) ? '/' : ch );
        }

        if ( rel.empty() == false )
        {
            if ( sb.view().empty() == false && sb.view().back() != '/' )
                sb.append( '/' );
            for ( const utf8 ch : rel )
            {
                sb.append( ( ch == '\\' ) ? '/' : ch );
            }
        }

        return string( sb.view() );
    }

    bool FileUtil::startsWithPathComponent( string_view path, string_view component )
    {
        if ( path.size() < component.size() )
            return false;
        if ( path.compare( 0, component.size(), component ) != 0 )
            return false;
        return path.size() == component.size() || path[component.size()] == '/' || path[component.size()] == '\\';
    }

    string FileUtil::suffixAfterPathComponent( string_view path, string_view component )
    {
        if ( path.size() <= component.size() )
            return {};
        // "component/" 를 건너뛴다
        return string{ path.substr( component.size() + 1 ) };
    }

    bool FileUtil::tryMakePathUnderRoot( string_view root, string_view path, string& outRelative, bool bIgnoreCase )
    {
        const string rootText   = trimTrailingSlashes( normalizeSeparators( root ) );
        const string pathText   = normalizeSeparators( path );
        const bool   bUnderRoot = rootText.empty() == false && pathText.size() > rootText.size() + 1 && pathText[rootText.size()] == '/' && StringUtil::startsWith( pathText, rootText, bIgnoreCase );
        if ( bUnderRoot == false )
            return false;
        outRelative = pathText.substr( rootText.size() + 1 );
        return true;
    }

    bool FileUtil::ensureParentDirectoryExists( string_view filePath )
    {
        // 구분자 정규화까지 `ensureDirectoryExists` 와 같게 한다.
        return ensureDirectoryExists( getDirectoryPart( filePath ) );
    }

    string FileUtil::getExecutablePath()
    {
        // 실행 파일 경로를 못 얻으면 어느 루트도 정할 수 없다 - 빈 글로 물러나지 않고 멈춘다(예전에는 빈 글을 받은 쪽마다 따로 실패했다).
        string executablePath;
        if ( PlatformFileSystem::getExecutablePath( executablePath ) == false || executablePath.empty() )
            SW_FATAL( "The executable path could not be queried from the OS" );
        return executablePath;
    }

    bool FileUtil::getFileStamp( string_view fileName, FileStamp& outStamp )
    {
        // 크기와 시각을 한 번의 조회로 얻는다(`getFileSize` · `getFileWriteTime` 두 번이 아니라). 폴더는 false 다.
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( fileName, status ) == false || status._kind != PlatformFileSystem::FileKind::File )
            return false;
        outStamp._sizeBytes = status._size;
        outStamp._writeTime = static_cast<uint64>( status._writeTicks );
        return true;
    }

    uint64 FileUtil::getCreationEpoch()
    {
        return FileUtilInternal::s_writeCounters._creationEpoch.load( std::memory_order_acquire );
    }

    void FileUtil::noteFileCreated()
    {
        FileUtilInternal::s_writeCounters._creationEpoch.fetch_add( 1, std::memory_order_release );
    }

    uint64 FileUtil::getRemovalEpoch()
    {
        return FileUtilInternal::s_writeCounters._removalEpoch.load( std::memory_order_acquire );
    }

    void FileUtil::noteFileRemoved()
    {
        FileUtilInternal::s_writeCounters._removalEpoch.fetch_add( 1, std::memory_order_release );
    }

    void FileUtil::setWriteGate( WriteGate gate )
    {
        FileUtilInternal::s_writeCounters._writeGate.store( gate, std::memory_order_release );
    }

    bool FileUtil::isWriteAllowed( string_view path )
    {
        const WriteGate gate = FileUtilInternal::s_writeCounters._writeGate.load( std::memory_order_acquire );
        if ( gate == nullptr )
            return true;
        string reason;
        if ( gate( path, reason ) )
            return true;
        SW_LOG_ERROR_ID( "File.WriteRefused", "Changing %# was refused: %#", string( path ).c_str(), reason.c_str() );
        return false;
    }

    bool FileUtil::writeFile( string_view fileName, const uint8* pData, const uint64 size )
    {
        if ( size == 0 || pData == nullptr )
            return false;
        static_assert( sizeof( size_t ) == sizeof( uint64 ), "64 비트 대상만 지원한다 — size_t 로 줄여도 잘리지 않는다" );
        return FileUtilInternal::writeAtomically( fileName, pData, static_cast<size_t>( size ) );
    }

    bool FileUtil::readFile( string_view fileName, vector<uint8>& outBytes, const uint64 offset, const uint64 maxReadCount )
    {
        if ( FileUtilInternal::readRange( fileName, offset, maxReadCount, outBytes, true ) != OptionalFileRead::Read )
            return false;
        PlatformFileUtil::noteFileOpenedForRead( fileName, outBytes.size() ); // 디스크 흉내 지연은 위치 지정 읽기가 이미 냈다
        return true;
    }

    bool FileUtil::readStreamTextFile( string_view fileName, string& outText, const uint64 maxBytes )
    {
        outText.clear();
        const string filePath = normalizeSeparators( fileName );
        FILE*        pFile    = PlatformFileUtil::openFile( filePath.c_str(), "rb" );
        if ( pFile == nullptr )
        {
            SW_LOG_ERROR_ID( "File.NotFound", "File not found: %#", fileName );
            return false;
        }
        // 크기를 재지 않고 끝(EOF)까지 덩이로 읽는다 — 의사 파일은 크기를 0 으로 알린다.
        utf8 arrChunk[constant::kMaxBuffer4096];
        bool bWithinLimit = true;
        while ( bWithinLimit )
        {
            const size_t readBytes = std::fread( arrChunk, 1, sizeof( arrChunk ), pFile );
            if ( readBytes == 0 )
                break;
            bWithinLimit = outText.size() + readBytes <= maxBytes;
            if ( bWithinLimit )
                outText.append( arrChunk, readBytes );
        }
        const bool bReadError = std::ferror( pFile ) != 0;
        std::fclose( pFile );
        if ( bReadError || bWithinLimit == false )
        {
            outText.clear();
            SW_LOG_ERROR( "Failed to read %# to the end (%#)", fileName, bReadError ? "read error" : "larger than the limit" );
            return false;
        }
        PlatformFileUtil::noteFileOpenedForRead( fileName, outText.size() );
        return true;
    }

    bool FileUtil::readTextFile( string_view fileName, string& outText )
    {
        return FileUtilInternal::readText( fileName, outText, true ) == OptionalFileRead::Read;
    }

    OptionalFileRead FileUtil::readOptionalTextFile( string_view fileName, string& outText )
    {
        return FileUtilInternal::readText( fileName, outText, false );
    }

    bool FileUtil::writeCrashFile( const utf8* pPath, const void* pData, size_t size )
    {
        const NativeFileHandle handle = PlatformFileUtil::createNativeFileForWrite( pPath );
        if ( handle == kInvalidNativeFileHandle )
            return false;
        const bool bWritten = PlatformFileUtil::writeNativeFile( handle, pData, size );
        PlatformFileUtil::closeNativeFile( handle );
        return bWritten;
    }

    bool FileUtil::writeTextFile( string_view fileName, string_view text )
    {
        return FileUtilInternal::writeAtomically( fileName, text.data(), text.size() );
    }

    bool FileUtil::appendTextFile( string_view fileName, string_view text )
    {
        const string filePath = normalizeSeparators( fileName );
        if ( isWriteAllowed( filePath ) == false )
            return false;
        FILE* pFile = PlatformFileUtil::openFile( filePath.c_str(), "ab" );
        if ( pFile == nullptr )
            return false;
        const size_t written = text.empty() ? 0 : std::fwrite( text.data(), 1, text.size(), pFile );
        const bool   bClosed = std::fclose( pFile ) == 0;
        if ( written != text.size() || bClosed == false )
            return false;
        noteFileCreated();
        return true;
    }

    bool FileUtil::moveToRecycleBin( const vector<string>& listPath )
    {
        for ( const string& path : listPath )
        {
            if ( exists( path ) && isWriteAllowed( path ) == false )
                return false;
        }
        const bool bMoved = PlatformFileUtil::moveToRecycleBin( listPath );
        noteFileRemoved(); // 일부만 옮겼어도 옮긴 항목의 옛 경로는 낡았다
        return bMoved;
    }

    string_view FileUtil::skipUTF8Bom( string_view text )
    {
        const bool bHasUTF8Bom = text.size() >= 3 &&
                                 static_cast<uint8>( text[0] ) == 0xEF &&
                                 static_cast<uint8>( text[1] ) == 0xBB &&
                                 static_cast<uint8>( text[2] ) == 0xBF;
        if ( bHasUTF8Bom )
            return text.substr( 3 );
        return text;
    }

    void FileUtil::skipUTF8Bom( const uint8*& pData, size_t& size )
    {
        const bool bHasUTF8Bom = pData != nullptr && size >= 3 &&
                                 pData[0] == 0xEF &&
                                 pData[1] == 0xBB &&
                                 pData[2] == 0xBF;
        if ( bHasUTF8Bom )
        {
            pData += 3;
            size -= 3;
        }
    }

    string FileUtil::findPathIgnoringCase( string_view root, string_view lowerRelative )
    {
        string current = trimTrailingSlashes( normalizeSeparators( root ) );
        size_t start   = 0;
        while ( start < lowerRelative.size() )
        {
            size_t end = lowerRelative.find( '/', start );
            if ( end == string_view::npos )
                end = lowerRelative.size();
            const string_view component = lowerRelative.substr( start, end - start );
            start                       = end + 1;
            if ( component.empty() )
                continue;
            string match;
            if ( FileUtilInternal::findEntryIgnoringCase( current, component, match ) == false )
                return {};
            current = joinPath( current, match );
        }
        return current;
    }

    string FileUtil::makePathIgnoringCase( string_view root, string_view relative )
    {
        string current  = trimTrailingSlashes( normalizeSeparators( root ) );
        bool   bPresent = true; // 앞 성분까지 디스크에 있다 — 없는 성분 아래는 있을 수 없어 더 훑지 않는다
        size_t start    = 0;
        while ( start < relative.size() )
        {
            size_t end = relative.find_first_of( "/\\", start );
            if ( end == string_view::npos )
                end = relative.size();
            const string_view component = relative.substr( start, end - start );
            start                       = end + 1;
            if ( component.empty() )
                continue;
            string match;
            if ( bPresent && FileUtilInternal::findEntryIgnoringCase( current, component, match ) )
            {
                current = joinPath( current, match );
                continue;
            }
            bPresent = false;
            current  = joinPath( current, component );
        }
        return current;
    }

    bool FileUtil::collectFiles( string_view directory, string_view filterExtension, vector<string>& outListFilePath, const bool bRecursive, const StopToken& stopToken )
    {
        if ( isDirectory( directory ) == false )
            return false;

        const bool bHasFilter = filterExtension.empty() == false;
        const bool bWalked    = forEachDirectoryEntry( directory, bRecursive, [&outListFilePath, filterExtension, bHasFilter, stopToken]( const DirectoryEntry& entry )
           {
            if ( stopToken.isStopRequested() )
                return false;
            if ( entry._bDirectory || ( bHasFilter && hasExtension( entry._path, filterExtension ) == false ) )
                return true;
            outListFilePath.push_back( string( entry._path ) );
            return true;
        } );
        const bool bStopped   = stopToken.isStopRequested();
        if ( bWalked == false && bStopped == false )
            SW_LOG_WARNING( "Collecting files under '%#' stopped partway - the list is incomplete", directory );
        return bWalked;
    }

    bool FileUtil::collectFolders( string_view directory, vector<string>& outListFolder, const bool bRecursive )
    {
        if ( isDirectory( directory ) == false )
            return false;

        const bool bWalked = forEachDirectoryEntry( directory, bRecursive, [&outListFolder]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory )
                outListFolder.push_back( string( entry._path ) );
            return true;
        } );
        if ( bWalked == false )
            SW_LOG_WARNING( "Collecting folders under '%#' stopped partway - the list is incomplete", directory );
        return bWalked;
    }

} // namespace sw

namespace sw
{
    bool FileUtil::makeRelativePath( string_view rootDir, string_view path, string& outResult )
    {
        if ( rootDir.empty() || path.empty() || AbsolutePath::isStrictAbsolute( path ) == false )
            return false;

        // 글 연산이다(링크를 풀지 않는다). `.` · `..` 는 절대화가 정리한다.
        string absoluteRoot;
        string absolutePath;
        if ( PlatformFileSystem::makeAbsolute( rootDir, absoluteRoot ) == false || PlatformFileSystem::makeAbsolute( path, absolutePath ) == false )
            return false;
        const vector<string_view> listRoot = FileUtilInternal::splitComponents( absoluteRoot );
        const vector<string_view> listPath = FileUtilInternal::splitComponents( absolutePath );

        size_t commonCount = 0;
        while ( commonCount < listRoot.size() && commonCount < listPath.size() && FileUtilInternal::isSameComponent( listRoot[commonCount], listPath[commonCount] ) )
            ++commonCount;
        if ( PlatformFileSystem::kPathsAreCaseInsensitive && commonCount == 0 )
            return false; // 드라이브 · 서버가 다르면 상대 경로가 없다

        outResult.clear();
        for ( size_t index = commonCount; index < listRoot.size(); ++index )
        {
            if ( outResult.empty() == false )
                outResult.push_back( '/' );
            outResult.append( ".." );
        }
        for ( size_t index = commonCount; index < listPath.size(); ++index )
        {
            if ( outResult.empty() == false )
                outResult.push_back( '/' );
            outResult.append( listPath[index].data(), listPath[index].size() );
        }
        if ( outResult.empty() )
            outResult = ".";
        return true;
    }

    std::optional<AbsolutePath> FileUtil::requireAbsolutePath( string_view path )
    {
        // 빈 글도 절대가 아니다 - `acceptsPathInput` 은 빈 글을 통과시키므로(열기 실패로 드러난다) 여기서 따로 거절한다.
        if ( path.empty() || PlatformFileUtil::acceptsPathInput( path ) == false )
        {
            if ( path.empty() )
                SW_LOG_ERROR_ID( "File.RelativePath", "An empty path was given where an absolute disk path is required" );
            return std::nullopt;
        }
        return AbsolutePath::tryMake( path );
    }

    bool FileUtil::makeAbsolutePath( string_view path, string& outResult )
    {
        return PlatformFileSystem::makeAbsolute( path, outResult );
    }

    bool FileUtil::makeCanonicalPath( string_view path, string& outResult )
    {
        return PlatformFileSystem::makeCanonical( path, outResult );
    }

    bool FileUtil::ensureDirectoryExists( string_view directoryPath )
    {
        if ( directoryPath.empty() || isDirectory( directoryPath ) )
            return true;

        const string directory = FileUtilInternal::trimTrailingSeparators( directoryPath );
        if ( FileUtilInternal::createDirectoryTree( directory ) )
            noteFileCreated(); // 폴더를 만들었다(없는 폴더를 기억한 캐시가 버린다)
        // 성공은 "폴더가 있는가" 로만 가른다. 병렬 쿠킹 · 임포트가 같은 폴더를 동시에 만들 때 다른 스레드가 먼저 만든 경쟁은 실패가 아니다.
        // 같은 이름의 파일이 막고 있으면 여전히 실패다.
        if ( isDirectory( directory ) == false )
        {
            SW_LOG_ERROR_ID( "File.CreateDirectoryFailed", "Could not create directory '%#': %#", directoryPath, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    string FileUtil::reserveUniquePath( string_view basePath, string_view extension, bool bDirectory, uint32 maxAttempt, string_view numberSeparator )
    {
        if ( basePath.empty() || ensureParentDirectoryExists( basePath ) == false )
            return {};
        for ( uint32 attempt = 1; attempt <= maxAttempt; ++attempt )
        {
            string path{ basePath };
            if ( attempt > 1 )
            {
                utf8 arrNumber[constant::kMaxBuffer64];
                formatstring( arrNumber, constant::kMaxBuffer64, "%#%#", numberSeparator, attempt );
                path.append( arrNumber );
            }
            path.append( extension );
            if ( isWriteAllowed( path ) == false )
                return {};
            if ( bDirectory )
            {
                // createDirectory 는 이 호출이 만들었을 때만 true 다 - 이미 있으면(다른 프로세스가 먼저 잡았으면) 다음 번호로.
                if ( PlatformFileSystem::createDirectory( path ) )
                {
                    noteFileCreated();
                    return path;
                }
                if ( exists( path ) == false )
                {
                    SW_LOG_ERROR_ID( "File.CreateDirectoryFailed", "Could not create directory '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
                    return {};
                }
                continue;
            }
            // "x"(C11): 이미 있으면 열지 않는다 - 만들기와 있는지 보기가 한 시스템 호출이다.
            FILE* pFile = PlatformFileUtil::openFile( path.c_str(), "wbx" );
            if ( pFile != nullptr )
            {
                std::fclose( pFile );
                noteFileCreated();
                return path;
            }
            if ( exists( path ) == false )
            {
                SW_LOG_ERROR_ID( "File.CreateFailed", "Could not create file '%#'", path );
                return {};
            }
        }
        SW_LOG_ERROR_ID( "File.CreateFailed", "Every name from '%#%#' to suffix %#%# is taken", basePath, extension, numberSeparator, maxAttempt );
        return {};
    }

    bool FileUtil::exists( string_view path )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( path, status );
    }

    bool FileUtil::isDirectory( string_view path )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( path, status ) && status._kind == PlatformFileSystem::FileKind::Directory;
    }

    bool FileUtil::isRegularFile( string_view path )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( path, status ) && status._kind == PlatformFileSystem::FileKind::File;
    }

    bool FileUtil::isReadOnlyFile( string_view fileName )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( fileName, status ) && status._kind == PlatformFileSystem::FileKind::File && status._bReadOnly;
    }

    bool FileUtil::setWritable( string_view path, const bool bWritable )
    {
        if ( PlatformFileSystem::setWritable( path, bWritable ) == false )
        {
            SW_LOG_WARNING( "Could not make '%#' %#: %#", path, bWritable ? "writable" : "read-only", PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    string FileUtil::getCurrentPath()
    {
        string current;
        if ( PlatformFileSystem::getCurrentDirectory( current ) == false )
        {
            SW_LOG_WARNING( "Could not read the current directory: %#", PlatformFileSystem::describeLastError().c_str() );
            return {};
        }
        return current;
    }

    bool FileUtil::setCurrentPath( string_view path )
    {
        if ( PlatformFileSystem::setCurrentDirectory( path ) == false )
        {
            SW_LOG_WARNING( "Could not change the current directory to '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    bool FileUtil::tryGetFileTimestamp( string_view fileName, uint64& outSeconds )
    {
        // 있는지 따로 묻지 않는다 - 묻고 나서 읽는 사이에 지워지면(파일 감시 · 핫 리로드) 어긋난다. 한 번 묻는다.
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( fileName, status ) == false )
            return false;
        outSeconds = static_cast<uint64>( status._writeTicks / kFileTimeTicksPerSecond );
        return true;
    }

    uint64 FileUtil::getCurrentFileTimestamp()
    {
        return static_cast<uint64>( getCurrentFileWriteTime() / kFileTimeTicksPerSecond );
    }

    bool FileUtil::getFileWriteTime( string_view path, int64& outTicks )
    {
        outTicks = 0;
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( path, status ) == false )
            return false;
        outTicks = status._writeTicks;
        return true;
    }

    bool FileUtil::setFileWriteTime( string_view path, const int64 ticks )
    {
        if ( PlatformFileSystem::setWriteTicks( path, ticks ) == false )
        {
            SW_LOG_WARNING( "Could not set the write time of '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    int64 FileUtil::getCurrentFileWriteTime()
    {
        return PlatformFileSystem::getNowTicks();
    }

    bool FileUtil::tryGetFileSize( string_view fileName, uint64& outSize )
    {
        // 크기만 알면 되므로 파일을 열지 않는다.
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( fileName, status ) == false || status._kind != PlatformFileSystem::FileKind::File )
            return false;
        outSize = status._size;
        return true;
    }

    bool FileUtil::copyFile( string_view source, string_view destination )
    {
        if ( isWriteAllowed( destination ) == false )
            return false;
        if ( PlatformFileSystem::copyFileOverwrite( source, destination ) == false )
        {
            SW_LOG_ERROR( "copyFile '%#' -> '%#' failed: %#", source, destination, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        noteFileCreated(); // 복사는 대상 파일을 만든다(없는 파일을 기억한 캐시가 버린다)
        return true;
    }

    bool FileUtil::removeFile( string_view path )
    {
        if ( path.empty() )
            return true;
        if ( exists( path ) && isWriteAllowed( path ) == false ) // 없는 파일을 지우는 정리는 바꾸는 것이 아니다
            return false;
        if ( PlatformFileSystem::removeEntry( path ) )
            noteFileRemoved(); // 지웠다(있는 파일을 기억한 캐시가 버린다)
        if ( exists( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
        return false;
    }

    bool FileUtil::tryRemoveFile( string_view path )
    {
        if ( path.empty() )
            return true;
        if ( exists( path ) && isWriteAllowed( path ) == false )
            return false;
        if ( PlatformFileSystem::removeEntry( path ) )
            noteFileRemoved();
        return exists( path ) == false;
    }

    bool FileUtil::removeDirectory( string_view path )
    {
        if ( path.empty() )
            return true;
        if ( exists( path ) && isWriteAllowed( path ) == false )
            return false;

        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( path, status ) )
        {
            const string directory      = FileUtilInternal::trimTrailingSeparators( path );
            const bool   bRealDirectory = status._kind == PlatformFileSystem::FileKind::Directory && status._bLink == false;
            const size_t removedCount   = bRealDirectory ? FileUtilInternal::removeTree( directory ) : ( PlatformFileSystem::removeEntry( directory ) ? 1 : 0 );
            if ( removedCount > 0 )
                noteFileRemoved(); // 일부만 지웠어도 지운 항목의 옛 경로는 낡았다
        }
        if ( isDirectory( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove directory '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
        return false;
    }

    string FileUtil::getTempDirectory()
    {
        string temp;
        if ( PlatformFileSystem::getTempDirectory( temp ) == false )
            return {};
        return temp;
    }

    bool FileUtil::forEachDirectoryEntry( string_view directory, const bool bRecursive, const DirectoryEntryVisitFn visit, void* pContext )
    {
        if ( visit == nullptr || directory.empty() )
            return false;

        FileUtilInternal::WalkContext walk;
        walk._directory  = FileUtilInternal::trimTrailingSeparators( directory );
        walk._bRecursive = bRecursive;
        walk._visit      = visit;
        walk._pContext   = pContext;
        return PlatformFileSystem::forEachChild( walk._directory, FileUtilInternal::walkChild, &walk );
    }
} // namespace sw
