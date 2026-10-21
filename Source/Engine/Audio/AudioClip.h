/**
 * @file AudioClip.h
 * @brief 믹서가 읽는 클립(float 샘플)과, 경로별 클립 캐시(`AudioClipCache`)입니다.
 * @details 디코드는 워커에서 한다(`AudioClipDecoder`는 WAV 와 OGG 를 처리하고, 백엔드가 등록하는 대체 디코더는 Windows 의 Media Foundation 이다).
 *          디코드 결과는 float 로 바꿔 캐시에 넣는다. 믹서 스레드는 캐시에서 **찾기만** 하고(`findClip`) 디코드를 기다리지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AudioPcm;

    class AudioGraph;
    class AudioStreamSource;
    class TaskArgs;

    /**
     * @struct AudioClipData
     * @brief 재생할 소리 하나입니다. float32 교차 샘플이며, 채널은 1(모노)이나 2(스테레오)입니다. 그보다 많으면 읽을 때 스테레오로 줄입니다.
     * @details 긴 소리는 샘플 배열 대신 압축 바이트(`_pStream`)를 들고 보이스가 재생하는 만큼만 풉니다(`AudioStream.h`). 그때 `_listSample` 은 비어 있고
     *          `_frameCount` · `_sampleRate` · `_channelCount` 는 전체 소리의 값입니다.
     *          소리 그래프(`*.audiograph.xml`)도 이 형식이다. 샘플 대신 굳힌 그래프(`_pGraph`)를 들고, 보이스마다 실행 상태를 만들어 재생하는 만큼 렌더한다
     *          (모노 · 48 kHz, `_frameCount` 는 그래프 길이, `Graph/AudioGraph.h`).
     */
    struct SW_API AudioClipData
    {
        vector<float32>                     _listSample;        ///< 채널 교차 샘플([-1, 1]). 스트리밍 클립은 비어 있다.
        shared_ptr<const AudioStreamSource> _pStream{};         ///< 스트리밍 클립의 압축 바이트다(통째로 푼 클립은 nullptr).
        shared_ptr<const AudioGraph>        _pGraph{};          ///< 소리 그래프다(파일 클립은 nullptr).
        uint32                              _frameCount{ 0 };   ///< 채널당 샘플 수다.
        uint32                              _sampleRate{ 0 };   ///< 원본 샘플레이트(Hz)
        uint16                              _channelCount{ 0 }; ///< 1 또는 2 다.

        /** @brief 스트리밍 클립인지입니다. */
        bool isStreamed() const { return _pStream != nullptr; }
        /** @brief 소리 그래프인지입니다. */
        bool isGraph() const { return _pGraph != nullptr; }

        /** @brief 길이(초)입니다. */
        float32 getDurationSeconds() const { return _sampleRate == 0 ? 0.0f : static_cast<float32>( _frameCount ) / static_cast<float32>( _sampleRate ); }

        /**
         * @brief 디코드한 PCM(정수 8, 16, 24, 32 비트와 float 32 비트)을 float 클립으로 바꿉니다. 3 채널 이상은 채널 마스크(WAV 순서)의 배치로
         *        명시 다운믹스(ITU-R BS.775, `AudioDownmix`)해 스테레오로 줄입니다.
         * @return 형식이 비었거나(채널이나 샘플레이트가 0) 비트 수를 모르거나, 3 채널 이상인데 배치를 모르면(높이 채널, 마스크 비트 부족) false 를 반환합니다.
         */
        [[nodiscard]] static bool convertPcm( const AudioPcm& pcm, AudioClipData& outClip );
        /** @brief 채널을 평균해 모노 샘플로 바꿉니다(립싱크 분석에 씁니다). 스트리밍 클립은 샘플이 없어 빈 결과입니다. */
        void copyMonoSamples( vector<float32>& outListSample ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 클립 캐시에서 경로 하나의 상태입니다. */
    enum class AudioClipStatus : uint8
    {
        Missing = 0, ///< 요청한 적이 없다.
        Loading,     ///< 워커가 디코드 중이다
        Ready,       ///< 쓸 수 있다
        Failed,      ///< 디코드나 읽기에 실패했다(다시 요청하면 다시 시도한다).
    };

    /** @brief 공통 디코더가 다루지 않는 형식(MP3 등)을 디코드하는 백엔드 함수입니다. @p path 는 리소스 상대 경로이거나 절대 경로입니다. */
    using AudioFallbackDecodeFunction = bool ( * )( string_view path, AudioPcm& outPcm );

    /**
     * @class AudioClipCache
     * @brief 경로별로 디코드한 클립을 보관하는 캐시입니다. 모든 함수는 어느 스레드에서 호출해도 됩니다.
     * @details 요청(`requestClip`)은 엔진 태스크가 있으면 워커에서 디코드하고, 없으면(테스트, 헤드리스) 호출한 자리에서 바로 디코드한다.
     *          같은 경로를 두 번 요청해도 디코드는 한 번이다. 클립은 `shared_ptr<const>` 라서 재생 중인 보이스가 참조하는 클립은 `clear` 뒤에도 살아 있다.
     * @note 워커 디코드 태스크는 이 캐시를 가리킨다 — 소멸자가 띄운 태스크가 모두 끝날 때까지 기다린다(기다리는 동안 다른 태스크를 돕는다).
     */
    class SW_API AudioClipCache
    {
    public:
        AudioClipCache();
        /** @brief 이 캐시가 띄운 디코드 태스크가 모두 끝날 때까지 기다립니다 — 태스크가 해제된 캐시에 결과를 쓰지 않게. */
        ~AudioClipCache();

        AudioClipCache( const AudioClipCache& )            = delete;
        AudioClipCache& operator=( const AudioClipCache& ) = delete;

        /** @brief 이 초 이상 긴 소리는 통째로 풀지 않고 스트리밍합니다(기본 `kDefaultStreamMinSeconds`). 0 이면 스트리밍할 수 있는 형식은 모두 스트리밍합니다. 이미 캐시된 클립은 바뀌지 않습니다. */
        void setStreamMinSeconds( float32 seconds ) { _streamMinSeconds = seconds; }
        /** @brief 지금 스트리밍 기준(초)입니다. */
        float32 getStreamMinSeconds() const { return _streamMinSeconds; }

        /** @brief 공통 디코더가 못 읽는 형식에 쓸 대체 디코더를 겁니다(nullptr 이면 없음). */
        void setFallbackDecoder( AudioFallbackDecodeFunction pFunction ) { _pFallbackDecoder = pFunction; }

        /** @brief 스트리밍 기준의 기본값(초)입니다. 48 kHz 스테레오 float 로 풀면 약 7.7 MB 에 해당합니다. */
        static constexpr float32 kDefaultStreamMinSeconds = 20.0f;

        /** @brief 준비된 클립입니다. 아직 없거나 실패했으면 nullptr 입니다. 디코드하지 않습니다. */
        shared_ptr<const AudioClipData> findClip( const hashed_string& path ) const;

        /** @brief 경로의 상태입니다. */
        AudioClipStatus getStatus( const hashed_string& path ) const;

        /** @brief 지금 디코드해 캐시에 넣고 반환합니다. 이미 있으면 그것을 반환합니다. 읽기나 디코드에 실패하면 nullptr 를 반환합니다. */
        shared_ptr<const AudioClipData> loadClip( const hashed_string& path );

        /**
         * @brief 디코드를 요청합니다. 엔진 태스크가 있으면 워커에서, 없으면 지금 디코드합니다. 이미 준비됐거나 디코드 중이면 아무것도 하지 않습니다.
         * @details 실패한 경로는 다시 요청하면 다시 시도한다. 이 호스트가 읽지 않는 종류(전용 서버의 오디오, `ResourcePaths::isExcludedForHost`)는
         *          디코드하지 않고 경고 없이 실패로 둔다 — 서버 패키지에는 오디오가 없다(언리얼 전용 서버가 사운드 웨이브를 쿠킹에서 빼는 것과 같다).
         */
        void requestClip( const hashed_string& path );

        /**
         * @brief 그 경로(구분자 · 대소문자 무시)를 이 캐시가 준비해 두었으면 캐시의 이름을 @p outPath 에 돌려줍니다. 이름을 intern 하지 않습니다(핫 리로드가 바뀐 파일마다 묻는 길).
         */
        [[nodiscard]] bool findReadyPath( string_view path, hashed_string& outPath ) const;
        /**
         * @brief 준비된 경로를 지금 다시 디코드해 바꿉니다(소리 그래프 핫 리로드). 재생 중인 보이스는 옛 클립을 끝까지 씁니다(`shared_ptr`).
         * @return 다시 읽기에 실패하면 오류를 남기고 false 이며 옛 클립을 그대로 둡니다(읽을 수 없는 파일로 소리를 지우지 않는다).
         */
        [[nodiscard]] bool reloadClip( const hashed_string& path );
        /** @brief 만든 클립을 경로 이름으로 넣습니다(테스트, 절차적 소리). 같은 이름이 있으면 교체합니다. */
        void addClip( const hashed_string& path, shared_ptr<const AudioClipData> pClip );

        /** @brief 캐시를 비웁니다. 디코드 중인 요청의 결과는 도착하면 다시 들어갑니다. */
        void clear();

    private:
        /** @brief 바이트를 읽어 디코드합니다(리소스 → 파일 → 대체 디코더 순). 소리 그래프 파일이면 `decodeGraph` 로 넘깁니다. */
        shared_ptr<const AudioClipData> decodeClip( const hashed_string& path ) const;
        /** @brief 소리 그래프 파일을 읽어 굳히고 그래뉼러 소스 클립을 풉니다. 읽기 · 검사 · 소스 실패면 오류를 남기고 nullptr 입니다. */
        shared_ptr<const AudioClipData> decodeGraph( const hashed_string& path ) const;
        /** @brief 워커 태스크 본문입니다. 인자는 경로(문자열) 하나입니다. */
        void decodeClipTask( const TaskArgs& args );
        /** @brief 디코드 결과를 캐시에 적습니다. */
        void storeResult( const hashed_string& path, shared_ptr<const AudioClipData> pClip );

    private:
        /** @brief 캐시 엔트리 하나입니다. */
        struct Entry
        {
            shared_ptr<const AudioClipData> _pClip{};
            AudioClipStatus                 _status{ AudioClipStatus::Missing };
        };

        unordered_map<hashed_string, Entry, hashed_string::HashFunc> _mapPathToEntry;     /**< 경로에서 엔트리로 가는 맵입니다. */
        AudioFallbackDecodeFunction                                  _pFallbackDecoder;   /**< 대체 디코더입니다(없으면 nullptr). */
        float32                                                      _streamMinSeconds;   /**< 이보다 긴 소리는 스트리밍합니다. */
        mutable mutex                                                _mutex;              /**< `_mapPathToEntry` 를 지킵니다. */
        atomic<uint32>                                               _pendingDecodeCount; /**< 띄웠지만 아직 끝나지 않은 워커 디코드 태스크 수입니다(소멸자가 0 을 기다린다). */
    };
} // namespace sw
