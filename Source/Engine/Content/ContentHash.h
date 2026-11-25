/**
 * @file ContentHash.h
 * @brief 쿠킹 판정이 쓰는 파일 내용 해시(원시 바이트의 FNV-1a 64)와 그 캐시입니다. 쿠킹 기록(`CookRecord`)이 같은 값을 씁니다.
 * @details 판정은 파일 시각이 아니라 내용이다 — 이 저장소는 `git pull` 이 소스와 산출물의 시각을 임의의 순서로 바꾼다.
 *          캐시는 파일 크기와 쓰기 시각이 그대로일 때만 다시 읽지 않는다(셰이더 요청 하나가 같은 소스를 여러 번 본다 — 한 번 여는 데 약 200µs).
 *          기록을 쓰는 쪽과 판정하는 쪽이 같은 함수를 쓴다.
 *          **소유.** 캐시는 서비스 범위의 서브시스템 `ContentHashCache` 가 갖고(등록 줄 — `EngineSubsystemRegistration.h`), 창구(`ContentHash::computeFile`)는 서비스 표의 칸으로 찾는다.
 *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 언리얼 DDC 의 파일 해시 캐시(`FFileHashCache`)가 엔진 쪽 객체에 있는 자리.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

namespace sw
{
    /**
     * @class ContentHash
     * @brief 파일 · 바이트의 내용 해시입니다. 0 은 "읽지 못함" 입니다(내용 해시가 0 인 파일은 사실상 없다).
     */
    class SW_API ContentHash
    {
    public:
        /**
         * @brief 바이트의 내용 해시입니다(원시 바이트의 FNV-1a 64).
         * @details 예전에는 CR 을 건너뛰어 바이너리에서 그 자리만 다른 두 파일이 같은 해시였다(결함). 줄 끝은 `.gitattributes`(`* text=auto eol=lf`)가 맞추므로
         *          무엇이 텍스트인지를 해시가 따로 알지 않는다.
         */
        static uint64 computeBytes( const uint8* pData, size_t size );
        /** @brief 조각으로 세는 내용 해시의 첫 상태입니다. `appendBytes( kInitialState, …)` 를 이어 부른 값이 이어 붙인 바이트의 `computeBytes` 와 같다(팩 스트림 쓰기). */
        static constexpr uint64 kInitialState = HashUtil::kFNVOffset64;
        /** @brief 상태 @p state 에 @p size 바이트를 더한 내용 해시 상태입니다. */
        static uint64 appendBytes( uint64 state, const uint8* pData, size_t size );
        /**
         * @brief 파일을 조각(`kStreamChunkBytes`)으로 읽어 내용 해시를 셉니다 — 캐시를 보지 않고, 파일을 한 벡터로 들지 않는다(큰 팩 · 모드 팩).
         * @return 열거나 읽지 못하면 오류입니다. 빈 파일은 `kInitialState` 다.
         */
        [[nodiscard]] static expected<uint64> computeFileStream( const AbsolutePath& absPath, uint64* pOutByteCount = nullptr );
        /** @brief `computeFileStream` 이 한 번에 읽는 바이트입니다. */
        static constexpr uint64 kStreamChunkBytes = 4 * constant::memory::kBytesPerMegabyte;
        /** @brief 바이트의 내용 해시입니다. */
        static uint64 computeBytes( const vector<uint8>& bytes ) { return computeBytes( bytes.data(), bytes.size() ); }
        /**
         * @brief 파일의 내용 해시입니다. 크기와 쓰기 시각이 지난번과 같으면 파일을 열지 않습니다(`ContentHashCache`). 읽지 못하면 0 입니다. 엔진 서비스가 없으면 멈춘다.
         * @details "파일이 바뀌었나" 를 묻는 쪽(핫 리로드의 자기 저장 거르기 · 미리보기 자동 갱신)은 도장이 아니라 이 값을 비교한다 — 같은 틱 · 같은 크기의 다시 쓰기도 놓치지 않는다.
         */
        static uint64 computeFile( const AbsolutePath& absPath );
        /**
         * @brief @p stamp 를 열쇠로 내용 값을 캐시해도 되는지 — 쓴 지 시간 창(2 초)이 지났는지입니다.
         * @details git 의 racy-git 과 같은 결함이다: 같은 틱 안에 같은 크기로 다시 쓴 파일은 (크기, 시각) 도장이 같아 옛 값이 돌아왔다(셰이더 쿠킹 판정 테스트가 전체 실행에서만 졌다).
         *          막 쓴 파일은 매번 읽고 자리 잡은 파일만 캐시한다. 내용 해시 캐시와 트리 훑기 캐시(`AssetScanCache`)가 이 판정 하나를 쓴다.
         */
        static bool isStampStable( const FileStamp& stamp );
        /** @brief 캐시를 비웁니다(수동 리로드의 "전부 다시 본다"). 이 프로세스의 쓰기는 쓰기 관찰자가 이미 받으므로 테스트가 부를 일은 없다. */
        static void clearCache();
    };
} // namespace sw

namespace sw
{
    /**
     * @class ContentHashCache
     * @brief 경로 → (파일 크기 · 쓰기 시각, 내용 해시) 캐시입니다(서비스 범위의 서브시스템). `ContentHash` 창구만 부릅니다.
     * @details 셰이더 컴파일 · 쿠킹이 워커에서도 부르므로 안에서 잠근다. 항목은 기동 뒤 첫 셰이더 반사 · 쿠킹이 채우고 서비스 범위가 내려갈 때 돌아간다.
     *          **낡음 판정.** 이 프로세스의 쓰기는 파일 계층의 쓰기 관찰자(`IFileWriteObserver`)로 그 자리에서 받는다 — 쓴 뒤의 도장을 "자기 쓰기" 표로 남겨, 같은 틱 ·
     *          같은 크기의 다시 쓰기로 도장이 그대로여도 옛 해시를 돌려주지 않는다. 캐시는 자기 쓰기든 바깥 도구(git · 외부 편집기 · 자식 프로세스)의 쓰기든 쓴 지
     *          시간 창(`kRacyWindowMs`, git 의 racy-git 과 같은 자리)이 지난 도장만 남긴다 — 이 프로세스가 쓴 그 틱에 바깥 도구가 같은 크기로 다시 써도 창 안에서는 매번 읽는다.
     *          **영속.** 쿠킹 프로세스가 `load` 로 파일(`makeLocalCachePath`)을 싣고 끝에 `save` 한다 — 변경 없는 두 번째 쿠킹이 입력을 도장만 보고 넘긴다
     *          (git 색인 · 언리얼 `FFileHashCache` 자리). 저장 시각의 시간 창 안에 고쳐진 도장은 싣지 않는다(위 시간 창을 파일로 넓힌 것 — 저장 뒤 같은 틱에
     *          바깥 도구가 같은 크기로 다시 쓰면 도장으로 알 길이 없다). 파일이 없거나 판이 다르면 빈 캐시다 — 캐시라 폴백이 아니다.
     */
    class SW_API ContentHashCache final : public Subsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "ContentHashCache";

        ContentHashCache();

        // Subsystem — 파일 계층에 쓰기 관찰자로 붙는다(항목은 처음 묻는 파일부터 채운다).
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief `ContentHash::computeFile` 의 본문입니다. */
        uint64 computeFile( const AbsolutePath& absPath ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 영속 캐시 파일의 판입니다. 항목 모양이나 해시 규칙을 바꾸면 올린다 — 다른 판의 파일은 빈 캐시로 읽는다. */
        static constexpr uint32 kCacheFormatVersion = 0;
        /** @brief 로컬 영속 캐시 파일(`<프로젝트>/Saved/ContentHashCache.bin`)입니다. */
        static AbsolutePath makeLocalCachePath();
        /**
         * @brief @p cachePath 의 항목을 싣고, 이 경로를 `save` 의 대상으로 둡니다. 이미 있는 항목(이 프로세스가 구한 것)은 덮지 않습니다.
         * @return 파일이 없거나 판이 다르면 빈 채로 성공이고, 읽지 못했거나 깨졌으면 오류입니다(그래도 저장 대상은 정해진다 — 다음 `save` 가 새로 쓴다).
         */
        [[nodiscard]] expected<void> load( const AbsolutePath& cachePath ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 해시가 정해진 항목을 `load` 의 경로에 씁니다(원자적 쓰기 — 임시 파일에 쓰고 바꿔 끼운다). `load` 를 부르지 않았으면 오류입니다. */
        [[nodiscard]] expected<void> save() const SW_EXCLUDES_LOCK( _mutex );
        /** @brief 이 캐시가 해시하려고 읽은 파일 바이트의 누적입니다(변경 없는 쿠킹이 0 인지 재는 값). */
        uint64 getHashedByteCount() const { return _hashedByteCount.load(); }
        /** @brief `ContentHash::clearCache` 의 본문입니다. */
        void clear() SW_EXCLUDES_LOCK( _mutex );
        /** @brief 이 프로세스가 @p filePath 를 바꿨습니다 — 그 항목을 "자기 쓰기" 표로 바꿉니다(아무 스레드, 쓰기 관찰자 `WriteObserver` 가 부른다). */
        void recordOwnWrite( const AbsolutePath& filePath ) SW_EXCLUDES_LOCK( _mutex );

    private:
        /** @brief 파일 계층에 거는 쓰기 관찰자입니다 — 서브시스템이 창구를 함께 상속하지 않고 멤버로 든다(기반은 하나). */
        class WriteObserver final : public IFileWriteObserver
        {
        public:
            explicit WriteObserver( ContentHashCache& owner );
            void recordFileWritten( string_view filePath ) override;

        private:
            ContentHashCache& _owner;
        };

        /** @brief 항목 하나입니다. 그 해시를 구했을 때의 파일 크기와 쓰기 시각을 함께 둡니다. */
        struct Entry
        {
            FileStamp _stamp{};
            uint64    _hash{ 0 };        ///< 0 은 "자기 쓰기 표" — 이 도장의 내용은 이 프로세스가 썼고 해시는 다음 조회가 읽어 채운다
            uint64    _writeSerial{ 0 }; ///< 이 항목을 만든 자기 쓰기의 번호. 읽는 사이에 다른 자기 쓰기가 끼면 번호가 달라 읽은 해시를 버린다
        };

        /** @brief 캐시 열쇠입니다(구분자를 맞춘 경로 — 쓰기 입구와 조회가 같은 열쇠를 쓴다). */
        static string makeKey( string_view path );

        mutable mutex                               _mutex;
        unordered_map<string, Entry> _mapEntry      SW_GUARDED_BY( _mutex );
        uint64 _writeSerial                         SW_GUARDED_BY( _mutex );    ///< 자기 쓰기 알림마다 오른다
        AbsolutePath _cachePath                     SW_GUARDED_BY( _mutex );    ///< `load` 가 정한 영속 파일 경로(빈 글이면 `save` 할 곳이 없다)
        atomic<uint64>                              _hashedByteCount;           ///< 해시하려고 읽은 바이트 누적(워커가 함께 더한다)
        WriteObserver                               _writeObserver;             ///< 파일 계층에 건 쓰기 관찰자
        PlatformFileUtil::WriteObserverRegistration _writeObserverRegistration; ///< 쓰기 관찰자 등록 — 내릴 때 뗀다(관찰자는 하나라 둘째는 단언)

        SW_SERVICE_SLOT( ContentHashCache, Required );
    };
} // namespace sw
