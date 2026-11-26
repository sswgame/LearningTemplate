/**
 * @file ValidationIssueLog.cpp
 * @brief 검증 결과 표(`ValidationIssueLog`)와 결과의 안전한 고침(`ValidationContext::addErrorWithFix` · `addWarningWithFix`)입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 * @details 고침은 맵 검사 패널이 적용하는 데이터라 배포에서 쓰는 곳이 없다. 배포의 타입 검증(읽기 검사)은 `addError` · `addWarning` 만 쓴다.
 */
#include "pch.h"

#include "Engine/Reflection/Dev/ValidationIssueLog.h"

#include "Core/Concurrency/ScopedLock.h"

namespace sw
{
    void ValidationContext::addErrorWithFix( const string_view message, const ValidationPropertyEdit& fix )
    {
        add( ValidationSeverity::Error, message, fix );
    }

    void ValidationContext::addWarningWithFix( const string_view message, const ValidationPropertyEdit& fix )
    {
        add( ValidationSeverity::Warning, message, fix );
    }

    void ValidationIssueLog::replaceIssues( const uint64 sourceID, const vector<ValidationIssue>& listIssue )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _listIssue.empty() && listIssue.empty() )
            return;
        // 제자리에서 지운다. 씬 언로드는 오브젝트마다 여기를 지나므로 새 목록을 만들지 않는다.
        const size_t sizeBefore = _listIssue.size();
        size_t       keptCount  = 0;
        for ( size_t issueIndex = 0; issueIndex < sizeBefore; ++issueIndex )
        {
            if ( _listIssue[issueIndex]._sourceID == sourceID )
                continue;
            if ( keptCount != issueIndex )
                _listIssue[keptCount] = std::move( _listIssue[issueIndex] );
            ++keptCount;
        }
        _listIssue.resize( keptCount );
        const bool bRemovedAny = keptCount != sizeBefore;
        for ( const ValidationIssue& issue : listIssue )
        {
            _listIssue.push_back( issue );
            _listIssue.back()._sourceID = sourceID;
        }
        if ( bRemovedAny || listIssue.empty() == false )
            _revision.fetch_add( 1, std::memory_order_acq_rel );
    }

    void ValidationIssueLog::removeSource( const uint64 sourceID )
    {
        replaceIssues( sourceID, {} );
    }

    void ValidationIssueLog::collectIssues( vector<ValidationIssue>& outListIssue ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outListIssue = _listIssue;
    }

    uint32 ValidationIssueLog::getIssueCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<uint32>( _listIssue.size() );
    }

    void ValidationIssueLog::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _listIssue.empty() == false )
            _revision.fetch_add( 1, std::memory_order_acq_rel );
        _listIssue.clear();
    }
} // namespace sw
