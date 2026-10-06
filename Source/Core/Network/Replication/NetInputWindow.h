/**
 * @file NetInputWindow.h
 * @brief 비신뢰 입력 묶음입니다 — 보내는 쪽은 상대가 확인한 다음 틱부터 가장 새 틱까지를 싣고(`NetInputSendWindow`), 받는 쪽은 틱별로 모아
 *        "빈틈없이 받은 다음 틱" 을 확인으로 돌려줍니다(`NetInputReceiveBuffer`). 권위 서버 입력 · 롤백 입력이 같이 씁니다.
 * @details 확인 기반이라 연속 손실이 몇 틱이든 빈틈이 남지 않는다 — "최근 N 개" 만 겹쳐 보내면 N 을 넘는 연속 손실이 영구 빈틈이 된다(롤백은 30 틱
 *          끊김 뒤 두 쪽이 멈췄다). 확인 값은 받는 쪽이 자기 메시지(스냅숏 · 롤백 입력)에 실어 돌려준다 — 이 부품은 값만 준다.
 *          묶음의 선 형식은 `NetInputFormat` — 첫 틱(가변 정수) · 개수(가변 정수) · 항목마다 [스탬프(가변 정수)] + 바이트.
 *          GGPO 의 입력 큐(확인된 다음 프레임부터 전부) · 언리얼 `FSavedMove` 목록(확인된 이동까지 지운다) · 유니티 Netcode `ICommandData`(최근 3 개 +
 *          확인)의 자리입니다.
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
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Replication/TickRingBuffer.h"

namespace sw
{
    class BitReader;
    class BitWriter;
    class NetSendBudget;

    /**
     * @struct NetInputFormat
     * @brief 입력 묶음의 선 형식입니다 — 보내는 쪽과 받는 쪽이 같은 값을 써야 합니다(키트가 상수로 둔다). 값을 바꾸면 그 키트의 와이어 판을 올린다.
     * @details 묶음 = 첫 틱(가변 정수) · 개수(가변 정수) · 항목마다 [스탬프(가변 정수) — `_bStamped` 일 때] + 바이트(`_fixedEntryBytes` 면 그만큼 그대로,
     *          아니면 길이 붙인 덩어리 `writeBlob`).
     */
    struct NetInputFormat
    {
        int32 _fixedEntryBytes{ 0 }; ///< 0 보다 크면 항목마다 이 바이트를 길이 칸 없이 싣는다(롤백 버튼 1 바이트). 0 이면 길이 붙인 덩어리
        int32 _maxEntryBytes{ 255 }; ///< 덩어리 하나의 상한 — 넘는 입력은 넣지 않고(`push` · `store` 가 false), 받는 쪽은 넘는 길이를 깨짐으로 본다
        int32 _maxEntryCount{ 32 };  ///< 묶음 하나의 항목 수 상한 — 보내는 쪽은 이만큼까지 싣고, 받는 쪽은 넘는 개수를 깨짐으로 본다
        uint8 _bStamped{ SW_FALSE }; ///< 항목마다 스탬프(가변 정수)를 싣는다 — 입력이 만들어진 순간(서브틱 시각 · 그때 보던 틱)을 항목과 함께 보낼 때

        /** @brief 이 형식에 실을 수 있는 항목 크기인가입니다. */
        constexpr bool isValidEntrySize( int32 byteCount ) const
        {
            return _fixedEntryBytes > 0 ? byteCount == _fixedEntryBytes : 0 <= byteCount && byteCount <= _maxEntryBytes;
        }
    };
} // namespace sw

namespace sw
{
    /** @struct NetInputEntry @brief 틱 하나의 입력입니다(고리 한 칸 — 빈 칸 표시는 `TickRingBuffer` 가 든다). */
    struct NetInputEntry
    {
        vector<uint8> _bytes{};
        uint32        _tick{ 0 };
        uint32        _stamp{ 0 }; ///< `NetInputFormat::_bStamped` 일 때 항목과 함께 온 값(아니면 0)
    };
} // namespace sw

namespace sw
{
    /** @brief 받는 창을 누가 옮기나입니다. */
    enum class NetInputWindowMode : uint8
    {
        Manual,       ///< 쓰는 쪽이 `setWindow` 로 옮긴다(롤백 — 지금 프레임 ± 고리 반)
        FollowNewest, ///< 받은 가장 새 틱이 창 끝에 오게 따라간다(권위 서버 — 클라이언트 틱이 어디서 시작하든 받는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetInputSendWindow
     * @brief 내 입력을 틱 순으로 들고, 상대가 확인한 다음 틱부터 가장 새 틱까지를 묶음으로 씁니다.
     * @details - 들고 있는 것은 최근 `capacity` 틱이다 — 그보다 오래 확인이 없으면 가장 오래된 것부터 잊는다. 쓰는 쪽이 그 앞에서 멈추거나(롤백 — 최대
     *            예측) 받는 쪽이 이미 지나간 틱이다(권위 서버 — 서버가 꺼낸 틱은 확인으로 넘어간다).
     *          - 예산이 모자라면 **오래된 것부터** 싣고 새 것은 다음 묶음으로 민다 — 새 것부터 실으면 못 실은 오래된 것이 영구 빈틈이 된다.
     *          - 틱은 하나씩 는다. 같은 틱을 다시 넣으면 처음 값이 남는다(롤백은 멈춘 프레임에 같은 프레임 입력을 다시 낸다). 틱이 건너뛰면 그 앞과
     *            이어 실을 수 없어 그 틱부터 다시 쌓고, 틱이 줄면(새 판) 확인까지 비운다.
     *          스레드 안전하지 않다(보내는 쪽 하나가 쓴다).
     */
    class SW_API NetInputSendWindow
    {
    public:
        NetInputSendWindow();

        /** @brief 최근 @p capacity 틱을 들 자리를 잡고 비웁니다. 묶음 하나에는 `NetInputFormat::_maxEntryCount` 까지만 싣는다. */
        void initialize( int32 capacity, const NetInputFormat& format );
        /** @brief 들고 있는 입력과 확인을 비웁니다(다시 연결 · 새 판). */
        void reset();
        /**
         * @brief @p tick 의 내 입력을 넣습니다. 형식에 맞지 않는 크기이거나 이미 있는 틱이면 넣지 않고 false 입니다(처음 값이 남는다).
         * @param stamp `NetInputFormat::_bStamped` 일 때 항목과 함께 가는 값입니다(아니면 싣지 않는다).
         */
        [[nodiscard]] bool push( uint32 tick, const uint8* pData, int32 byteCount, uint32 stamp = 0 );
        /** @brief 상대가 빈틈없이 받은 다음 틱입니다 — 그 앞은 다시 싣지 않는다. 줄어드는 값(늦게 온 옛 확인)과 아직 넣지 않은 틱은 무시한다. */
        void acknowledge( uint32 firstMissingTick );
        /**
         * @brief 확인 안 된 가장 오래된 틱부터 가장 새 틱까지를 묶음으로 씁니다. @p budget 이 모자라면 오래된 것부터 들어가는 만큼만 — 못 실은 새 것은
         *        다음 묶음이 싣는다. 첫 틱 · 개수 칸도 @p budget 에서 센다.
         * @return 실은 항목 수입니다.
         */
        int32 write( BitWriter& writer, NetSendBudget& budget ) const;

        bool   hasEntry() const { return _bHasEntry != SW_FALSE; }
        uint32 getLatestTick() const { return _latestTick; }
        /** @brief 다음 묶음이 싣기 시작할 틱(확인 안 된 가장 오래된 것)입니다. */
        uint32 getFirstPendingTick() const;
        /** @brief 확인 안 된 항목 수 — 다음 묶음에 실을 후보입니다. */
        int32 getPendingCount() const;

    private:
        /** @brief 들고 있는 틱 [_oldestTick, _latestTick] 의 항목입니다. */
        const NetInputEntry& getEntry( uint32 tick ) const;
        int32                computeEntryBits( const NetInputEntry& entry ) const;
        void                 writeEntry( BitWriter& writer, const NetInputEntry& entry ) const;

        TickRingBuffer<NetInputEntry> _listEntry; ///< 틱으로 찾는 고리 — 들고 있는 틱 [_oldestTick, _latestTick] 은 늘 들어 있다
        NetInputFormat                _format;
        uint32                        _oldestTick; ///< 들고 있는 가장 오래된 틱(그 앞은 덮였거나 틱이 건너뛰었다)
        uint32                        _latestTick;
        uint32                        _firstUnacknowledgedTick; ///< 상대가 빈틈없이 받은 다음 틱
        uint8                         _bHasEntry;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetInputReceiveBuffer
     * @brief 상대 입력을 틱별로 모으고 "빈틈없이 받은 다음 틱"(확인)을 셉니다. 다시 보낸 틱 · 창 밖 틱은 버린다.
     * @details - 받는 창 [첫 틱, 끝)은 고리 크기를 넘지 않는다 — 창 안의 틱끼리는 고리 칸을 덮지 않는다(위 창이 없으면 고리 한 바퀴 뒤의 먼 틱이
     *            받아 둔 입력을 덮는다). 첫 틱은 줄지 않고, 창 아래는 더 받지 않으므로 확인도 첫 틱까지 넘어간다.
     *          - 깨진 묶음(개수 · 길이 상한, 모자란 바이트)은 끝까지 훑어 본 뒤 **하나도 넣지 않는다**.
     *          스레드 안전하지 않다(받는 쪽 하나가 쓴다 — 읽기만 하는 `get*` · `find*` 는 쓰기와 겹치지 않으면 여러 스레드가 같이 불러도 된다).
     */
    class SW_API NetInputReceiveBuffer
    {
    public:
        /** @brief `setWindow` 의 끝을 정하지 않을 때 — 창 첫 틱 + 고리 크기까지 받는다. */
        static constexpr uint32 kNoWindowEnd = invalid_index::kUint32;

        NetInputReceiveBuffer();

        /** @brief @p capacity 칸 고리를 잡고 비웁니다. 창은 [0, @p capacity) 에서 시작한다. */
        void initialize( int32 capacity, const NetInputFormat& format, NetInputWindowMode mode );
        /** @brief 받은 입력 · 창 · 확인을 비웁니다(다시 연결 · 새 판). */
        void reset();
        /**
         * @brief 받는 창을 [@p first, @p end) 로 옮깁니다. 첫 틱은 줄지 않고 끝은 첫 틱 + 고리 크기로 잘린다.
         *        창 아래는 더 받지 않으므로 확인(`getFirstMissingTick`)도 첫 틱까지 넘어간다 — 다 쓴 틱을 놓을 때(권위 서버가 꺼낸 틱) 부른다.
         */
        void setWindow( uint32 first, uint32 end );
        /** @brief 입력 하나를 넣습니다. 창 밖 · 이미 있는 틱 · 형식에 맞지 않는 크기면 넣지 않고 false 입니다. */
        [[nodiscard]] bool store( uint32 tick, const uint8* pData, int32 byteCount, uint32 stamp = 0 );
        /**
         * @brief 묶음 하나를 읽어 창 안의 새 틱만 넣습니다. 깨졌으면 하나도 넣지 않고 false 입니다(받은 쪽은 메시지를 깨짐으로 센다).
         * @param pOutListNewTick 있으면 새로 넣은 틱을 오름차순으로 채웁니다(먼저 비운다).
         */
        [[nodiscard]] bool read( BitReader& reader, vector<uint32>* pOutListNewTick = nullptr );

        /** @brief 그 틱의 입력입니다. 없으면(못 받았거나 고리에서 덮였다) nullptr 입니다. */
        const NetInputEntry* find( uint32 tick ) const;
        /** @brief @p tick 이하 · @p lowestTick 이상에서 가장 새로 받은 것입니다(고리 한 바퀴 안까지만 본다). 없으면 nullptr 입니다. */
        const NetInputEntry* findLatestAtOrBefore( uint32 tick, uint32 lowestTick ) const;
        /** @brief 빈틈없이 받은 다음 틱 — 상대에게 돌려주는 확인입니다(창 아래로 놓은 틱은 받은 것으로 친다). */
        uint32 getFirstMissingTick() const { return _firstMissingTick; }
        uint32 getWindowFirst() const { return _windowFirst; }
        uint32 getWindowEnd() const { return _windowEnd; }

    private:
        bool               canStore( uint32 tick ) const;
        void               followTick( uint32 tick );
        void               advanceFirstMissingTick();
        [[nodiscard]] bool readEntry( BitReader& reader, NetInputEntry& outEntry ) const;
        [[nodiscard]] bool skipEntry( BitReader& reader ) const;

        TickRingBuffer<NetInputEntry> _listEntry; ///< 틱으로 찾는 고리 — 받는 창이 고리 크기를 넘지 않아 창 안끼리 덮지 않는다
        NetInputFormat                _format;
        uint32                        _windowFirst;

        uint32             _windowEnd;
        uint32             _firstMissingTick;
        NetInputWindowMode _mode;
    };
} // namespace sw
