#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuInstanceRing.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    vector<GpuInstance>& GpuInstanceRing::acquireWrite()
    {
        if ( _pWrite != nullptr )
            return *_pWrite;

        // 아무도 안 읽는 슬롯, 곧 링만 들고 있는 것을 고른다. 발행본과 패킷이 든 슬롯은 use_count 가 2 이상이라 걸러진다.
        for ( uint32 slotIndex = 0; slotIndex < _listSlot.size(); ++slotIndex )
        {
            if ( _listSlot[slotIndex] != nullptr && _listSlot[slotIndex].use_count() == 1 )
            {
                _pWrite         = _listSlot[slotIndex];
                _writeSlotIndex = slotIndex;
                return *_pWrite;
            }
        }
        // 모자라면 하나 더 만든다. 렌더 큐가 깊은 만큼만 자란다(패킷이 슬롯을 놓으면 그 슬롯이 다시 골라진다).
        _listSlot.push_back( make_shared<vector<GpuInstance>>() );
        _listSlotBuild.push_back( 0 );
        _writeSlotIndex = static_cast<uint32>( _listSlot.size() - 1 );
        _pWrite         = _listSlot.back();
        return *_pWrite;
    }

    shared_ptr<const vector<GpuInstance>> GpuInstanceRing::publish( bool bAllDirty, const vector<GpuInstanceRun>& listDirtyRun )
    {
        if ( _pWrite == nullptr )
            return _pPublished;
        ++_publishCounter;
        if ( _writeSlotIndex < _listSlotBuild.size() )
            _listSlotBuild[_writeSlotIndex] = _publishCounter;

        PublishRecord record{};
        record._build = _publishCounter;
        record._bAll  = bAllDirty ? SW_TRUE : SW_FALSE;
        if ( bAllDirty == false )
            record._listRun = listDirtyRun;
        if ( _listHistory.size() >= kPublishHistoryCount )
            _listHistory.erase( _listHistory.begin() );
        _listHistory.push_back( std::move( record ) );

        _pPublished = _pWrite;
        _pWrite.reset();
        return _pPublished;
    }

    void GpuInstanceRing::syncWriteFromPublished()
    {
        if ( _pPublished == nullptr )
            return;
        const vector<GpuInstance>& prev = *_pPublished;
        vector<GpuInstance>&       work = acquireWrite();

        // 슬롯이 발행된 뒤 무엇이 바뀌었나. 이력에서 (슬롯의 발행 번호, 마지막 발행 번호] 를 모은다.
        const uint64 slotBuild = ( _writeSlotIndex < _listSlotBuild.size() ) ? _listSlotBuild[_writeSlotIndex] : 0;
        bool         bWhole    = ( slotBuild == 0 ) || ( work.size() != prev.size() );
        uint64       expected  = slotBuild + 1;
        if ( bWhole == false )
        {
            for ( const PublishRecord& record : _listHistory )
            {
                if ( record._build <= slotBuild )
                    continue;
                // 이력이 끊겼으면(중간 발행이 밀려났으면) 통째로.
                if ( record._build != expected || record._bAll != SW_FALSE )
                {
                    bWhole = true;
                    break;
                }
                ++expected;
            }
            if ( expected != _publishCounter + 1 )
                bWhole = true;
        }

        work.resize( prev.size() );
        if ( bWhole )
        {
            if ( prev.empty() == false )
                Memory::copy( work.data(), prev.data(), prev.size() * sizeof( GpuInstance ) );
            return;
        }
        for ( const PublishRecord& record : _listHistory )
        {
            if ( record._build <= slotBuild )
                continue;
            for ( const GpuInstanceRun& run : record._listRun )
            {
                const size_t start = MathUtil::min<size_t>( run._start, prev.size() );
                const size_t end   = MathUtil::min<size_t>( static_cast<size_t>( run._start ) + run._count, prev.size() );
                if ( end > start )
                    Memory::copy( work.data() + start, prev.data() + start, ( end - start ) * sizeof( GpuInstance ) );
            }
        }
    }

    void GpuInstanceRing::clear()
    {
        _listSlot.clear();
        _listSlotBuild.clear();
        _pWrite.reset();
        _writeSlotIndex = 0;
        _pPublished.reset();
        _listHistory.clear();
        _publishCounter = 0;
    }
} // namespace sw
