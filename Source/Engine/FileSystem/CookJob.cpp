#include "pch.h"

#include "Engine/FileSystem/CookJob.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/FileSystem/ContentHash.h"

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

            /** @brief 입력의 id 입니다. 리소스 루트 안이면 리소스 id, 밖이면 받은 경로(구분자만 맞춘다)입니다. */
            static string makeInputID( string_view path )
            {
                string id = AssetPaths::toAssetID( path );
                return id.empty() ? FileUtil::normalizeSeparators( path ) : id;
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
        const string absPath = AssetPaths::findLooseFile( _assetID ).getDiskPath(); // 읽기다 — 없는 입력은 빈 경로(해시 0)
        if ( _kind == CookInputKind::Listing )
            return CookJob::computeListingHash( absPath, _extension );
        return absPath.empty() ? 0 : ContentHash::computeFile( absPath );
    }

    bool CookJob::admitRead( string_view assetID, const uint8* pData, size_t size )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob != nullptr )
        {
            pJob->recordInput( assetID, ContentHash::computeBytes( pData, size ) );
            return true;
        }
        if ( CookSession::isActive() == false )
            return true;
        SW_LOG_ERROR( "'%#' was read during cooking outside a cook job - the read is not recorded as an input, so it fails (open a CookJob around the cooker)",
                      assetID );
        return false;
    }

    bool CookJob::requiresCallingThreadRead()
    {
        return CookJobInternal::_t_pCurrent != nullptr || CookSession::isActive();
    }

    bool CookJob::admitAsyncRead( string_view assetID )
    {
        if ( requiresCallingThreadRead() == false )
            return true;
        SW_LOG_ERROR( "'%#' was read asynchronously during cooking - its completion is not recorded as an input; cookers read synchronously", assetID );
        return false;
    }

    void CookJob::admitMissingRead( string_view assetID )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob != nullptr )
            pJob->recordInput( assetID, 0 );
    }

    bool CookJob::collectFiles( string_view folderPath, string_view extension, vector<string>& outListFile )
    {
        outListFile.clear();
        // 폴더가 없으면 빈 목록이 맞다(목록 해시 0 으로 기록된다). 도중에 멈춘 부분 목록은 구울 것을 빠뜨리므로 실패다(`FileUtil::collectFiles` 가 경로를 알린다).
        if ( FileUtil::isDirectory( folderPath ) && FileUtil::collectFiles( folderPath, extension, outListFile, true ) == false )
            return false;
        std::sort( outListFile.begin(), outListFile.end() );
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob == nullptr )
            return true;
        CookInput input;
        input._kind = CookInputKind::Listing;
        // 폴더는 받은 경로 그대로 둔다 — 도메인 폴더(`engine`)는 리소스 id 로 줄이면 다시 풀 때 같은 폴더로 돌아오지 않는다.
        input._assetID     = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( folderPath ) );
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
        std::sort( listRelative.begin(), listRelative.end() );
        string canonical;
        for ( const string& relative : listRelative )
        {
            canonical.append( relative ).append( "\n" );
        }
        // 빈 목록도 "폴더가 있다" 이므로 0(폴더 없음)과 다르다.
        canonical.append( "listing" );
        return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
    }

    void CookJob::recordInput( string_view assetID, uint64 contentHash )
    {
        CookInput input;
        input._assetID     = CookJobInternal::makeInputID( assetID );
        input._contentHash = contentHash;
        recordInput( input );
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
        std::sort( listSorted.begin(), listSorted.end(), []( const CookInput* pLeft, const CookInput* pRight )
        {
            if ( pLeft->_kind != pRight->_kind )
                return pLeft->_kind < pRight->_kind;
            if ( pLeft->_assetID != pRight->_assetID )
                return pLeft->_assetID < pRight->_assetID;
            return pLeft->_extension < pRight->_extension;
        } );

        // 사람이 읽을 수 있는 기준 글을 만들어 해시한다 — 칸 사이 구분자가 있어 이름 둘이 붙어 다른 이름이 되지 않는다.
        string canonical;
        canonical.append( "cooker=" ).append( desc._cookerName ).append( "\nversion=" ).append( sw::to_string( desc._cookerVersion ) );
        canonical.append( "\ntarget=" ).append( desc._target ).append( "\nsettings=" ).append( sw::to_string( desc._settingsHash ) ).append( "\n" );
        for ( const CookInput* pInput : listSorted )
        {
            canonical.append( pInput->_kind == CookInputKind::Listing ? "listing\t" : "file\t" ).append( pInput->_assetID ).append( "\t" );
            canonical.append( pInput->_extension ).append( "\t" ).append( sw::to_string( pInput->_contentHash ) ).append( "\n" );
        }
        return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
    }
} // namespace sw
