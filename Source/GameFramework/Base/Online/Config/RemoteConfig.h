/**
 * @file RemoteConfig.h
 * @brief 서버가 내려 주는 설정 값 · 기능 플래그 — 저장소 표 `remote_config`(키 → 값 + 판)가 정본입니다. 서버는 버스 "config.changed" 를 받거나 주기로 다시 읽고,
 *        클라이언트는 로그인 응답의 묶음 해시가 다르면 묶음을 받아 간다.
 * @details - 값 종류: 정수 · 실수 · 글 · 플래그. 플래그 = 켬/끔(`_integer` 0 이 아니면 켬) + 출시 비율(0..10000 만분율) — 계정마다
 *            `hash( 플래그 이름, 계정 id ) % 10000 < 비율` 이라 같은 계정은 늘 같은 쪽이다.
 *          - 키는 `[0-9a-z_.]`(예: "account.minimum_build.windows", "feature.trade_enabled"). 클라이언트 묶음에는 `_bClientVisible` 인 키만 든다.
 *          - 바꾸기(`submitSet`)는 이 객체가 마지막으로 읽은 판을 조건으로 건다 — 그새 다른 서버가 바꿨으면 Conflict(다시 읽고 다시 한다). 감사 줄과 같은 트랜잭션이다.
 *          - 맡긴 일의 `complete` 가 이 객체를 부르므로, 저장소의 완료를 모두 거두거나 저장소를 먼저 내린 뒤에 이 객체를 지운다. 서비스 스레드 하나가 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ServiceAuditEntry;

    class BitReader;
    class BitWriter;
    class IServerBus;

    /** @brief 값 종류입니다. */
    enum class RemoteConfigValueType : uint8
    {
        Integer = 0,
        Real,
        Text,
        Flag
    };
} // namespace sw

namespace sw
{
    /** @brief 값 하나입니다. */
    struct RemoteConfigValue
    {
        static constexpr int32 kMaxTextSize       = 4096;
        static constexpr int32 kFullRolloutPoints = 10000;

        string                _text{};
        int64                 _integer{ 0 };
        float64               _real{ 0.0 };
        int32                 _rolloutBasisPoints{ kFullRolloutPoints }; ///< Flag — 켜진 계정 비율(만분율)
        RemoteConfigValueType _type{ RemoteConfigValueType::Integer };
        uint8                 _bClientVisible{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RemoteConfig
     * @brief 원격 설정의 사본 하나(서버 · 클라이언트)입니다.
     */
    class SW_GF_API RemoteConfig
    {
    public:
        RemoteConfig();
        ~RemoteConfig();

        RemoteConfig( const RemoteConfig& )            = delete;
        RemoteConfig& operator=( const RemoteConfig& ) = delete;

        /** @brief 저장소에서 다시 읽는 일을 맡깁니다(완료되면 값과 묶음 해시가 바뀐다). */
        void requestReload( IServiceStore& store );
        /** @brief 운영 · GM 이 값 하나를 바꿉니다 — 판 조건 + 감사 줄을 한 트랜잭션에, 성공하면 버스 "config.changed"(몸 = 키)로 알린다. */
        void submitSet( IServiceStore& store, IServerBus* pBus, string_view key, const RemoteConfigValue& value, const ServiceAuditEntry& audit );

        bool findInteger( string_view key, int64& outValue ) const;
        bool findReal( string_view key, float64& outValue ) const;
        bool findText( string_view key, string& outValue ) const;
        /** @brief 플래그가 이 계정에 켜졌는가입니다(없거나 플래그가 아닌 키는 @p bDefault). */
        bool isFeatureEnabled( string_view flag, uint64 accountId, bool bDefault = false ) const;

        /** @brief 클라이언트 묶음(보이는 키만)의 해시입니다. */
        uint64 getSnapshotHash() const { return _snapshotHash; }
        /** @brief 클라이언트에 보낼 묶음(보이는 키만, 키 순서)을 씁니다. */
        void writeClientSnapshot( BitWriter& outWriter ) const;
        /** @brief 클라이언트 쪽 — 받은 묶음으로 값을 모두 바꿉니다. 깨졌으면 false 이고 바꾸지 않는다. */
        [[nodiscard]] bool readClientSnapshot( BitReader& reader );

        int32              getPendingWorkCount() const { return _pendingWorkCount; }
        ServiceStoreResult getLastReloadResult() const { return _lastReloadResult; }
        ServiceStoreResult getLastSetResult() const { return _lastSetResult; }
        int32              getValueCount() const { return static_cast<int32>( _mapEntry.size() ); }

        static const hashed_string& getTable(); ///< "remote_config"
        static bool                 isValidKey( string_view key );
        /** @brief 계정의 출시 칸(0..9999)입니다 — `isFeatureEnabled` 가 비율과 견준다. */
        static int32 computeRolloutBucket( string_view flag, uint64 accountId );

    private:
        class ReloadWork;
        class SetWork;

        struct Entry
        {
            RemoteConfigValue _value{};
            uint64            _version{ 0 };
        };

        const Entry* findEntry( string_view key, RemoteConfigValueType type ) const;
        void         recomputeSnapshotHash();
        void         onReloadCompleted( ServiceStoreResult result, map<string, Entry>& inoutMapEntry );
        void         onSetCompleted( ServiceStoreResult result, IServerBus* pBus, const string& key, const RemoteConfigValue& value, uint64 version );

        map<string, Entry> _mapEntry;
        uint64             _snapshotHash;
        int32              _pendingWorkCount;
        ServiceStoreResult _lastReloadResult;
        ServiceStoreResult _lastSetResult;
    };
} // namespace sw
