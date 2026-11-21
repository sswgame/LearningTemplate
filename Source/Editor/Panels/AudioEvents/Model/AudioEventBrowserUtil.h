/**
 * @file AudioEventBrowserUtil.h
 * @brief 오디오 이벤트 창이 그리는 테이블 행을 엔진 상태에서 만드는 순수 로직입니다. ImGui 를 쓰지 않아 단위 테스트가 바로 부릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Audio/AudioEngineState.h"

namespace sw
{
    struct AudioAttenuationDef;
    struct AudioEventDef;
    struct AudioEventLibrary;

    class AudioService;
} // namespace sw

namespace sw::editor
{
    /** @brief 이벤트 속성 테이블의 한 줄입니다(이름 · 값). */
    struct AudioEventFieldRow
    {
        const utf8* _pLabel{ "" }; ///< 현지화한 필드 이름이다(정적 수명).
        string      _value{};      ///< 단위까지 붙인 값이다.
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 보이스 가상화 프로파일러 한 장입니다(실제 · 가상 보이스 수와 상한). */
    struct AudioVoiceProfile
    {
        uint32  _realVoiceCount{ 0 };
        uint32  _virtualVoiceCount{ 0 };
        uint32  _voiceCount{ 0 }; ///< 실제 + 가상 + 클립 대기 보이스다.
        uint32  _peakRealVoiceCount{ 0 };
        uint32  _maxRealVoiceCount{ 0 }; ///< 0 이면 믹서 그래프가 아직 없다.
        uint32  _maxVoiceCount{ 0 };
        uint32  _instanceCount{ 0 };
        uint64  _playedEventCount{ 0 };
        uint64  _droppedEventCount{ 0 };
        float32 _realUsage{ 0.0f };           ///< 실제 보이스 수 / 실제 상한([0, 1]로 자른다).
        float32 _voiceUsage{ 0.0f };          ///< 보이스 수 / 보이스 상한([0, 1]로 자른다).
        bool    _bRealLimitReached{ false };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool    _bVoiceLimitReached{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct AudioEventBrowserUtil
     * @brief 이벤트 목록 거르기, 이벤트 속성 행, 재생 중 인스턴스 행, 보이스 프로파일을 만듭니다.
     */
    struct AudioEventBrowserUtil
    {
        /** @brief @p listName 에서 @p filter 를 (대소문자 무시) 품은 이름만 사전순으로 @p outListName 에 담습니다. 필터가 비면 전부입니다. */
        static void filterEventNames( const vector<hashed_string>& listName, string_view filter, vector<hashed_string>& outListName );
        /**
         * @brief 오디오 서비스가 돌면 그 엔진이 게시한 통계를 @p outStats 에 담고 true 입니다. 서비스가 없거나 서지 않았으면 false 입니다.
         * @details 서비스 · 엔진은 여기(Model)서만 찾는다 — 창과 감시 값은 값만 받는다(MVVM 계약).
         */
        [[nodiscard]] static bool readStats( AudioEngineStats& outStats );
        /**
         * @brief 이벤트 라이브러리가 바뀌었으면(엔진이 새로 떴거나 세대가 바뀜) 이벤트 이름을 @p outListName 에 다시 모으고 true 입니다.
         * @param inoutEngineKey 지난번에 모은 엔진의 표지(비교에만 쓴다 — 창이 값으로 든다)
         * @param inoutGeneration 지난번에 모은 때의 라이브러리 세대
         */
        [[nodiscard]] static bool collectEventNamesIfChanged( const void*& inoutEngineKey, uint32& inoutGeneration, vector<hashed_string>& outListName );
        /** @brief 재생 중인 인스턴스 상태를 @p outListStatus 에 담습니다(비우고 채운다). */
        static void collectInstanceStatuses( vector<AudioInstanceStatus>& outListStatus );
        /** @brief 이벤트 @p eventName 의 설명자 · 감쇠 · 그것을 가진 라이브러리 경로를 찾습니다. 없으면(라이브러리가 언로드됨) false 입니다. */
        [[nodiscard]] static bool findEventDesc( const hashed_string& eventName, AudioEventDef& outEventDesc, hashed_string& outLibrary, AudioAttenuationDef& outAttenuation );
        /** @brief 이벤트 @p eventName 을 한 번 보냅니다(Post Event — 재생 id 는 쓰지 않는다, 끝나면 저절로 멈춘다). */
        static void postEvent( const hashed_string& eventName );

        /** @brief 데이터 파일 @p path 를 연결된 도구로 엽니다(`EditorAssetCommands::openPath`). 여는 도구가 없으면 false 입니다. */
        [[nodiscard]] static bool openDataFile( string_view path );
        /** @brief 이벤트 설명자와 감쇠 프리셋에서 속성 테이블 행을 만듭니다(비우고 채웁니다). 감쇠 이름이 비면 반경 줄은 "2D" 입니다. */
        static void makeEventFieldRows( const AudioEventDef& eventDesc, const AudioAttenuationDef& attenuation, vector<AudioEventFieldRow>& outListRow );
        /**
         * @brief 재생 중 인스턴스 테이블 행을 만듭니다(비우고 채웁니다). @p eventFilter 가 비지 않으면 그 이벤트만 담습니다.
         * @details 남은 시간이 짧은 것이 위에 오고, 끝을 모르는 것(반복 · 클립 대기)은 맨 아래에 재생 id 순으로 둡니다.
         */
        static void makeInstanceRows( const vector<AudioInstanceStatus>& listStatus, const hashed_string& eventFilter, vector<AudioInstanceStatus>& outListRow );
        /** @brief 엔진 통계에서 보이스 프로파일을 만듭니다. */
        static AudioVoiceProfile makeVoiceProfile( const AudioEngineStats& stats );
        /** @brief 남은 시간 칸의 글입니다. 음수면 "loop" 계열 표시(현지화)이고, 아니면 "1.25 s" 입니다. */
        static string formatRemainingSeconds( float32 seconds );

        /**
         * @brief 라이브러리 사본에서 이벤트 @p source 를 복제해 뒤에 더하고 새 이름을 돌려줍니다(`<원본>_Copy`, 겹치면 `<원본>_Copy2` …).
         * @details 새 이벤트는 원본의 클립 · 곡선 · 설정을 그대로 가져 검사(`AudioEventLibrary::validate`)를 바로 통과한다 — 클립 없는 빈 이벤트를 만들면
         *          저장이 막힌다. 원본이 없으면 아무것도 하지 않고 빈 이름입니다(FMOD 의 Duplicate Event 에 해당).
         */
        static hashed_string duplicateEvent( AudioEventLibrary& inoutLibrary, const hashed_string& source );
        /** @brief 라이브러리 사본에서 이벤트 @p name 을 지웁니다. 없으면 false 입니다. */
        [[nodiscard]] static bool removeEvent( AudioEventLibrary& inoutLibrary, const hashed_string& name );

    private:
        /** @brief 에디터의 오디오 서비스입니다. 없으면(에디터 밖) nullptr 입니다. Model 안에서만 찾는다. */
        static AudioService* findAudioService();
        /** @brief 에디터의 오디오 서비스입니다(에디터는 오디오를 짓는 구성이라 늘 있다 — 전용 서버 구성만 오디오를 짓지 않는다). 없으면 멈춘다. */
        static AudioService& getAudioService();
    };
} // namespace sw::editor
