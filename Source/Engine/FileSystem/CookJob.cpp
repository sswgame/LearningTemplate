#include "pch.h"

#include "Engine/FileSystem/CookJob.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/FileSystem/ContentHash.h"
#include "Engine/Foundation/EngineServices.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "CookJob" );

    namespace
    {
        struct CookJobInternal
        {
            inline static thread_local CookJob* _t_pCurrent{ nullptr };
            inline static atomic<int32>         _s_sessionCount{ 0 };

            /** @brief 리소스 읽기 입력의 id 입니다(`normalizeAssetID` 로 맞춘다). 루트 밖으로 나가는 id(`..`)는 입력이 될 수 없어 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool makeInputID( const AssetID& id, string& outID )
            {
                outID = AssetPaths::normalizeAssetID( id.view() );
                if ( outID.empty() == false )
                    return true;
                SW_LOG_ERROR( "Cook input '%#' leaves the resource tree - it cannot be recorded, so the read fails", id.view() );
                return false;
            }

            /** @brief 디스크 경로의 절대 경로(구분자 `/`)입니다. 만들지 못하면 오류를 남기고 false 입니다(상대 글을 기록하면 리소스 id 로 다시 풀린다 — 파일 머리말). */
            [[nodiscard]] static bool makeAbsoluteDiskPath( string_view diskPath, string& outAbsPath )
            {
                if ( FileUtil::makeAbsolutePath( diskPath, outAbsPath ) == false )
                {
                    SW_LOG_ERROR( "Cook input '%#' cannot be made absolute - it is not recorded, so the read fails", diskPath );
                    return false;
                }
                outAbsPath = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( outAbsPath ) );
                return true;
            }

            /** @brief 디스크 읽기 입력의 종류와 출처입니다. 붙인 도메인 · 리소스 루트 아래면 리소스(id), 밖이면 디스크 파일(절대 경로)입니다. */
            [[nodiscard]] static bool makeDiskInput( string_view diskPath, CookInputKind& outKind, string& outSource )
            {
                string absPath;
                if ( makeAbsoluteDiskPath( diskPath, absPath ) == false )
                    return false;
                outSource = engine::getSubsystem<AssetPaths>().toAssetID( absPath );
                outKind   = CookInputKind::Resource;
                if ( outSource.empty() )
                {
                    outSource = std::move( absPath );
                    outKind   = CookInputKind::DiskFile;
                }
                return true;
            }

            /** @brief 작업에 내용 입력(리소스 · 디스크 파일) 하나를 기록합니다. */
            static void recordContentInput( CookJob& job, CookInputKind kind, string source, uint64 contentHash )
            {
                CookInput input;
                input._kind        = kind;
                input._source      = std::move( source );
                input._contentHash = contentHash;
                job.recordInput( input );
            }

            /** @brief 작업이 없을 때의 읽기 판정입니다(`CookJob::admitRead` 참고). */
            [[nodiscard]] static bool admitReadOutsideJob( string_view source )
            {
                if ( CookSession::isActive() == false )
                    return true;
                SW_LOG_ERROR( "'%#' was read during cooking outside a cook job - the read is not recorded as an input, so it fails (open a CookJob around the cooker)",
                              source );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CookSession::CookSession()
    {
        CookJobInternal::_s_sessionCount.fetch_add( 1, std::memory_order_acq_rel );
    }

    CookSession::~CookSession()
    {
        CookJobInternal::_s_sessionCount.fetch_sub( 1, std::memory_order_acq_rel );
    }

    bool CookSession::isActive()
    {
        return CookJobInternal::_s_sessionCount.load( std::memory_order_acquire ) > 0;
    }

    CookJob::CookJob( const CookJobDesc& desc )
        : _desc{ desc }
        , _listInput{}
        , _pOuter{ CookJobInternal::_t_pCurrent }
    {
        CookJobInternal::_t_pCurrent = this;
    }

    CookJob::~CookJob()
    {
        CookJobInternal::_t_pCurrent = _pOuter;
    }

    CookJob* CookJob::findCurrent()
    {
        return CookJobInternal::_t_pCurrent;
    }

    uint64 CookInput::computeCurrentHash() const
    {
        switch ( _kind )
        {
            case CookInputKind::Resource:
            {
                const std::optional<AbsolutePath> absPath = engine::getSubsystem<AssetPaths>().findLooseFile( _source ); // 읽기다 — 없는 입력은 해시 0
                return absPath.has_value() ? ContentHash::computeFile( absPath->str() ) : 0;
            }
            case CookInputKind::DiskFile:
                return FileUtil::isRegularFile( _source ) ? ContentHash::computeFile( _source ) : 0;
            case CookInputKind::Listing:
                return CookJob::computeListingHash( _source, _extension );
        }
        return 0;
    }

    bool CookJob::admitRead( const AssetID& id, const uint8* pData, size_t size )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob == nullptr )
            return CookJobInternal::admitReadOutsideJob( id.view() );
        string inputID;
        if ( CookJobInternal::makeInputID( id, inputID ) == false )
            return false;
        CookJobInternal::recordContentInput( *pJob, CookInputKind::Resource, std::move( inputID ), ContentHash::computeBytes( pData, size ) );
        return true;
    }

    bool CookJob::admitDiskRead( string_view diskPath, const uint8* pData, size_t size )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob == nullptr )
            return CookJobInternal::admitReadOutsideJob( diskPath );
        return pJob->recordDiskInput( diskPath, ContentHash::computeBytes( pData, size ) );
    }

    bool CookJob::requiresCallingThreadRead()
    {
        return CookJobInternal::_t_pCurrent != nullptr || CookSession::isActive();
    }

    bool CookJob::admitAsyncRead( const AssetID& id )
    {
        if ( requiresCallingThreadRead() == false )
            return true;
        SW_LOG_ERROR( "'%#' was read asynchronously during cooking - its completion is not recorded as an input; cookers read synchronously", id.view() );
        return false;
    }

    void CookJob::admitMissingRead( const AssetID& id )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        string   inputID;
        if ( pJob != nullptr && CookJobInternal::makeInputID( id, inputID ) ) // 루트 밖 id 는 오류 로그로 남는다 — 읽기는 이미 실패다
            CookJobInternal::recordContentInput( *pJob, CookInputKind::Resource, std::move( inputID ), 0 );
    }

    void CookJob::admitMissingDiskRead( string_view diskPath )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob != nullptr )
            (void)pJob->recordDiskInput( diskPath, 0 ); // 실패는 오류 로그로 남는다 — 읽기는 이미 실패라 돌려줄 것이 없다
    }

    bool CookJob::collectFiles( string_view folderPath, string_view extension, vector<string>& outListFile )
    {
        outListFile.clear();
        // 폴더가 없으면 빈 목록이 맞다(목록 해시 0 으로 기록된다). 도중에 멈춘 부분 목록은 구울 것을 빠뜨리므로 실패다(`FileUtil::collectFiles` 가 경로를 알린다).
        if ( FileUtil::isDirectory( folderPath ) && FileUtil::collectFiles( folderPath, extension, outListFile, true ) == false )
            return false;
        VectorUtil::sort( outListFile );
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob == nullptr )
            return true;
        CookInput input;
        input._kind = CookInputKind::Listing;
        // 폴더는 절대 경로로 둔다 — 도메인 폴더(`engine`)는 리소스 id 로 줄이면 다시 풀 때 같은 폴더로 돌아오지 않는다.
        if ( CookJobInternal::makeAbsoluteDiskPath( folderPath, input._source ) == false )
            return false;
        input._extension   = string( extension );
        input._contentHash = computeListingHash( folderPath, extension );
        pJob->recordInput( input );
        return true;
    }

    uint64 CookJob::computeListingHash( string_view folderPath, string_view extension )
    {
        if ( FileUtil::isDirectory( folderPath ) == false )
            return 0;
        vector<string> listFile;
        // 도중에 멈춘 목록의 해시는 믿을 수 없다 — "폴더 없음"(0)으로 돌려 기록과 어긋나게 해 다음 검사가 다시 굽게 한다(경로는 `FileUtil::collectFiles` 가 알린다).
        if ( FileUtil::collectFiles( folderPath, extension, listFile, true ) == false )
            return 0;
        const string   root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( folderPath ) );
        vector<string> listRelative;
        listRelative.reserve( listFile.size() );
        for ( const string& filePath : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( filePath );
            listRelative.push_back( normalized.size() > root.size() ? string( normalized.substr( root.size() + 1 ) ) : normalized );
        }
        VectorUtil::sort( listRelative );
        string canonical;
        for ( const string& relative : listRelative )
        {
            canonical.append( relative ).append( "\n" );
        }
        // 빈 목록도 "폴더가 있다" 이므로 0(폴더 없음)과 다르다.
        canonical.append( "listing" );
        return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
    }

    bool CookJob::recordDiskInput( string_view diskPath, uint64 contentHash )
    {
        CookInputKind kind = CookInputKind::DiskFile;
        string        source;
        if ( CookJobInternal::makeDiskInput( diskPath, kind, source ) == false )
            return false;
        CookJobInternal::recordContentInput( *this, kind, std::move( source ), contentHash );
        return true;
    }

    void CookJob::recordInput( const CookInput& input )
    {
        for ( CookJob* pJob = this; pJob != nullptr; pJob = pJob->_pOuter )
        {
            if ( pJob->_uniqueSetKnownSource.insert( input.makeSourceKey() ).second )
                pJob->_listInput.push_back( input );
        }
    }

    uint64 CookJob::computeKey( const CookJobDesc& desc, const vector<CookInput>& listInput )
    {
        vector<const CookInput*> listSorted;
        listSorted.reserve( listInput.size() );
        for ( const CookInput& input : listInput )
        {
            listSorted.push_back( &input );
        }
        VectorUtil::sort( listSorted, []( const CookInput* pLeft, const CookInput* pRight )
        {
            if ( pLeft->_kind != pRight->_kind )
                return pLeft->_kind < pRight->_kind;
            if ( pLeft->_source != pRight->_source )
                return pLeft->_source < pRight->_source;
            return pLeft->_extension < pRight->_extension;
        } );

        // 사람이 읽을 수 있는 기준 글을 만들어 해시한다 — 칸 사이 구분자가 있어 이름 둘이 붙어 다른 이름이 되지 않는다.
        string canonical;
        canonical.append( "cooker=" ).append( desc._cookerName ).append( "\nversion=" ).append( sw::to_string( desc._cookerVersion ) );
        canonical.append( "\ntarget=" ).append( desc._target ).append( "\nsettings=" ).append( sw::to_string( desc._settingsHash ) ).append( "\n" );
        for ( const CookInput* pInput : listSorted )
        {
            canonical.append( CookInput::getKindName( pInput->_kind ) ).append( "\t" ).append( pInput->_source ).append( "\t" );
            canonical.append( pInput->_extension ).append( "\t" ).append( sw::to_string( pInput->_contentHash ) ).append( "\n" );
        }
        return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
    }
} // namespace sw
