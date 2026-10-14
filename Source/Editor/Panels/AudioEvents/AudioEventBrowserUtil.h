/**
 * @file AudioEventBrowserUtil.h
 * @brief 오디오 이벤트 창이 그리는 표 행을 엔진 상태에서 만드는 순수 로직입니다. ImGui 를 쓰지 않아 단위 테스트가 바로 부릅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioEngineState.h"

namespace sw
{
    struct AudioAttenuationDesc;
    struct AudioEventDesc;
} // namespace sw

namespace sw::editor
{
    /** @brief 이벤트 속성 표의 한 줄입니다(이름 · 값). */
    struct AudioEventFieldRow
    {
        const utf8* _pLabel{ "" }; ///< 현지화한 필드 이름이다(정적 수명).
        string      _value{};      ///< 단위까지 붙인 값이다.
    };

    /** @brief 보이스 가상화 프로파일러 한 장입니다(실제 · 가상 보이스 수와 상한). */
    struct AudioVoiceProfile
    {
        uint32  _realVoiceCount{ 0 };
        uint32  _virtualVoiceCount{ 0 };
        uint32  _voiceCount{ 0 };         ///< 실제 + 가상 + 클립 대기 보이스다.
        uint32  _peakRealVoiceCount{ 0 };
        uint32  _maxRealVoiceCount{ 0 };  ///< 0 이면 믹서 그래프가 아직 없다.
        uint32  _maxVoiceCount{ 0 };
        uint32  _instanceCount{ 0 };
        uint64  _playedEventCount{ 0 };
        uint64  _droppedEventCount{ 0 };
        float32 _realUsage{ 0.0f };       ///< 실제 보이스 수 / 실제 상한([0, 1]로 자른다).
        float32 _voiceUsage{ 0.0f };      ///< 보이스 수 / 보이스 상한([0, 1]로 자른다).
        bool    _bRealLimitReached{ false };
        bool    _bVoiceLimitReached{ false };
    };

    /**
     * @struct AudioEventBrowserUtil
     * @brief 이벤트 목록 거르기, 이벤트 속성 행, 재생 중 인스턴스 행, 보이스 프로파일을 만듭니다.
     */
    struct AudioEventBrowserUtil
    {
        /** @brief @p listName 에서 @p filter 를 (대소문자 무시) 품은 이름만 사전순으로 @p outListName 에 담습니다. 필터가 비면 전부입니다. */
        static void filterEventNames( const vector<hashed_string>& listName, string_view filter, vector<hashed_string>& outListName );
        /** @brief 이벤트 설명자와 감쇠 프리셋에서 속성 표 행을 만듭니다(비우고 채웁니다). 감쇠 이름이 비면 반경 줄은 "2D" 입니다. */
        static void buildEventFieldRows( const AudioEventDesc& eventDesc, const AudioAttenuationDesc& attenuation, vector<AudioEventFieldRow>& outListRow );
        /**
         * @brief 재생 중 인스턴스 표 행을 만듭니다(비우고 채웁니다). @p eventFilter 가 비지 않으면 그 이벤트만 담습니다.
         * @details 남은 시간이 짧은 것이 위에 오고, 끝을 모르는 것(반복 · 클립 대기)은 맨 아래에 재생 id 순으로 둡니다.
         */
        static void buildInstanceRows( const vector<AudioInstanceStatus>& listStatus, const hashed_string& eventFilter, vector<AudioInstanceStatus>& outListRow );
        /** @brief 엔진 통계에서 보이스 프로파일을 만듭니다. */
        static AudioVoiceProfile buildVoiceProfile( const AudioEngineStats& stats );
        /** @brief 남은 시간 칸의 글입니다. 음수면 "loop" 계열 표시(현지화)이고, 아니면 "1.25 s" 입니다. */
        static string formatRemainingSeconds( float32 seconds );
    };
} // namespace sw::editor
