/**
 * @file RHIRetireList.h
 * @brief GPU 자원 지연 해제(프레임 은퇴)의 목록입니다. 항목을 은퇴 키(GPU 펜스 값 · 렌더 스레드 프레임 수 · draw 스냅샷 번호)와 함께 들고 있다가, 기준값이 그 키에 닿으면 넣은 순서대로 내줍니다.
 * @details 일반 컨테이너(Core/Container)가 아니라 기능(RHI 지연 해제) 쪽에 둔다 — 언리얼 RHI 의 지연 삭제(펜스 · 프레임이 지난 뒤 자원을 놓는 줄)와
 *          Godot `RenderingDevice` 의 프레임별 dispose 목록이 렌더 하드웨어 층에 있는 것과 같은 자리다. 쓰는 곳은 모두 "기준값에 닿으면 놓는다" 는 은퇴 규칙이다:
 *          `RHIReleaseQueue`(GPU 펜스), `IRHIDevice` 의 미룬 핸들(렌더 스레드가 끝낸 프레임 수), 에디터 draw 해제 큐(UI 스냅샷 번호).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class RHIRetireList
     * @brief 은퇴 목록입니다. "키 <= 기준값" 인 항목을 넣은 순서를 지켜 꺼냅니다.
     * @details 잠금은 갖지 않는다. 쓰는 쪽이 자기 잠금(스핀락 · 뮤텍스) 아래에서 부르고, 꺼낸 항목의 실행(해제 콜백 · 디바이스로 넘기기)은 잠금 밖에서 한다.
     *          잠금 안에서 할당하지 않으려면 `extractReadyReserving` 을 쓴다. 키의 뜻(어느 프레임 · 펜스 · 스냅샷부터 안전한가)은 쓰는 쪽이 정한다.
     * @code
     *     _list.push( releaseDelegate, currentFrame + latency );
     *     _list.extractReady( currentFrame, listReady ); // 잠금 아래
     * @endcode
     */
    template <typename TItem>
    class RHIRetireList
    {
    public:
        /** @brief @p item 을 키 @p readyKey 와 함께 넣습니다. 기준값이 @p readyKey 이상이 되면 꺼낼 수 있습니다. */
        void push( const TItem& item, uint64 readyKey ) { _listEntry.push_back( Entry{ item, readyKey } ); }

        /** @brief 기준값 @p threshold 에서 꺼낼 수 있는 항목 수입니다. */
        size_t countReady( uint64 threshold ) const
        {
            return static_cast<size_t>( std::count_if( _listEntry.begin(), _listEntry.end(), [threshold]( const Entry& entry )
            { return entry._readyKey <= threshold; } ) );
        }

        /** @brief 키가 @p threshold 이하인 항목을 넣은 순서대로 @p outListItem 뒤에 붙이고 목록에서 뺍니다. 남는 항목의 순서도 그대로입니다. */
        void extractReady( uint64 threshold, vector<TItem>& outListItem )
        {
            const typename vector<Entry>::iterator partitionIter =
                std::stable_partition( _listEntry.begin(), _listEntry.end(), [threshold]( const Entry& entry )
            { return entry._readyKey > threshold; } );
            for ( typename vector<Entry>::iterator iter = partitionIter; iter != _listEntry.end(); ++iter )
            {
                outListItem.push_back( iter->_item );
            }
            _listEntry.erase( partitionIter, _listEntry.end() );
        }

        /**
         * @brief `extractReady` 를 잠금 아래에서 할당하지 않고 합니다.
         * @details 꺼낼 개수를 먼저 세고, @p outListItem 의 용량이 모자라면 잠금을 놓고 늘린 뒤 다시 센다(그사이 항목이 늘 수 있다).
         * @param lock 이 목록을 지키는 잠금입니다. 이미 잠겨 있어야 하며, 돌아올 때도 잠겨 있습니다.
         */
        template <typename TLock>
        void extractReadyReserving( UniqueLock<TLock>& lock, uint64 threshold, vector<TItem>& outListItem ) SW_REQUIRES_LOCK( lock )
        {
            for ( ;; )
            {
                const size_t neededCapacity = outListItem.size() + countReady( threshold );
                if ( neededCapacity <= outListItem.capacity() )
                    break;
                lock.unlock();
                outListItem.reserve( neededCapacity );
                lock.lock();
            }
            extractReady( threshold, outListItem );
        }

        /** @brief 키와 상관없이 모든 항목을 넣은 순서대로 @p outListItem 뒤에 붙이고 비웁니다(종료 · GPU 유휴 뒤 남은 해제를 모두 실행할 때). */
        void extractAll( vector<TItem>& outListItem )
        {
            for ( const Entry& entry : _listEntry )
            {
                outListItem.push_back( entry._item );
            }
            _listEntry.clear();
        }

        /** @brief 두 목록을 맞바꿉니다(할당 없음). 잠금 아래에서 통째로 넘겨받고, 실행은 잠금 밖에서 할 때 씁니다. */
        void swap( RHIRetireList& other ) noexcept { _listEntry.swap( other._listEntry ); }

        /** @brief 아직 꺼내지 않은 항목 수입니다. */
        size_t size() const { return _listEntry.size(); }
        /** @brief 항목이 없는지 봅니다. */
        bool isEmpty() const { return _listEntry.empty(); }

    private:
        struct Entry
        {
            TItem  _item;
            uint64 _readyKey{ 0 };
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
