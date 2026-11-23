/**
 * @file FileServeContentSource.h
 * @brief 장치가 PC 의 쿠킹한 팩 폴더를 HTTP 로 받아 읽는 콘텐츠 소스입니다(개발 전용 — `py -3 -m Scripts fileserve`).
 * @details 다시 구운 팩을 APK 를 다시 묶지 않고 장치에서 읽는다 — ezEngine Fileserve · 언리얼 쿡 데이터 스트리밍(Zen)과 같은 자리다.
 *          받은 파일은 앱 전용 폴더의 거울(`mirrorRoot`)에 두고 디스크 소스로 읽는다. 거울 곁 도장(`<파일>.fsstamp` — 크기와 PC 의 수정 시각)이 서버의 것과 같으면 다시 받지 않는다.
 *          파일은 1 MiB 이하 구간으로 나눠 받아(`/file?offset=&size=` — 장치 HTTP 클라이언트의 본문 상한) 임시 파일 + 원자 교체로 쓴다. 걸린 폴더 밖 경로는 없는 파일이다.
 *          서버 응답은 믿지 않는다: 목록 이름에 구분자 · `..` 가 있으면 그 목록을 거절하고(거울 밖에 쓰지 않는다), 숫자 칸이 틀리면 없는 파일로 본다.
 *          전송은 엔진의 `IHTTPClient` 계약이다(장치는 개발 전용 평문 클라이언트 `PlainHTTPClient` — `Net/Dev`, 테스트는 가짜). 기동(`AndroidMain`)이 `-fileserve=<주소>` 일 때만 건다(배포 구성에는 없다).
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/ContentFileSource.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class IHTTPClient;

    /**
     * @class FileServeContentSource
     * @brief 파일 시스템 경로 @p mountRoot 아래를 Fileserve 서버의 루트로 옮겨 읽습니다. 파일 머리말 참고.
     */
    class SW_API FileServeContentSource final : public IContentFileSource
    {
    public:
        /** @brief 한 번에 받는 구간의 끝입니다 — 장치 HTTP 클라이언트의 응답 본문 상한(1 MiB)과 서버의 `MAX_CHUNK_BYTES` 와 같다. */
        static constexpr uint64 kChunkBytes = uint64{ 1024 } * 1024;
        /** @brief 서버가 알린 파일 크기의 상한입니다 — 그 위는 없는 파일로 보고 받지 않는다(거울 디스크를 채우지 않게). 가장 큰 쿠킹 팩은 1.3 GB 다. */
        static constexpr uint64 kMaxFileBytes = uint64{ 8 } * 1024 * 1024 * 1024;
        /** @brief 거울 파일 곁 도장의 확장자입니다. */
        static constexpr const utf8* kStampExtension = ".fsstamp";

        /**
         * @param client 호출자가 소유한다. 이 소스보다 오래 살아야 한다. 여러 스레드가 불러도 되는 것이어야 한다(`PlainHTTPClient` 는 요청마다 연결 하나라 상태가 없다).
         * @param baseURL 서버 주소(`http://127.0.0.1:1112` — 끝 `/` 없음)입니다.
         * @param mountRoot 엔진이 묻는 폴더 경로(`ProcessRoots::_packs`)입니다.
         * @param mirrorRoot 받은 파일을 둘 쓰기 가능한 폴더(앱 전용 폴더 아래)입니다.
         */
        FileServeContentSource( IHTTPClient& client, string_view baseURL, string_view mountRoot, string_view mirrorRoot );

        /** @brief 질의 값 글자를 퍼센트 인코딩합니다(RFC 3986 의 예약하지 않은 글자와 `/` 는 그대로). */
        static string encodeQueryValue( string_view text );

        AsyncFileHandle open( string_view path ) const override;
        bool            isFile( string_view path ) const override;
        bool            isFolder( string_view path ) const override;
        bool            collectFiles( string_view folder, vector<string>& outListPath ) const override;
        bool            readFile( string_view path, vector<uint8>& outBytes ) const override;
        bool            readTextFile( string_view path, string& outText ) const override;
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const override;
        AsyncReadHandle readRangeAsync( AsyncFileIO& io, string_view path, uint64 offset, uint64 size, AsyncIOPriority priority,
                                        const AsyncReadCompleteDelegate& onComplete ) const override;

        /** @brief 이 소스가 받은 구간 수입니다(진단 · 테스트 — 도장이 맞으면 늘지 않는다). */
        const DevCounter& getReceivedChunkCount() const { return _receivedChunkCount; }

    private:
        /** @brief 서버가 답한 자원의 종류입니다. */
        enum class RemoteKind : uint8
        {
            Missing,
            File,
            Folder,
        };

        /** @brief `/stat` 의 답입니다. */
        struct RemoteStat
        {
            uint64     _sizeBytes{ 0 };
            uint64     _modifiedNanos{ 0 };
            RemoteKind _kind{ RemoteKind::Missing };
        };

        /** @brief 경로 @p path 를 서버 상대 경로로 옮깁니다. 걸린 폴더 밖이면 false 입니다. */
        [[nodiscard]] bool tryMapToRelative( string_view path, string& outRelative ) const;
        /** @brief 상대 경로의 서버 상태입니다(이 실행에서 한 번만 묻는다). */
        RemoteStat findRemoteStat( const string& relative ) const;
        /** @brief 경로 @p path 를 거울에 받아 둔 경로를 @p outMirrorPath 에 담습니다. 없는 파일 · 받지 못함이면 false 입니다. */
        [[nodiscard]] bool ensureMirrored( string_view path, string& outMirrorPath ) const;
        /** @brief 파일 하나를 구간으로 받아 @p mirrorPath 에 원자적으로 씁니다. */
        [[nodiscard]] bool downloadFile( const string& relative, uint64 sizeBytes, const string& mirrorPath ) const;
        /** @brief GET 하나를 보내고 성공 본문을 담습니다. 404 는 @p outbMissing 을 켜고 false 입니다. */
        [[nodiscard]] bool fetch( const string& url, vector<uint8>& outBody, bool& outbMissing ) const;

    private:
        IHTTPClient&                                             _client;
        string                                                   _baseURL;
        string                                                   _mountRoot;  ///< 구분자를 `/` 로 맞추고 끝 구분자를 뗀 경로
        string                                                   _mirrorRoot; ///< 구분자를 `/` 로 맞추고 끝 구분자를 뗀 경로
        DiskContentFileSource                                    _disk;
        mutable mutex                                            _mutex;
        mutable unordered_map<string, RemoteStat> _mapRemoteStat SW_GUARDED_BY( _mutex ); ///< shortcut: 실행 동안 PC 쪽 변경을 다시 묻지 않는다 — 실행 중 다시 굽기를 받으려면 핫 리로드 신호로 지운다
        mutable unordered_map<string, string> _mapMirrored       SW_GUARDED_BY( _mutex ); ///< 상대 경로 → 받아 둔 거울 경로
        mutable DevCounter                                       _receivedChunkCount;
    };
} // namespace sw
