#include "pch.h"

#include "Engine/Resource/Cook/CookJob.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Resource/Cook/ContentHash.h"

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

    bool CookJob::admitRead( string_view resourceID, const uint8* pData, size_t size )
    {
        CookJob* pJob = CookJobInternal::_t_pCurrent;
        if ( pJob != nullptr )
        {
            pJob->recordInput( resourceID, ContentHash::computeBytes( pData, size ) );
            return true;
        }
        if ( CookSession::isActive() == false )
            return true;
        SW_LOG_ERROR( "'%#' was read during cooking outside a cook job - the read is not recorded as an input, so it fails (open a CookJob around the cooker)",
                      resourceID );
        return false;
    }

    bool CookJob::admitAsyncRead( string_view resourceID )
    {
        if ( CookJobInternal::_t_pCurrent == nullptr && CookSession::isActive() == false )
            return true;
        SW_LOG_ERROR( "'%#' was read asynchronously during cooking - its completion is not recorded as an input; cookers read synchronously", resourceID );
        return false;
    }

    void CookJob::recordInput( string_view resourceID, uint64 contentHash )
    {
        for ( CookJob* pJob = this; pJob != nullptr; pJob = pJob->_pOuter )
        {
            const bool bKnown = std::any_of( pJob->_listInput.begin(), pJob->_listInput.end(), [resourceID]( const CookInput& input )
            { return input._resourceID == resourceID; } );
            if ( bKnown == false )
                pJob->_listInput.push_back( CookInput{ string( resourceID ), contentHash } );
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
        { return pLeft->_resourceID < pRight->_resourceID; } );

        // 사람이 읽을 수 있는 정본 글을 만들어 해시한다 — 칸 사이 구분자가 있어 이름 둘이 붙어 다른 이름이 되지 않는다.
        string canonical;
        canonical.append( "cooker=" ).append( desc._cookerName ).append( "\nversion=" ).append( sw::to_string( desc._cookerVersion ) );
        canonical.append( "\ntarget=" ).append( desc._target ).append( "\nsettings=" ).append( sw::to_string( desc._settingsHash ) ).append( "\n" );
        for ( const CookInput* pInput : listSorted )
        {
            canonical.append( pInput->_resourceID ).append( "\t" ).append( sw::to_string( pInput->_contentHash ) ).append( "\n" );
        }
        return StringUtil::computeHash64( canonical.c_str(), canonical.size(), false );
    }
} // namespace sw
