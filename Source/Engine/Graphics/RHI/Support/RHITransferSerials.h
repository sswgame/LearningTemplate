/**
 * @file RHITransferSerials.h
 * @brief 전송 티켓의 시리얼 발급, 완료 워터마크, 티켓 판정, 읽기 결과 보관을 네 백엔드가 같이 씁니다(docs/plans/RhiAsyncUploads.md 2.2).
 * @details 백엔드마다 다른 것은 "GPU 가 끝냈는지 어떻게 아는가"(DX11 이벤트 쿼리, GL 펜스, DX12 프레임 펜스, Vulkan 큐 대기)뿐이고, 시리얼을 매기는 규칙과
 *          `Complete` 의 뜻(연속으로 끝난 시리얼의 끝 이하)은 여기 한 곳이 정한다. 그래서 백엔드가 서로 다른 완료 의미를 갖는 일이 생기지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class RHITransferSerials
     * @brief 한 장치의 전송 시리얼을 발급하고 완료 워터마크를 쥡니다.
     * @details **시리얼은 큐 제출 순서대로 매겨야 한다.** 그래서 `issue` 는 백엔드가 자기 큐 제출 락을 쥔 채, 제출을 낸 직후에 부른다(같은 락 안). 락 밖에서 발급하면 두
     *          스레드의 시리얼 순서가 큐 실행 순서와 어긋나고, 워터마크가 아직 끝나지 않은 제출을 덮는다.
     *          워터마크는 연속으로 끝난 시리얼의 끝이다. `advanceWatermark( n )` 은 1 ~ n 이 모두 끝났을 때만 부른다.
     */
    class SW_API RHITransferSerials
    {
    public:
        /** @brief 장치 세대를 새로 받습니다(`allocateRHIDeviceGeneration`). */
        RHITransferSerials();

        RHITransferSerials( const RHITransferSerials& )            = delete;
        RHITransferSerials& operator=( const RHITransferSerials& ) = delete;

        /** @brief 이 장치가 발급하는 티켓에 싣는 세대입니다. */
        [[nodiscard]] uint32 getGeneration() const { return _generation; }

        /** @brief 다음 시리얼을 발급합니다. 백엔드의 큐 제출 락 안에서, 제출을 낸 직후에 부릅니다. */
        [[nodiscard]] uint64 issue();

        /**
         * @brief 이미 GPU 에서 끝난 제출의 시리얼을 발급하고 곧바로 워터마크를 올립니다. 제출 안에서 완료를 기다리는 임시 구현(Vulkan B0)이 씁니다.
         * @details 발급과 워터마크를 한 락 안에서 하므로 발급된 시리얼은 모두 끝난 상태다 — `Complete` 가 앞선 시리얼도 끝났다는 뜻이어야 하는 규칙이 선다.
         *          `issue` 와 한 백엔드에서 섞어 쓰지 않는다(섞으면 끝나지 않은 앞선 시리얼을 워터마크가 덮는다).
         */
        [[nodiscard]] uint64 issueCompleted();

        /** @brief 발급한 시리얼과 세대로 `Accepted` 결과를 만듭니다. */
        [[nodiscard]] RHITransferSubmit makeAccepted( uint64 serial ) const { return RHITransferSubmit::accepted( serial, _generation ); }

        /** @brief 1 ~ @p serial 이 모두 끝났음을 알립니다. 워터마크는 올라가기만 합니다. */
        void advanceWatermark( uint64 serial );

        /** @brief 연속으로 끝난 시리얼의 끝입니다. 어느 스레드에서나 읽습니다. */
        [[nodiscard]] uint64 getWatermark() const { return _watermark.load( std::memory_order_acquire ); }

        /** @brief 가장 최근에 발급한 시리얼입니다. */
        [[nodiscard]] uint64 getLastIssued() const { return _lastIssued.load( std::memory_order_acquire ); }

        /** @brief 시리얼이 워터마크 이하이면 true 입니다. */
        [[nodiscard]] bool isComplete( uint64 serial ) const { return serial != 0 && serial <= getWatermark(); }

        /**
         * @brief 장치 손실을 알립니다. 이후 끝나지 않은 티켓은 `Failed` 입니다. 이미 `Complete` 인 티켓은 그대로 `Complete` 입니다.
         * @details 장치를 잃었다는 사실의 저장소는 여기 하나다 — 백엔드는 자기 손실 플래그를 따로 두지 않고(두면 한쪽만 표시해 제출 진입의 `isLost` 가 놓친다) 이 값을 읽는다.
         * @return 이번 호출이 처음 표시했으면 true, 이미 잃은 상태였으면 false 입니다(처음 본 쪽만 로그를 남길 때 씁니다).
         */
        bool markLost();

        /**
         * @brief 티켓을 판정합니다. 무효 티켓, 다른 세대의 티켓, 발급한 적 없는 시리얼은 오류를 남기고 `Failed` 입니다.
         * @details 워터마크 이하면 `Complete`, 장치를 잃었으면 `Failed`, 아니면 `Pending` 입니다. 백엔드는 이 값이 `Pending` 일 때만 자기 완료 확인을 진행합니다.
         */
        [[nodiscard]] RHITransferStatus classify( RHITransferTicket ticket ) const;

        /**
         * @brief bindless 등록 보호입니다. 텍스처 레코드의 마지막 전송 시리얼이 아직 끝나지 않았으면 오류를 남기고 false 입니다.
         * @param lastTransferSerial 레코드가 들고 있는 마지막 쓰기 전송(업로드)의 시리얼입니다. 전송한 적이 없으면 0 이고 그때는 늘 true 입니다.
         * @details 백엔드의 `registerBindlessTexture` 가 부르고, false 면 `kInvalidDescriptorIndex` 를 돌려준다. "`Complete` 를 본 뒤에 등록한다" 는 소비자 규칙을 주석이 아니라
         *          코드로 막는 자리다. 워터마크는 `pollTransfer` 가 `Complete` 를 돌려줄 때 이미 올라가 있으므로, 규칙을 지킨 소비자는 늘 통과한다.
         */
        [[nodiscard]] bool isTextureReadyToRegister( uint64 lastTransferSerial, RHITextureHandle texture ) const;

    private:
        atomic<uint64> _lastIssued;
        atomic<uint64> _watermark;
        uint32         _generation;
        atomic<uint8>  _bLost;
        mutex          _issueCompletedMutex; ///< `issueCompleted` 의 발급과 워터마크 올리기를 한 덩어리로 묶는다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class RHITransferFenceQueue
     * @brief 제출 순서대로 쌓은 "완료 확인 대상"(Probe)을 앞에서부터 확인해 워터마크를 올립니다.
     * @tparam Probe 완료를 확인하는 값입니다(DX11 의 이벤트 쿼리, GL 의 `GLsync`). 이미 끝난 제출은 `isDone` 이 바로 참을 주는 Probe 로 넣습니다.
     * @details 같은 큐에서 제출한 일은 제출 순서대로 끝나므로(DX11 즉시 컨텍스트, GL 컨텍스트) 앞에서부터 확인해 연속으로 끝난 것까지 워터마크를 올린다.
     *          자기 뮤텍스는 잎(leaf)이다 — `enqueue` 는 백엔드 제출 락 안에서, `retireDone` 은 `isDone` 이 필요로 하는 백엔드 락(DX11 즉시 컨텍스트 락)을 이미 쥔 채로 부른다.
     */
    template <typename Probe>
    class RHITransferFenceQueue
    {
    public:
        explicit RHITransferFenceQueue( RHITransferSerials& serials )
            : _serials{ serials }
        {
        }

        RHITransferFenceQueue( const RHITransferFenceQueue& )            = delete;
        RHITransferFenceQueue& operator=( const RHITransferFenceQueue& ) = delete;

        /** @brief 시리얼을 발급하고 Probe 를 줄 끝에 넣습니다. 백엔드의 큐 제출 락 안에서, 제출을 낸 직후에 부릅니다. */
        [[nodiscard]] uint64 enqueue( Probe probe )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const uint64            serial = _serials.issue();
            _listEntry.push_back( Entry{ serial, std::move( probe ) } );
            return serial;
        }

        /**
         * @brief 앞에서부터 `isDone( probe )` 가 참인 동안 `retire( probe )` 로 놓고 줄에서 빼며, 마지막으로 뺀 시리얼까지 워터마크를 올립니다. 기다리지 않습니다.
         * @return 새로 끝난 제출이 있으면 true 입니다.
         */
        template <typename IsDoneFn, typename RetireFn>
        bool retireDone( IsDoneFn&& isDone, RetireFn&& retire )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            uint64                  doneSerial{ 0 };
            while ( _listEntry.empty() == false && isDone( _listEntry.front()._probe ) )
            {
                doneSerial = _listEntry.front()._serial;
                retire( _listEntry.front()._probe );
                _listEntry.pop_front();
            }
            if ( doneSerial != 0 )
                _serials.advanceWatermark( doneSerial );
            return doneSerial != 0;
        }

        /** @brief 장치가 쉬었다(`waitIdle`). 남은 Probe 를 모두 `retire` 로 놓고 마지막 시리얼까지 워터마크를 올립니다. */
        template <typename RetireFn>
        void retireAll( RetireFn&& retire )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            uint64                  doneSerial{ 0 };
            while ( _listEntry.empty() == false )
            {
                doneSerial = _listEntry.front()._serial;
                retire( _listEntry.front()._probe );
                _listEntry.pop_front();
            }
            if ( doneSerial != 0 )
                _serials.advanceWatermark( doneSerial );
        }

        /** @brief 아직 끝나지 않은 제출 수입니다. */
        [[nodiscard]] uint32 getPendingCount() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return static_cast<uint32>( _listEntry.size() );
        }

    private:
        struct Entry
        {
            uint64 _serial;
            Probe  _probe;
        };

        RHITransferSerials& _serials;
        mutable mutex       _mutex;
        deque<Entry>        _listEntry;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RHIPendingReadbackTable
     * @brief 읽기 티켓의 결과(또는 결과를 담은 GPU 스테이징)를 시리얼로 보관합니다. 꺼내면 지워지므로 결과는 한 번만 나갑니다.
     * @tparam Payload 백엔드가 읽기 하나에 대해 들고 있는 것입니다(DX11 스테이징 텍스처, 읽어 둔 바이트 등).
     * @details `takeTextureReadback` 은 `take` 로 꺼내 쓰고, `discardTextureReadback` 은 `take` 로 꺼낸 것을 백엔드의 방식으로 놓는다(진행 중인 복사가 쓰는 스테이징은 GPU 가
     *          끝난 뒤에 해제해야 한다). 업로드 티켓은 여기에 없으므로 `take` 가 false 를 준다.
     */
    template <typename Payload>
    class RHIPendingReadbackTable
    {
    public:
        /** @brief 읽기 하나를 시리얼로 등록합니다. 같은 시리얼이 이미 있으면 안 됩니다(시리얼은 발급마다 새 값이다). */
        void add( uint64 serial, Payload payload )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _mapPayload.insert_or_assign( serial, std::move( payload ) );
        }

        /** @brief 등록된 읽기를 꺼내며 지웁니다. 없으면(업로드 티켓, 이미 가져감, 버림) false 입니다. */
        [[nodiscard]] bool take( uint64 serial, Payload& outPayload )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              foundIt = _mapPayload.find( serial );
            if ( foundIt == _mapPayload.end() )
                return false;
            outPayload = std::move( foundIt->second );
            _mapPayload.erase( foundIt );
            return true;
        }

        /** @brief 아직 가져가지 않은 읽기 수입니다. */
        [[nodiscard]] uint32 getCount() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return static_cast<uint32>( _mapPayload.size() );
        }

    private:
        mutable mutex                  _mutex;
        unordered_map<uint64, Payload> _mapPayload;
    };
} // namespace sw
