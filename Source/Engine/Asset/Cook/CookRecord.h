/**
 * @file CookRecord.h
 * @brief 쿠킹 작업 하나의 기록(`<출력>.cookrec`)과 파생 데이터 캐시(DDC)입니다. 다음 쿠킹은 기록의 입력만 다시 해시해 같으면 DDC 의 출력을 그대로 씁니다.
 * @details 기록은 키, 출력 해시, 입력(리소스 id 와 내용 해시) 목록을 글로 적는다. 판정은 파일 시각이 아니라 내용 해시다(`ContentHash`).
 *          기록이 없거나 형식이 다르거나(머리 줄) 깨졌으면 다시 베이크한다 — 옛 형식을 읽지 않는다.
 *          DDC 는 키의 내용 주소 폴더(`<루트>/<키 앞 두 글자>/<키>`)의 층 목록이다(`Config/Dev/DerivedData.json` — 로컬 `<프로젝트>/Saved/DDC`, 공유는 환경 변수 `SW_SHARED_DDC`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/CookJob.h"

namespace sw
{
    /**
     * @struct CookRecord
     * @brief 작업 하나의 기록입니다.
     */
    struct SW_API CookRecord
    {
        /** @brief 기록 파일 머리 줄입니다. 형식을 바꾸면 버전을 올린다 — 옛 기록은 다시 베이크하는 이유가 된다. */
        static constexpr string_view kHeader{ "SWCOOKREC 4" }; // 4: 내용 해시가 원시 바이트(CR 을 건너뛰지 않는다) - 옛 기록은 거절되어 한 번 다시 굽는다

        uint64            _key{ 0 };
        uint64            _outputHash{ 0 };
        vector<CookInput> _listInput;

        /** @brief 출력 해시를 이미 구했을 때의 기록 만들기입니다(`CookStore` 가 시간을 재며 한 번만 해시한다 — 바이트를 다시 훑지 않는다). */
        static CookRecord make( const CookJob& job, uint64 outputHash );

        /** @brief 기록을 글로 씁니다. */
        [[nodiscard]] bool write( string_view absPath ) const;
        /**
         * @brief 기록을 읽습니다. 파일이 없으면 false(조용히 — 처음 베이크하는 작업), 머리 줄이 다르거나 줄이 깨졌으면 이유를 경고하고 false 입니다.
         */
        [[nodiscard]] static bool read( string_view absPath, CookRecord& outRecord );

        /**
         * @brief 이 기록의 출력을 다시 써도 되는지 판정합니다. 입력마다 지금 내용 해시가 기록과 같고, @p desc 로 다시 구한 키가 기록의 키와 같아야 합니다.
         * @details 키 비교가 쿠커 버전 · 대상 · 설정이 바뀐 것을 잡는다. 입력 경로는 리소스 id 라 워크트리가 달라도 같은 기록이 맞는다.
         */
        bool isFresh( const CookJobDesc& desc ) const;
        /** @brief 지금 해시가 기록과 다른 첫 입력입니다. 모두 같으면 nullptr 입니다(다시 베이크하는 이유를 알릴 때). */
        const CookInput* findChangedInput() const;
    };
} // namespace sw

namespace sw
{
    /** @enum DerivedDataAccess @brief DDC 층에 이 머신이 쓸 수 있는지입니다. */
    enum class DerivedDataAccess : uint8
    {
        ReadOnly,  ///< 찾기만 한다(공유 층의 기본 — 쓰기는 설정이 허락한 머신만)
        ReadWrite, ///< 찾고, 미스 뒤 출력 · 뒤 층 히트를 채운다
    };

    /** @enum DerivedDataEntryState @brief 항목 파일 하나를 검증한 결과입니다(로그는 부르는 쪽이 정한다 — 찾기는 경고, 쓰기 전 확인은 조용히). */
    enum class DerivedDataEntryState : uint8
    {
        Missing,        ///< 파일이 없다
        Unreadable,     ///< 있는데 읽지 못했다
        InvalidFormat,  ///< 머리가 형식이 아니다(옛 형식 · 잘린 파일)
        KeyCollision,   ///< 같은 64 비트 키를 다른 작업(서술)이 썼다
        PayloadCorrupt, ///< 본문이 머리의 해시와 다르다
        Valid,          ///< 서술이 같고 본문 해시가 맞다
    };

    /**
     * @struct DerivedDataEvictionPolicy
     * @brief 쓰기 가능 층의 정리 규칙입니다(`DerivedDataCache::evictUnusedEntries`).
     * @details 언리얼 파일 시스템 DDC 의 `UnusedFileAge`(찾을 때 파일 시각을 갱신하고 오래 안 쓴 항목을 지운다)에 Zen 캐시의 디스크 상한을 더한 꼴이다 —
     *          쿠킹 팩 항목이 코드 지문마다 새로 생겨 나이만으로는 하루 수십 GB 가 쌓인다.
     */
    struct DerivedDataEvictionPolicy
    {
        uint32 _unusedFileAgeDays{ 0 }; ///< 이 날 수 넘게 찾거나 쓰지 않은 항목을 지운다
        uint64 _maxSizeBytes{ 0 };      ///< 층 합계가 넘으면 오래 안 쓴 항목부터 지운다
    };

    /** @struct DerivedDataLayer @brief DDC 층 하나(폴더 루트와 쓰기 권한, 정리 규칙)입니다. */
    struct DerivedDataLayer
    {
        AbsolutePath                        _rootPath; ///< 디스크 폴더(형식 — 항목 경로를 글로 잇고 다시 올리지 않는다)
        DerivedDataAccess                   _access{ DerivedDataAccess::ReadOnly };
        optional<DerivedDataEvictionPolicy> _evictionPolicy{}; ///< 없으면 이 프로세스가 지우지 않는다(읽기 전용 층 · 테스트 층)
    };

    /**
     * @class DerivedDataCache
     * @brief 키로 쿠킹 · 임포트 출력을 저장하고 찾는 폴더 층 목록입니다(앞이 먼저 — 로컬, 공유).
     * @details 언리얼 `DerivedDataBackendGraph` 의 파일 시스템 계층 꼴이다. 뒤 층에서 찾으면 앞의 쓰기 가능 층에 채운다.
     *          항목은 머리(형식 · 키 서술 길이 · 본문 해시) + 키 서술 + 본문이다. 서술이 글자 그대로 같고 본문 해시가 맞을 때만 히트다 — 64 비트 키 충돌과
     *          다른 머신이 쓴 깨진 바이트(공유 층은 신뢰 경계)가 조용히 틀린 산출물 대신 미스 + 경고가 된다.
     *          폴더가 아닌 백엔드(HTTP · 버킷)는 호스팅이 정해질 때 가상 인터페이스로 낸다 — 지금은 층이 모두 폴더다.
     *          쓰기는 임시 파일 + 원자 교체(`AtomicFileWriter` — 본문을 조각으로 흘린다)이고, 같은 서술 · 같은 본문의 항목이 이미 있으면 그대로 둔다 — 같은 내용의 원본 둘(병렬 임포트) ·
     *          같은 셰이더를 함께 컴파일하는 프로세스가 한 파일을 다투지 않고, 교체를 진 쪽도 이긴 쪽의 같은 항목을 보면 성공이다.
     *          셰이더 디스크 캐시(`<프로젝트>/Saved/ShaderCompileCache`)도 이 항목 형식을 쓴다(층 하나).
     */
    class SW_API DerivedDataCache
    {
    public:
        /** @brief 항목 머리의 형식 글입니다. 형식을 바꾸면 올린다 — 옛 항목은 미스가 된다. */
        static constexpr string_view kEntryHeader{ "SWDDC 1" };
        /** @brief 층 목록 설정 파일(프로젝트 루트 기준)입니다. */
        static constexpr const utf8* kConfigFile = "Config/Dev/DerivedData.json";

        explicit DerivedDataCache( vector<DerivedDataLayer> listLayer );

        /** @brief 로컬 DDC 루트(`<프로젝트>/Saved/DDC`)입니다. */
        static AbsolutePath makeLocalRootPath();
        /**
         * @brief `Config/Dev/DerivedData.json` 의 층 목록으로 만듭니다. 파일이 없거나 칸이 틀리면 오류입니다(폴백 없음).
         * @details 층은 `_root`(파생 데이터 루트 `<프로젝트>/Saved` 기준 폴더)나 `_rootEnvironment`(머신마다 다른 절대 경로를 담은 환경 변수) 중 하나를 쓴다.
         *          환경 변수가 없거나 비었으면 그 층은 없는 것이다(로그 `DDC.SharedLayerOff` — 층은 선택 항목이지 필수 값이 아니다).
         *          쓰기 가능 층은 정리 규칙 `_unusedFileAgeDays` · `_maxSizeGB`(둘 다 1 이상)가 필수다.
         */
        [[nodiscard]] static expected<DerivedDataCache> loadFromConfig();

        /**
         * @brief 키의 출력을 앞 층부터 찾아 @p outBytes 에 담습니다. 머리의 서술이 @p keyDescription 과 같고 본문 해시가 맞을 때만 true 이고, 뒤 층 히트는 앞의 쓰기 가능 층에 채웁니다.
         * @details 이미 메모리에 드는 작은 출력(셰이더 바이트코드 · 임포트 결과 묶음)의 자리다. 큰 출력(쿠킹 팩)은 파일 꼴을 쓴다 — 통째로 들지 않는다.
         */
        [[nodiscard]] bool find( uint64 key, string_view keyDescription, vector<uint8>& outBytes ) const;
        /**
         * @brief `find` 의 파일 꼴입니다 — 본문을 조각으로 흘려 @p outputPath 에 원자적으로 씁니다(부모 폴더를 만든다). 본문 해시가 맞을 때만 바꿔 끼우고 true 입니다.
         * @details 출력을 메모리로 들지 않는다(쿠킹 출력 · 큰 팩). 어긋난 항목은 @p outputPath 를 건드리지 않는다.
         */
        [[nodiscard]] bool find( uint64 key, string_view keyDescription, string_view outputPath ) const;
        /** @brief 키의 출력을 쓰기 가능 층 모두에 씁니다. 읽기 전용 층은 건드리지 않습니다. 쓰기 가능 층 하나라도 못 쓰면 false 입니다. */
        [[nodiscard]] bool store( uint64 key, string_view keyDescription, const vector<uint8>& bytes ) const;
        /** @brief `store` 의 파일 꼴입니다 — 파일 @p sourcePath 를 조각으로 읽어 항목으로 씁니다(메모리로 들지 않는다). */
        [[nodiscard]] bool store( uint64 key, string_view keyDescription, string_view sourcePath ) const;
        /**
         * @brief 정리 규칙이 있는 층마다 오래 안 쓴 항목(마지막 쓰기 시각 — 찾기 히트가 갱신한다)을 지우고, 합계가 상한을 넘으면 오래된 것부터 더 지웁니다.
         * @details 쿠킹 시작에 한 번 부른다. 다른 프로세스가 읽는 중이라 못 지운 항목은 건너뛴다(다음 정리가 지운다). 지운 항목 찾기는 미스일 뿐이다.
         * @return 지운 항목 수
         */
        uint32 evictUnusedEntries() const;
        /** @brief 항목 파일 @p path 를 검증합니다. Valid 면 @p outBytes 에 본문을 담습니다. 로그를 남기지 않습니다. */
        [[nodiscard]] static DerivedDataEntryState validateEntry( const AbsolutePath& path, string_view keyDescription, vector<uint8>& outBytes );
        /** @brief 층 @p layerIndex 의 키 파일 경로입니다. */
        [[nodiscard]] AbsolutePath      makePath( size_t layerIndex, uint64 key ) const;
        const vector<DerivedDataLayer>& getLayers() const { return _listLayer; }

    private:
        vector<DerivedDataLayer> _listLayer;
    };
} // namespace sw
