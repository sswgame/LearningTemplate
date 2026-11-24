/**
 * @file ReplicationClient.h
 * @brief 클라이언트 쪽 복제기입니다. 받은 델타를 기준 스냅샷 위에 복원해 쌓고 확인을 돌려줍니다.
 * @details 보간 지연만큼 과거를 그리도록 렌더 시각을 맞추고, 입력은 서버가 확인한 다음 틱부터 보낸다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/InterpolationBuffer.h"
#include "Core/Network/Replication/NetClock.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Replication/TickRingBuffer.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"

namespace sw
{
    struct MeleeHitClaim;

    class INetPeers;

    /**
     * @brief 클라이언트 설정입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `ReplicationClient::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 기록 수를 쓸 때 조용히 4 로 올렸다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API ReplicationClientConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.001, Units = s )
        float32 _tickInterval{ 1.0f / 30.0f }; ///< 스냅샷의 틱 간격. 월드의 서버 틱(`NetClock::kTickSeconds`)의 정수 배수다
        PROPERTY( Min = 0, Units = s )
        float32 _interpolationDelay{ 0.1f }; ///< 렌더 지연의 최솟값. 실제 값은 이 값과 (틱 간격 × `NetRenderClock::kSampleIntervalsBehind`) 중 큰 쪽이며, 스냅샷 하나를 잃어도 끊기지 않게 하려는 것이다
        PROPERTY( Min = 4 )
        int32 _historySize{ 64 }; ///< 받아 두는 스냅샷 수. 델타의 기준과 보간 구간을 이 안에서 찾는다
    };
} // namespace sw

namespace sw
{
    /** @brief 엔티티 하나의 화면 시각 표본입니다 — 앞뒤 상태와 그 사이의 자리. */
    template <typename TState>
    struct NetStateSample
    {
        TState         _from;
        TState         _to;
        NetSampleRatio _ratio;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReplicationClient
     * @brief 받은 스냅샷을 틱 순서로 보관하고, `sampleState` 로 화면 시각의 엔티티 표본을 돌려줍니다.
     * @details 위치와 회전을 섞는 일은 게임이 한다(`NetSampleRatio` 의 규칙으로).
     */
    class SW_KIT_API ReplicationClient : public NetTableHandler<ReplicationClient, NetClientServerMessage::Table, NetClientServerMessage::Snapshot>
    {
    public:
        ReplicationClient();

        /**
         * @brief 호스트와 월드의 서버 시계(`SceneNetLink::getServerClock`)와 설정을 받아 시작합니다. 설정이 범위 밖이거나 틱 간격이 서버 틱의 정수 배수가 아니면 오류를 남기고 false 를 반환합니다.
         * @details 거절된 클라이언트는 호스트가 없는 클라이언트와 같다. 입력을 보내지 않는다.
         */
        [[nodiscard]] bool initialize( INetPeers* pHost, NetClock& worldClock, const ReplicationClientConfig& settings );
        NetHandleResult    receive( NetClientServerMessage::Snapshot, const NetMessageContext& context, BitReader& body );
        /** @brief 서버의 새 월드(새 연결 포함)에 들어왔습니다. 받은 스냅샷과 화면 시각을 처음부터 다시 합니다 — 새 월드의 틱은 앞 월드보다 작다. */
        void onWorldOpened( uint32 generation ) override;
        /**
         * @brief 화면 시각을 월드의 서버 시계에 맞춥니다. 라우터가 프레임에 한 번, 메시지를 다 나눠 준 뒤 부릅니다.
         * @details 그 프레임의 표본(적 · 남의 몸 · 발동 도장)이 모두 이 값을 쓴다 — 프레임 중간에 시계를 움직이면 앞 틱의 표본과 뒤 틱의 표본이 다른 시각으로 갈린다
         *          (언리얼 NetworkPrediction 의 보간 시각도 프레임에 하나다). 서버 시각의 추정은 월드에 하나뿐이고 여기는 보간 지연만 갖는다.
         */
        void onPollFinished() override;
        /**
         * @brief 이 틱의 입력을 넣고, 서버가 확인하지 않은 입력을 오래된 것부터 메시지 상한 안에서 보냅니다.
         * @details 못 실은 새 입력은 다음 메시지가 싣는다.
         *          입력이 `NetClientServerMessage::kMaxInputBytes` 를 넘으면 넣지도 보내지도 않고 false 를 반환한다. 같은 틱을 다시 내면 처음 값이 남는다.
         */
        bool sendInput( uint32 tick, const vector<uint8>& listInput );
        /**
         * @brief 폰을 조종하는 게임의 입력입니다 — 의도 하나와 이 입력 틱의 근접 맞음 주장을 한 형식으로 써서 `sendInput` 으로 보냅니다.
         * @details 바이트를 게임이 직접 짓지 않으므로 서버의 `ReplicationServer::popIntent` 와 형식이 어긋날 수 없습니다. 주장의 `_frame` 은 입력 틱이다 — 서버 프레임은 서버만 안다.
         *          쓰지 못하거나 주장이 `NetClientServerMessage::kMaxClaimsPerIntent` 를 넘으면 알리고 false 입니다.
         * @param pawnEntityID 이 의도를 낸 폰의 엔티티 id 입니다(폰이 하나인 게임은 0). 버튼 비트는 폰 스키마의 차례라, 빙의가 바뀌는 사이(탈것에 타고 내림)의 의도를
         *        서버가 다른 폰의 버튼으로 읽지 않게 한다 — 언리얼이 확인되지 않은 폰(`AcknowledgedPawn`)의 이동을 받지 않는 자리다.
         */
        [[nodiscard]] bool sendIntent( uint32 tick, const ControlIntent& intent, vector_reference<const MeleeHitClaim> listClaim, uint32 pawnEntityID = 0 );

        /**
         * @brief 종류가 @p typeID 인 엔티티의 화면 시각 표본입니다 — 앞뒤 스냅샷의 상태를 칸 선언(`ReplicatedLayout`)으로 읽고, 순간 이동 번호(`_teleportCount`)까지 보아 잇는 규칙을 정합니다.
         * @details 섞는 것은 `NetStateSample::_ratio` 의 연산으로만 한다 — 서버의 되감기(`LagCompensationHistory`)와 같은 규칙이다. `TState` 에 `_teleportCount` 가 없으면 컴파일되지 않는다.
         * @return 그 시각에 없거나(아직 생기지 않음 포함) 종류가 다르거나 깨졌으면 비어 있습니다.
         */
        template <typename TState>
        [[nodiscard]] optional<NetStateSample<TState>> sampleState( uint32 entityID, uint32 typeID ) const
        {
            const NetEntityState* pFrom = nullptr;
            const NetEntityState* pTo   = nullptr;
            NetSampleRatio        ratio = NetSampleRatio::makeHeld();
            if ( findSample( entityID, typeID, pFrom, pTo, ratio ) == false )
                return nullopt;
            NetStateSample<TState> sample{ TState{}, TState{}, ratio };
            BitReader              fromReader( pFrom->_buffer.data(), static_cast<int32>( pFrom->_buffer.size() ) );
            BitReader              toReader( pTo->_buffer.data(), static_cast<int32>( pTo->_buffer.size() ) );
            const bool             bRead = ReplicatedLayout::readFull( fromReader, sample._from, nullptr ) == FixedReadResult::Ok &&
                               ReplicatedLayout::readFull( toReader, sample._to, nullptr ) == FixedReadResult::Ok;
            if ( bRead == false )
                return nullopt;
            sample._ratio.breakAt( sample._from._teleportCount, sample._to._teleportCount );
            return sample;
        }
        /** @brief 렌더 시각에 보이는 엔티티 id 를 모읍니다. 앞 스냅샷을 기준으로 합니다. */
        void collectVisibleEntities( vector<uint32>& outListEntity ) const;

        const NetSnapshot* getLatest() const;
        /** @brief 가장 새 스냅샷에 실린 내 엔티티의 상태와 그 입력 틱입니다. 클라이언트 예측의 `reconcile` 에 넘깁니다. 서버가 아직 정하지 않았으면 nullptr 입니다. */
        const NetOwnerState* findOwnerState() const;
        /** @brief 그리는 서버 틱입니다. 보간 지연만큼 과거이며 소수이고, 프레임 안에서 바뀌지 않습니다. 스냅샷을 받기 전에는 0 입니다. */
        float64 getRenderTick() const { return _renderClock.hasSample() ? _renderClock.getRenderTick() : 0.0; }
        /** @brief 그리는 서버 틱을 보는 시각으로 냅니다 — 그린 뒤 `ControlSystem::publishViewTick` 에 넘긴다. */
        ControlViewTick   getViewTick() const { return ControlViewTick::makeSeen( getRenderTick() ); }
        const DevCounter& getDecodeFailureCount() const { return _decodeFailureCount; }
        bool              hasSnapshot() const { return _listSnapshot.hasNewest(); }
        /** @brief 서버가 아직 확인하지 않은 내 입력 수입니다. 다음 메시지가 다시 싣습니다. */
        int32 getPendingInputCount() const { return _inputWindow.getPendingCount(); }

    private:
        void resetHistory();
        void findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, NetSampleRatio& outRatio ) const;
        /** @brief 화면 시각을 사이에 둔 두 스냅샷에서 그 엔티티를 찾습니다. 그 시각에 없으면 false 입니다. */
        [[nodiscard]] bool findSample( uint32 entityID, uint32 typeID, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, NetSampleRatio& outRatio ) const;

    private:
        TickRingBuffer<NetSnapshot> _listSnapshot;  ///< 받은 스냅샷. 델타의 기준과 보간 구간을 틱으로 찾는다. 가장 새 틱이 마지막으로 받은 것이다
        NetSnapshot                 _decodeScratch; ///< 받은 델타를 복원하는 버퍼. 링 버퍼 슬롯과 맞바꿔 밀려난 스냅샷의 버퍼를 다음 복원이 재사용하므로 기준 슬롯을 덮어쓰지 않는다
        NetInputSendWindow          _inputWindow;   ///< 내 입력. 서버가 스냅샷에 실어 돌려준 확인 틱의 다음 틱부터 싣는다
        ReplicationClientConfig     _settings;
        INetPeers*                  _pHost;
        NetClock*                   _pWorldClock; ///< 월드의 서버 시계. 스냅샷의 틱을 알리고 화면 시각이 따라간다
        NetRenderClock              _renderClock; ///< 스냅샷을 그리는 시각
        uint32                      _stride;      ///< 스냅샷 틱 하나가 서버 틱 몇 개인지
        DevCounter                  _decodeFailureCount;
        NetMessageWriter            _messageWriter; ///< 보낼 메시지. 버퍼를 재사용한다
    };
} // namespace sw
