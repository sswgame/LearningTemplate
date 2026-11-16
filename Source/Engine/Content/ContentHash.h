/**
 * @file ContentHash.h
 * @brief 쿠킹 판정이 쓰는 파일 내용 해시(CR 을 뺀 바이트의 FNV-1a 64)와 그 캐시입니다. 셰이더 쿠킹 스탬프와 쿠킹 기록(`CookRecord`)이 같은 값을 씁니다.
 * @details 판정은 파일 시각이 아니라 내용이다 — 이 저장소는 `git pull` 이 소스와 산출물의 시각을 임의의 순서로 바꾼다.
 *          캐시는 파일 크기와 쓰기 시각이 그대로일 때만 다시 읽지 않는다(셰이더 요청 하나가 같은 소스를 여러 번 본다 — 한 번 여는 데 약 200µs).
 *          셰이더 스탬프를 쓰는 쪽과 판정하는 쪽(`ShaderCookStamp.cpp`)이 같은 함수를 쓴다.
 *          **소유.** 캐시는 서비스 범위의 서브시스템 `ContentHashCache` 가 갖고(`EngineSubsystemSet`), 창구(`ContentHash::computeFile`)는 서비스 표의 칸으로 찾는다.
 *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 언리얼 DDC 의 파일 해시 캐시(`FFileHashCache`)가 엔진 쪽 객체에 있는 자리.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @class ContentHash
     * @brief 파일 · 바이트의 내용 해시입니다. 0 은 "읽지 못함" 입니다(내용 해시가 0 인 파일은 사실상 없다).
     */
    class SW_API ContentHash
    {
    public:
        /** @brief 바이트의 내용 해시입니다(CR 을 빼고 FNV-1a 64). 사본을 만들지 않고 한 번에 훑는다. */
        static uint64 computeBytes( const uint8* pData, size_t size );
        /** @brief 바이트의 내용 해시입니다. */
        static uint64 computeBytes( const vector<uint8>& bytes ) { return computeBytes( bytes.data(), bytes.size() ); }
        /**
         * @brief 파일의 내용 해시입니다. 크기와 쓰기 시각이 지난번과 같으면 파일을 열지 않습니다(`ContentHashCache`). 읽지 못하면 0 입니다. 엔진 서비스가 없으면 멈춘다.
         * @details "파일이 바뀌었나" 를 묻는 쪽(핫 리로드의 자기 저장 거르기 · 미리보기 자동 갱신)은 도장이 아니라 이 값을 비교한다 — 같은 틱 · 같은 크기의 다시 쓰기도 놓치지 않는다.
         */
        static uint64 computeFile( string_view absPath );
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
     *          **낡음 판정 두 길.** 이 프로세스의 쓰기는 파일 계층의 쓰기 관찰자(`IFileWriteObserver`)로 그 자리에서 받는다 — 쓴 뒤의 도장을 "자기 쓰기" 표로 남기고,
     *          다음 조회가 그 도장 그대로면 읽은 해시를 바로 캐시한다(시간 창을 기다리지 않는다). 바깥 도구(git · 외부 편집기 · 자식 프로세스)의 쓰기만 시간 창
     *          (`kRacyWindowMs`, git 의 racy-git 과 같은 자리)으로 거른다 — 같은 틱에 같은 크기로 다시 쓰면 도장이 같아 알 길이 없다.
     *          shortcut: 이 프로세스가 쓴 바로 그 틱에 바깥 도구가 같은 파일을 같은 크기로 다시 쓰면 다음 도장 변화까지 이 프로세스의 해시가 남는다 — 두 프로세스가 한 파일을
     *          같은 틱에 쓰는 순서 없는 경합이다. 그런 흐름이 생기면 자기 쓰기 표에도 시간 창을 건다.
     */
    class SW_API ContentHashCache final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "ContentHashCache";

        ContentHashCache();

        // ISubsystem — 파일 계층에 쓰기 관찰자로 붙는다(항목은 처음 묻는 파일부터 채운다).
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief `ContentHash::computeFile` 의 본문입니다. */
        uint64 computeFile( string_view absPath ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief `ContentHash::clearCache` 의 본문입니다. */
        void clear() SW_EXCLUDES_LOCK( _mutex );
        /** @brief 이 프로세스가 @p filePath 를 바꿨습니다 — 그 항목을 "자기 쓰기" 표로 바꿉니다(아무 스레드, 쓰기 관찰자 `WriteObserver` 가 부른다). */
        void recordOwnWrite( string_view filePath ) SW_EXCLUDES_LOCK( _mutex );

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

        mutex                                  _mutex;
        unordered_map<string, Entry> _mapEntry SW_GUARDED_BY( _mutex );
        uint64 _writeSerial                    SW_GUARDED_BY( _mutex ); ///< 자기 쓰기 알림마다 오른다
        WriteObserver                          _writeObserver;          ///< 파일 계층에 건 쓰기 관찰자
        uint8                                  _bObserving : 1;         ///< 쓰기 관찰자를 걸었다(내릴 때 뗀다)

        SW_SERVICE_SLOT( ContentHashCache, Required );
    };
} // namespace sw
