/**
 * @file RemoteConfig.h
 * @brief 서버가 내려 주는 설정 값과 기능 플래그입니다. 저장소 테이블 `remote_config`(키별 값과 버전)가 원본입니다.
 *        서버는 주기적으로 다시 읽고(`OnlineServerRuntime`), 클라이언트는 로그인 응답의 번들 해시가 다르면 번들을 받아 갑니다.
 * @details - 값 종류는 정수, 실수, 문자열, 플래그다. 플래그는 켬/끔(`_integer` 가 0 이 아니면 켬)과 출시 비율(0..10000 만분율)이다. 계정마다
 *            `hash( 플래그 이름, 계정 id ) % 10000 < 비율` 이라 같은 계정은 늘 같은 쪽이다.
 *          - 키는 `[0-9a-z_.]`(예: "account.minimum_build.windows", "feature.trade_enabled"). 클라이언트 번들에는 `_bClientVisible` 인 키만 들어간다.
 *          - 바꾸기(`submitSet`)는 이 객체가 마지막으로 읽은 버전을 조건으로 건다. 그사이 다른 서버가 바꿨으면 Conflict 이고, 다시 읽고 다시 한다. 감사 줄과 같은 트랜잭션이다.
 *          - 요청한 작업의 `complete` 가 이 객체를 부르므로, 저장소의 완료를 모두 회수하거나 저장소를 먼저 종료한 뒤에 이 객체를 지운다. 서비스 스레드 하나가 쓴다.
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
        int32                 _rolloutBasisPoints{ kFullRolloutPoints }; ///< Flag 에서 켜진 계정 비율(만분율)
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

        /** @brief 저장소에서 다시 읽는 작업을 요청합니다. 완료되면 값과 번들 해시가 바뀝니다. */
        void requestReload( IServiceStore& store );
        /** @brief 운영자나 GM 이 값 하나를 바꿉니다. 버전 조건과 감사 줄을 한 트랜잭션에 넣습니다. 다른 서버는 주기적으로 다시 읽어 반영합니다. */
        void submitSet( IServiceStore& store, string_view key, const RemoteConfigValue& value, const ServiceAuditEntry& audit );

        bool findInteger( string_view key, int64& outValue ) const;
        bool findReal( string_view key, float64& outValue ) const;
        bool findText( string_view key, string& outValue ) const;
        /** @brief 플래그가 이 계정에 켜졌는가입니다(없거나 플래그가 아닌 키는 @p bDefault). */
        bool isFeatureEnabled( string_view flag, uint64 accountID, bool bDefault = false ) const;

        /** @brief 클라이언트 번들(보이는 키만)의 해시입니다. */
        uint64 getSnapshotHash() const { return _snapshotHash; }
        /** @brief 클라이언트에 보낼 번들(보이는 키만, 키 순서)을 씁니다. */
        void writeClientSnapshot( BitWriter& outWriter ) const;
        /** @brief 클라이언트 쪽에서 받은 번들로 값을 모두 바꿉니다. 깨졌으면 false 이고 바꾸지 않습니다. */
        [[nodiscard]] bool readClientSnapshot( BitReader& reader );

        int32              getPendingWorkCount() const { return _pendingWorkCount; }
        ServiceStoreResult getLastReloadResult() const { return _lastReloadResult; }
        ServiceStoreResult getLastSetResult() const { return _lastSetResult; }
        int32              getValueCount() const { return static_cast<int32>( _mapEntry.size() ); }

        static const hashed_string& getTable(); ///< "remote_config"
        static bool                 isValidKey( string_view key );
        /** @brief 계정의 출시 버킷(0..9999)입니다. `isFeatureEnabled` 가 비율과 비교합니다. */
        static int32 computeRolloutBucket( string_view flag, uint64 accountID );

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
        void         onSetCompleted( ServiceStoreResult result, const string& key, const RemoteConfigValue& value, uint64 version );

        map<string, Entry> _mapEntry;
        uint64             _snapshotHash;
        int32              _pendingWorkCount;
        ServiceStoreResult _lastReloadResult;
        ServiceStoreResult _lastSetResult;
    };
} // namespace sw
