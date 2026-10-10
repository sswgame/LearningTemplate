/**
 * @file LocalStore.h
 * @brief 클라이언트 로컬 저장 계약 — 슬롯(이름 → 바이트) 하나씩 원자적으로 읽고 · 쓰고 · 지우고 · 나열합니다. 맡기고(`submit*`) 거둡니다(`pollCompletions`) — 게임 스레드는 디스크를 기다리지 않는다.
 * @details - 슬롯 이름은 ASCII `[0-9a-z_.-]`(64 자 이하, `/` 로 한 단계 묶음 하나까지 — `save/slot0`). 대소문자를 가리지 않는 파일 시스템에서도 같다.
 *          - 쓰기는 슬롯 하나 단위로 원자적이다 — 도중에 꺼져도 옛 내용 아니면 새 내용이다(파일: 임시 파일 → 이름 바꾸기, SQLite: 한 문).
 *          - 모든 저장소가 같은 봉투(`LocalSlotEnvelope` — 형식 판 · 코덱 · 봉인 · CRC/태그)를 쓴다 — 저장소를 바꿔도 같은 바이트다.
 *          - 서버 계약(`IServiceStore` — 64 KiB 레코드 · 여러 키 트랜잭션)을 쓰지 않는다: 파일 저장소가 지킬 수 없는 약속이다. 경쟁 데이터의 정본은 서버다.
 *          언리얼 `ISaveGameSystem`(슬롯 이름 + 바이트, 플랫폼별 저장) 자리이고, 그 위의 `SaveGame` 이 `USaveGame` 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    enum class LocalStoreResult : uint8
    {
        Ok = 0,
        NotFound,
        Corrupt,  ///< 봉투 · 체크섬 · 태그가 맞지 않는다(변조 · 반쪽 쓰기 — 원자 쓰기라 보통은 변조)
        WrongKey, ///< 봉인을 풀 키가 이 장치 · 계정 것이 아니다
        IoError,  ///< 디스크 가득 · 권한 · 경로 · 내린 저장소
        Invalid   ///< 슬롯 이름 규칙 · 크기 상한(64 MiB) · 봉인할 암호 창구가 없다
    };
} // namespace sw

namespace sw
{
    enum class LocalStoreOperation : uint8
    {
        Read = 0,
        Write,
        Erase,
        List ///< 묶음 접두로 슬롯 이름 · 크기 · 쓴 시각
    };
} // namespace sw

namespace sw
{
    /** @brief 봉투의 봉인입니다. */
    enum class LocalStoreSeal : uint8
    {
        None = 0,      ///< CRC32 만(사람이 열어 볼 세이브 · 개발)
        Authenticated, ///< AEAD 태그 — 고치면 Corrupt(가벼운 변조 막기, 내용은 보인다)
        Encrypted      ///< AEAD — 내용도 숨긴다(계정 연동 토큰 · 결제 대기 영수증)
    };
} // namespace sw

namespace sw
{
    /** @brief 슬롯 하나의 쓰기 선택입니다. */
    struct LocalStoreWriteOptions
    {
        uint32         _formatVersion{ 1 };    ///< 쓰는 쪽의 형식 판 — 읽을 때 그대로 돌려준다(옛 판 리더를 두지 않는다 — 다르면 부르는 쪽이 거절)
        uint8          _compressionCodec{ 0 }; ///< Core `CompressionCodecType` 값(0 = 안 함). 줄지 않으면 원문으로 둔다
        LocalStoreSeal _seal{ LocalStoreSeal::None };
    };
} // namespace sw

namespace sw
{
    struct LocalSlotInfo
    {
        string _slot{};
        int64  _byteCount{ 0 };   ///< 봉투 크기
        int64  _writtenAtMs{ 0 }; ///< 이 기계의 파일 시계(밀리초) — 같은 기계 안에서만 견준다
    };
} // namespace sw

namespace sw
{
    /** @brief 끝난 요청 하나입니다. */
    struct LocalStoreCompletion
    {
        vector<uint8>         _bytes{};        ///< Read — 봉투를 푼 몸
        vector<LocalSlotInfo> _listSlotInfo{}; ///< List — 이름순
        string                _slot{};         ///< Read · Write · Erase 의 슬롯, List 의 묶음 접두
        uint64                _requestID{ 0 };
        uint32                _formatVersion{ 0 }; ///< Read — 쓸 때의 판
        LocalStoreResult      _result{ LocalStoreResult::Ok };
        LocalStoreOperation   _operation{ LocalStoreOperation::Read };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ILocalStoreKeyProvider
     * @brief 봉인 키(32 B)를 줍니다. 기본은 장치 키(`LocalDeviceKeyProvider`), 계정 단위 키(서버가 준 것)로 바꿀 수 있습니다. 저장소 스레드에서 불릴 수 있다.
     */
    class SW_GF_API ILocalStoreKeyProvider
    {
    public:
        static constexpr int32 kKeySize = 32;

        ILocalStoreKeyProvider()          = default;
        virtual ~ILocalStoreKeyProvider() = default;

        ILocalStoreKeyProvider( const ILocalStoreKeyProvider& )            = delete;
        ILocalStoreKeyProvider& operator=( const ILocalStoreKeyProvider& ) = delete;

        [[nodiscard]] virtual bool getSealKey( uint8 ( &outKey )[kKeySize] ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ILocalStore
     * @brief 로컬 저장의 앞입니다 — 게임 스레드 하나가 맡기고 거둔다. 완료는 맡긴 순서대로 온다.
     *        구현: `MemoryLocalStore`(시험 — 그 자리에서 실행) · `ThreadedLocalStore`(전용 스레드 하나 + 슬롯 저장소: 파일 · SQLite).
     */
    class SW_GF_API ILocalStore
    {
    public:
        static constexpr int64 kMaxSlotSize     = 64ll * 1024 * 1024;
        static constexpr int32 kMaxSlotNameSize = 64;

        ILocalStore()          = default;
        virtual ~ILocalStore() = default;

        ILocalStore( const ILocalStore& )            = delete;
        ILocalStore& operator=( const ILocalStore& ) = delete;

        virtual uint64 submitRead( string_view slot )                                                              = 0;
        virtual uint64 submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options ) = 0;
        virtual uint64 submitErase( string_view slot )                                                             = 0;
        /** @brief 묶음 접두(`save/` · 빈 글 = 모두)로 나열합니다. */
        virtual uint64 submitList( string_view groupPrefix )                              = 0;
        virtual int32  pollCompletions( vector<LocalStoreCompletion>& outListCompletion ) = 0;
        virtual int32  getPendingCount() const                                            = 0;
        /** @brief 남은 쓰기 · 지우기를 **끝까지 마치고** 내립니다(종료 때 세이브를 잃지 않는다 — 남은 읽기 · 나열은 IoError). 그 뒤 맡긴 것은 IoError. */
        virtual void shutdown() = 0;

        /** @brief 슬롯 이름 규칙(`[0-9a-z_.-]`, 1..64 자, `/` 하나까지, 빈 마디 · `.` 으로 시작하는 마디 없음)을 지키는가입니다. */
        static bool isValidSlotName( string_view slot );
        /** @brief 나열 접두 규칙 — 빈 글이거나 `<묶음>/` 입니다. */
        static bool isValidGroupPrefix( string_view groupPrefix );
    };
} // namespace sw
