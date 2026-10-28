/**
 * @file NetInputWindow.h
 * @brief 비신뢰 입력 번들을 주고받는 두 클래스를 제공합니다.
 * @details 보내는 쪽(`NetInputSendWindow`)은 상대가 확인한 다음 틱부터 가장 새 틱까지를 싣는다.
 *          받는 쪽(`NetInputReceiveBuffer`)은 틱별로 모아 두고 "빈틈없이 받은 다음 틱"을 확인으로 돌려준다.
 *          권위 서버 입력과 롤백 입력이 함께 쓴다.
 *
 *          확인에 기반하므로 연속 손실이 몇 틱이든 빈틈이 남지 않는다.
 *          "최근 N개"만 겹쳐 보내면 N을 넘는 연속 손실이 영구 빈틈이 되기 때문이다(롤백은 30틱 동안 끊기면 양쪽이 멈췄다).
 *          확인 값은 받는 쪽이 자기 메시지(스냅샷, 롤백 입력)에 실어 돌려준다. 이 클래스는 값만 제공한다.
 *          번들의 와이어 형식은 `NetInputFormat` 이 정한다. 첫 틱(가변 정수), 개수(가변 정수), 항목마다 [스탬프(가변 정수)]와 바이트가 이어진다.
 *          GGPO 의 입력 큐(확인된 다음 프레임부터 전부), 언리얼의 `FSavedMove` 목록(확인된 이동까지 지운다),
 *          유니티 Netcode 의 `ICommandData`(최근 3개와 확인)에 해당한다.
 * @code
 *     // 보내는 쪽 — 틱마다
 *     (void)window.push( tick, input.data(), static_cast<int32>( input.size() ) );
 *     window.acknowledge( ackFromPeer );                   // 상대 메시지에서 꺼낸 "빈틈없이 받은 다음 틱"
 *     NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
 *     budget.reserveBits( writer.getBitCount() );          // 이미 쓴 종류 바이트 · 머리
 *     (void)window.write( writer, budget );
 *     // 받는 쪽
 *     if ( buffer.read( reader ) == false ) return NetHandleResult::Malformed;
 *     reply.writeVarUint( buffer.getFirstMissingTick() );  // 확인
 * @endcode
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Replication/TickRingBuffer.h"

namespace sw
{
    class BitReader;
    class BitWriter;
    class NetSendBudget;

    /**
     * @struct NetInputFormat
     * @brief 입력 번들의 와이어 형식입니다.
     * @details 보내는 쪽과 받는 쪽이 같은 값을 써야 한다(키트가 상수로 둔다). 값을 바꾸면 그 키트의 와이어 버전을 올린다.
     *          번들은 첫 틱(가변 정수), 개수(가변 정수), 항목들로 이루어진다.
     *          `_bStamped` 이면 항목마다 스탬프(가변 정수)를 먼저 싣는다. 바이트는 `_fixedEntryBytes` 가 있으면 그 크기 그대로, 없으면 길이를 붙인 덩어리(`writeBlob`)로 싣는다.
     */
    struct NetInputFormat
    {
        int32 _fixedEntryBytes{ 0 }; ///< 0보다 크면 항목마다 이 크기의 바이트를 길이 필드 없이 싣는다(롤백 버튼은 1바이트). 0이면 길이를 붙인 덩어리로 싣는다.
        int32 _maxEntryBytes{ 255 }; ///< 덩어리 하나의 상한. 넘는 입력은 넣지 않고(`push`, `store` 가 false), 받는 쪽은 상한을 넘는 길이를 깨진 번들으로 본다.
        int32 _maxEntryCount{ 32 };  ///< 번들 하나의 항목 수 상한. 보내는 쪽은 이만큼까지 싣고, 받는 쪽은 넘는 개수를 깨진 번들으로 본다.
        uint8 _bStamped{ SW_FALSE }; ///< 항목마다 스탬프(가변 정수)를 싣는다. 입력이 만들어진 순간(서브틱 시각, 그때 보던 틱)을 항목과 함께 보낼 때 쓴다.

        /** @brief 이 형식에 실을 수 있는 항목 크기인지 반환합니다. */
        constexpr bool isValidEntrySize( int32 byteCount ) const
        {
            return _fixedEntryBytes > 0 ? byteCount == _fixedEntryBytes : 0 <= byteCount && byteCount <= _maxEntryBytes;
        }
    };
} // namespace sw

namespace sw
{
    /** @struct NetInputEntry @brief 틱 하나의 입력입니다. 링 버퍼의 한 슬롯이며, 빈 슬롯 표시는 `TickRingBuffer` 가 보관합니다. */
    struct NetInputEntry
    {
        vector<uint8> _bytes{};
        uint32        _tick{ 0 };
        uint32        _stamp{ 0 }; ///< `NetInputFormat::_bStamped` 일 때 항목과 함께 온 값(아니면 0).
    };
} // namespace sw

namespace sw
{
    /** @brief 받는 창을 누가 옮기는지 정합니다. */
    enum class NetInputWindowMode : uint8
    {
        Manual,       ///< 쓰는 쪽이 `setWindow` 로 옮긴다(롤백에서는 현재 프레임 ± 링 크기의 절반).
        FollowNewest, ///< 받은 가장 새 틱이 창 끝에 오도록 따라간다(권위 서버에서는 클라이언트 틱이 어디서 시작하든 받는다).
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetInputSendWindow
     * @brief 내 입력을 틱 순서로 보관하고, 상대가 확인한 다음 틱부터 가장 새 틱까지를 번들으로 씁니다.
     * @details - 보관하는 것은 최근 `capacity` 틱이다. 그보다 오래 확인이 없으면 가장 오래된 것부터 잊는다.
     *            이런 일은 쓰는 쪽이 그 앞에서 멈췄거나(롤백의 최대 예측), 받는 쪽이 이미 지나간 틱일 때(권위 서버가 꺼낸 틱은 확인으로 넘어간다) 생긴다.
     *          - 예산이 모자라면 **오래된 것부터** 싣고 새 것은 다음 번들으로 미룬다. 새 것부터 실으면 싣지 못한 오래된 것이 영구 빈틈이 된다.
     *          - 틱은 하나씩 증가한다. 같은 틱을 다시 넣으면 처음 값이 남는다(롤백은 멈춘 프레임에서 같은 프레임 입력을 다시 낸다).
     *            틱이 건너뛰면 앞 틱과 이어 실을 수 없으므로 그 틱부터 다시 쌓는다. 틱이 줄면(새 버전) 확인 위치까지 비운다.
     *          - 스레드 안전하지 않다(보내는 쪽 하나가 쓴다).
     */
    class SW_API NetInputSendWindow
    {
    public:
        NetInputSendWindow();

        /** @brief 최근 @p capacity 틱을 보관할 공간을 잡고 비웁니다. 번들 하나에는 `NetInputFormat::_maxEntryCount` 까지만 싣습니다. */
        void initialize( int32 capacity, const NetInputFormat& format );
        /** @brief 보관 중인 입력과 확인을 비웁니다(재연결, 새 버전). */
        void reset();
        /**
         * @brief @p tick 의 내 입력을 넣습니다. 형식에 맞지 않는 크기이거나 이미 있는 틱이면 넣지 않고 false 를 반환합니다(처음 값이 남는다).
         * @param stamp `NetInputFormat::_bStamped` 일 때 항목과 함께 보내는 값입니다(아니면 싣지 않는다).
         */
        [[nodiscard]] bool push( uint32 tick, const uint8* pData, int32 byteCount, uint32 stamp = 0 );
        /** @brief 상대가 빈틈없이 받은 다음 틱을 알립니다. 그 앞은 다시 싣지 않습니다. 줄어드는 값(늦게 도착한 옛 확인)과 아직 넣지 않은 틱은 무시합니다. */
        void acknowledge( uint32 firstMissingTick );
        /**
         * @brief 확인되지 않은 가장 오래된 틱부터 가장 새 틱까지를 번들으로 씁니다.
         * @details @p budget 이 모자라면 오래된 것부터 들어가는 만큼만 싣고, 싣지 못한 새 것은 다음 번들이 싣는다. 첫 틱과 개수 필드도 @p budget 에서 센다.
         * @return 실은 항목 수입니다.
         */
        int32 write( BitWriter& writer, NetSendBudget& budget ) const;

        bool   hasEntry() const { return _bHasEntry != SW_FALSE; }
        uint32 getLatestTick() const { return _latestTick; }
        /** @brief 다음 번들이 싣기 시작할 틱(확인되지 않은 가장 오래된 틱)입니다. */
        uint32 getFirstPendingTick() const;
        /** @brief 확인되지 않은 항목 수입니다. 다음 번들에 실을 후보입니다. */
        int32 getPendingCount() const;

    private:
        /** @brief 보관 중인 틱 [_oldestTick, _latestTick] 의 항목입니다. */
        const NetInputEntry& getEntry( uint32 tick ) const;
        int32                computeEntryBits( const NetInputEntry& entry ) const;
        void                 writeEntry( BitWriter& writer, const NetInputEntry& entry ) const;

        TickRingBuffer<NetInputEntry> _listEntry; ///< 틱으로 찾는 링 버퍼. 보관 중인 틱 [_oldestTick, _latestTick] 은 항상 들어 있다.
        NetInputFormat                _format;
        uint32                        _oldestTick; ///< 보관 중인 가장 오래된 틱(그 앞은 덮였거나 틱이 건너뛰었다).
        uint32                        _latestTick;
        uint32                        _firstUnacknowledgedTick; ///< 상대가 빈틈없이 받은 다음 틱.
        uint8                         _bHasEntry;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetInputReceiveBuffer
     * @brief 상대 입력을 틱별로 모으고 "빈틈없이 받은 다음 틱"(확인)을 계산합니다. 다시 보낸 틱과 창 밖 틱은 버립니다.
     * @details - 받는 창 [첫 틱, 끝)은 링 크기를 넘지 않는다. 그래서 창 안의 틱끼리는 링 슬롯을 덮어쓰지 않는다.
     *            창이 없으면 링을 한 바퀴 돈 먼 틱이 받아 둔 입력을 덮어쓴다.
     *            첫 틱은 줄지 않고 창 아래는 더 받지 않으므로, 확인도 첫 틱까지 넘어간다.
     *          - 깨진 번들(개수와 길이 상한 초과, 모자란 바이트)은 끝까지 훑어 본 뒤 **하나도 넣지 않는다**.
     *          - 스레드 안전하지 않다(받는 쪽 하나가 쓴다). 읽기만 하는 `get*` 과 `find*` 는 쓰기와 겹치지 않으면 여러 스레드가 함께 불러도 된다.
     */
    class SW_API NetInputReceiveBuffer
    {
    public:
        /** @brief `setWindow` 의 끝을 정하지 않을 때 쓰는 값입니다. 창 첫 틱에 링 크기를 더한 곳까지 받습니다. */
        static constexpr uint32 kNoWindowEnd = invalid_index::kUint32;

        NetInputReceiveBuffer();

        /** @brief @p capacity 슬롯의 링을 잡고 비웁니다. 창은 [0, @p capacity) 에서 시작합니다. */
        void initialize( int32 capacity, const NetInputFormat& format, NetInputWindowMode mode );
        /** @brief 받은 입력, 창, 확인을 비웁니다(재연결, 새 버전). */
        void reset();
        /**
         * @brief 받는 창을 [@p first, @p end) 로 옮깁니다. 첫 틱은 줄지 않고, 끝은 첫 틱에 링 크기를 더한 값으로 잘립니다.
         *        창 아래는 더 받지 않으므로 확인(`getFirstMissingTick`)도 첫 틱까지 넘어갑니다. 다 쓴 틱을 놓을 때(권위 서버가 꺼낸 틱) 부릅니다.
         */
        void setWindow( uint32 first, uint32 end );
        /** @brief 입력 하나를 넣습니다. 창 밖이거나 이미 있는 틱이거나 형식에 맞지 않는 크기면 넣지 않고 false 를 반환합니다. */
        [[nodiscard]] bool store( uint32 tick, const uint8* pData, int32 byteCount, uint32 stamp = 0 );
        /**
         * @brief 번들 하나를 읽어 창 안의 새 틱만 넣습니다. 깨졌으면 하나도 넣지 않고 false 를 반환합니다(받은 쪽은 메시지를 깨진 것으로 센다).
         * @param pOutListNewTick 있으면 새로 넣은 틱을 오름차순으로 채웁니다(먼저 비운다).
         */
        [[nodiscard]] bool read( BitReader& reader, vector<uint32>* pOutListNewTick = nullptr );

        /** @brief 그 틱의 입력을 반환합니다. 없으면(받지 못했거나 링에서 덮였다) nullptr 입니다. */
        const NetInputEntry* find( uint32 tick ) const;
        /** @brief @p tick 이하이고 @p lowestTick 이상인 틱 중 가장 새로 받은 것을 반환합니다. 링 한 바퀴 안까지만 봅니다. 없으면 nullptr 입니다. */
        const NetInputEntry* findLatestAtOrBefore( uint32 tick, uint32 lowestTick ) const;
        /** @brief 빈틈없이 받은 다음 틱을 반환합니다. 상대에게 돌려주는 확인 값이며, 창 아래로 놓은 틱은 받은 것으로 칩니다. */
        uint32 getFirstMissingTick() const { return _firstMissingTick; }
        uint32 getWindowFirst() const { return _windowFirst; }
        uint32 getWindowEnd() const { return _windowEnd; }

    private:
        bool               canStore( uint32 tick ) const;
        void               followTick( uint32 tick );
        void               advanceFirstMissingTick();
        [[nodiscard]] bool readEntry( BitReader& reader, NetInputEntry& outEntry ) const;
        [[nodiscard]] bool skipEntry( BitReader& reader ) const;

        TickRingBuffer<NetInputEntry> _listEntry; ///< 틱으로 찾는 링 버퍼. 받는 창이 링 크기를 넘지 않으므로 창 안의 틱끼리 덮어쓰지 않는다.
        NetInputFormat                _format;
        uint32                        _windowFirst;

        uint32             _windowEnd;
        uint32             _firstMissingTick;
        NetInputWindowMode _mode;
    };
} // namespace sw
