/**
 * @file AudioClip.h
 * @brief 믹서가 읽는 클립(float 샘플)과, 경로 → 클립 캐시(`AudioClipStore`)입니다.
 * @details 디코드(`AudioClipDecoder` — WAV · OGG, 백엔드가 걸어 주는 대체 디코더 — Windows 의 Media Foundation)는 워커에서 하고, 결과를 float 로
 *          바꿔 캐시에 넣습니다. 믹서 스레드는 캐시에서 **찾기만** 합니다(`findClip`) — 디코드를 기다리지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AudioPcm;

    class TaskArgs;

    /**
     * @struct AudioClipData
     * @brief 재생할 소리 하나 — float32 교차 샘플입니다. 채널은 1(모노) 또는 2(스테레오)이고, 그보다 많으면 읽을 때 스테레오로 내립니다.
     */
    struct SW_API AudioClipData
    {
        vector<float32> _listSample;        ///< 채널 교차 샘플([-1, 1])
        uint32          _frameCount{ 0 };   ///< 채널당 샘플 수
        uint32          _sampleRate{ 0 };   ///< 원본 샘플레이트(Hz)
        uint16          _channelCount{ 0 }; ///< 1 또는 2

        /** @brief 길이(초)입니다. */
        float32 getDurationSeconds() const { return _sampleRate == 0 ? 0.0f : static_cast<float32>( _frameCount ) / static_cast<float32>( _sampleRate ); }

        /**
         * @brief 디코드한 PCM(정수 8 · 16 · 24 · 32 비트, float 32 비트)을 float 클립으로 바꿉니다. 3 채널 이상은 앞 두 채널(왼쪽 · 오른쪽)로 내립니다.
         * @return 형식이 비었거나(채널 · 샘플레이트 0) 비트 수를 모르면 false 입니다.
         */
        [[nodiscard]] static bool convertPcm( const AudioPcm& pcm, AudioClipData& outClip );
    };
} // namespace sw

namespace sw
{
    /** @brief 클립 캐시에서 경로 하나의 상태입니다. */
    enum class AudioClipStatus : uint8
    {
        Missing = 0, ///< 요청된 적 없다
        Loading,     ///< 워커가 디코드 중이다
        Ready,       ///< 쓸 수 있다
        Failed,      ///< 디코드 · 읽기에 실패했다(다시 요청하면 다시 시도한다)
    };

    /** @brief 공통 디코더가 다루지 않는 형식(MP3 등)을 푸는 백엔드 함수입니다. @p path 는 리소스 상대 · 절대 경로입니다. */
    using AudioFallbackDecodeFunction = bool ( * )( string_view path, AudioPcm& outPcm );

    /**
     * @class AudioClipStore
     * @brief 경로 → 디코드된 클립 캐시입니다. 모든 함수는 어느 스레드에서 불러도 됩니다.
     * @details 요청(`requestClip`)은 엔진 태스크가 있으면 워커에서, 없으면(시험 · 헤드리스) 그 자리에서 디코드합니다. 같은 경로를 두 번 요청해도
     *          디코드는 한 번입니다. 클립은 `shared_ptr<const>` 라 재생 중인 보이스가 쥔 클립은 `clear` 뒤에도 살아 있습니다.
     */
    class SW_API AudioClipStore
    {
    public:
        AudioClipStore();
        ~AudioClipStore();

        AudioClipStore( const AudioClipStore& )            = delete;
        AudioClipStore& operator=( const AudioClipStore& ) = delete;

        /** @brief 공통 디코더가 못 읽는 형식에 쓸 대체 디코더를 겁니다(nullptr 이면 없음). */
        void setFallbackDecoder( AudioFallbackDecodeFunction pFunction ) { _pFallbackDecoder = pFunction; }

        /** @brief 준비된 클립입니다. 아직 없거나 실패했으면 nullptr 입니다. 디코드하지 않습니다. */
        shared_ptr<const AudioClipData> findClip( const hashed_string& path ) const;

        /** @brief 경로의 상태입니다. */
        AudioClipStatus getStatus( const hashed_string& path ) const;

        /** @brief 지금 디코드해 캐시에 넣고 반환합니다(이미 있으면 그것). 읽기 · 디코드에 실패하면 nullptr 입니다. */
        shared_ptr<const AudioClipData> loadClip( const hashed_string& path );

        /**
         * @brief 디코드를 요청합니다 — 엔진 태스크가 있으면 워커에서, 없으면 지금 합니다. 이미 준비됐거나 디코드 중이면 아무것도 하지 않습니다.
         * @details 실패한 경로는 다시 요청하면 다시 시도합니다.
         */
        void requestClip( const hashed_string& path );

        /** @brief 만든 클립을 경로 이름으로 넣습니다(시험 · 절차적 소리). 같은 이름이 있으면 바꿉니다. */
        void addClip( const hashed_string& path, shared_ptr<const AudioClipData> pClip );

        /** @brief 캐시를 비웁니다. 디코드 중인 요청의 결과는 도착하면 다시 들어갑니다. */
        void clear();

    private:
        /** @brief 바이트를 읽어 디코드합니다(리소스 → 파일 → 대체 디코더 순). */
        shared_ptr<const AudioClipData> decodeClip( const hashed_string& path ) const;
        /** @brief 워커 태스크 본문입니다. 인자는 경로(문자열) 하나입니다. */
        void decodeClipTask( const TaskArgs& args );
        /** @brief 디코드 결과를 캐시에 적습니다. */
        void storeResult( const hashed_string& path, shared_ptr<const AudioClipData> pClip );

    private:
        /** @brief 캐시 칸 하나입니다. */
        struct Entry
        {
            shared_ptr<const AudioClipData> _pClip{};
            AudioClipStatus                 _status{ AudioClipStatus::Missing };
        };

        unordered_map<hashed_string, Entry, hashed_string::HashFunc> _mapPathToEntry;   /**< 경로 → 칸입니다. */
        AudioFallbackDecodeFunction                                  _pFallbackDecoder; /**< 대체 디코더입니다(없으면 nullptr). */
        mutable mutex                                                _mutex;            /**< `_mapPathToEntry` 를 지킵니다. */
    };
} // namespace sw
