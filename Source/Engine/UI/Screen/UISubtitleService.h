/**
 * @file UISubtitleService.h
 * @brief 자막 서비스입니다. 화자, 텍스트, 표시 시간을 받아 화면 아래 가운데에 최대 둘까지 띄우며, 사용자 설정(끔/켬, 크기, 배경 불투명도)을 따릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    struct AudioSubtitleCue;

    class UISystem;

    /** @brief 자막 한 줄입니다. */
    struct SW_API UISubtitleLine
    {
        string  _speaker{};                ///< 화자 이름입니다(현지화한 텍스트이며, 비면 이름 줄을 접습니다).
        string  _text{};                   ///< 현지화한 텍스트입니다.
        float32 _durationSeconds{ 0.0f };  ///< 보이는 시간
        float32 _remainingSeconds{ 0.0f }; ///< 남은 시간입니다(보이기 시작한 뒤부터 줄어듭니다).
        uint32  _id{ 0 };                  ///< `post` 가 돌려준 번호
    };
} // namespace sw

namespace sw
{
    /**
     * @class UISubtitleService
     * @brief 자막 줄을 받아 오버레이 레이어의 자막 화면(`engine/ui/subtitles.ui.xml`)에 최대 둘까지 보여 줍니다. `UISystem` 의 일부입니다(`UISystem::getSubtitles`).
     * @details 언리얼의 `FSubtitleManager` 에 해당한다. 줄은 최대 둘이 동시에 보이고(오래된 것이 위), 넘치는 줄은 대기열에서 기다렸다가 자리가 나면
     *          그때부터 시간을 센다. 사용자 설정은 매 프레임 읽는다. `gv_subtitles` 를 끄면 화면은 닫지만 줄은 계속 받고 시간도 흐른다(켜면 지금 줄부터 보인다).
     *          `gv_subtitleSize`(0, 1, 2)는 문서에 적힌 글 크기에 0.85, 1, 1.3 을 곱하고, `gv_subtitleBackgroundOpacity` 는 줄 바탕의 알파다.
     *          텍스트는 이미 현지화한 것을 받는다(대화 러너와 음성 이벤트가 해석한다). 게임 스레드에서만 쓴다.
     */
    class SW_API UISubtitleService
    {
    public:
        /** @brief 동시에 보이는 줄 수입니다. */
        static constexpr uint32 kMaxVisibleLineCount = 2;
        /** @brief 읽기 시간의 하한(초)입니다. */
        static constexpr float32 kMinReadingSeconds = 2.0f;
        /** @brief 글자 하나의 읽기 시간(초)입니다. */
        static constexpr float32 kReadingSecondsPerCharacter = 0.06f;
        // 자막 화면 문서는 엔진 기본 에셋 표가 기준이다(`UISystem::getEngineDocuments()._subtitles`). 줄 위젯 이름은 `Line<n>`(바탕), `Speaker<n>`, `Text<n>`(텍스트)입니다.

        explicit UISubtitleService( UISystem& uiSystem );
        ~UISubtitleService();
        UISubtitleService( const UISubtitleService& )            = delete;
        UISubtitleService& operator=( const UISubtitleService& ) = delete;

        /**
         * @brief 줄 하나를 올립니다. 자리가 있으면 다음 `update` 부터 보이고, 없으면 대기열 끝에 섭니다.
         * @param durationSeconds 보이는 시간입니다. 0 이하면 읽기 시간(`computeReadingSeconds`)을 씁니다.
         * @return 줄 번호(1 부터)입니다.
         */
        uint32 post( string_view speaker, string_view text, float32 durationSeconds = 0.0f );
        /**
         * @brief 음성 이벤트의 자막 신호(`AudioEngine::takeSubtitleCues`)를 받은 순서대로 올립니다. 키를 지금 문화권으로 풀고 표시 시간은 읽기 시간입니다.
         * @details 엔진 프레임이 UI 갱신 직전에 부른다. 자막 데이터의 기준은 이벤트 라이브러리(`AudioClipEntry::_subtitleKey`, `AudioEventDesc::_subtitleSpeakerKey`)다.
         */
        void postAudioCues( const vector<AudioSubtitleCue>& listCue );
        /** @brief 줄을 모두 지우고 화면을 닫습니다(`UISystem::shutdown` 과 장면 전환 때 부릅니다). */
        void clear();
        /** @brief 시간을 흘리고(꺼져 있어도) 대기열을 당긴 뒤, 설정을 따라 화면을 열고 닫고 줄 위젯을 맞춥니다. `UISystem::update` 가 부릅니다. */
        void update( float32 deltaSeconds );

        /** @brief 텍스트 @p text 의 읽기 시간입니다. max( 2 초, 글자(코드 포인트) 수 × 0.06 초 ) 로 계산합니다. */
        static float32 computeReadingSeconds( string_view text );
        /** @brief 크기 설정(`gv_subtitleSize` 0, 1, 2)에 해당하는 글 크기 배율입니다. 범위 밖이면 가까운 끝 값을 씁니다. */
        static float32 computeSizeScale( int32 sizeSetting );

        /** @brief 지금 보이는(시간이 흐르는) 줄 수입니다. 설정이 꺼져 있어도 셉니다. */
        uint32 getActiveLineCount() const { return static_cast<uint32>( _listActive.size() ); }
        /** @brief 지금 보이는 줄 @p index(0 = 가장 오래된 것)입니다. */
        const UISubtitleLine& getActiveLine( uint32 index ) const { return _listActive[index]; }
        /** @brief 대기열의 줄 수입니다. */
        uint32 getQueuedLineCount() const { return static_cast<uint32>( _listQueued.size() ); }
        /** @brief 자막 화면입니다(열려 있지 않으면 무효 핸들). */
        UIScreenHandle getScreen() const { return _screen; }

    private:
        /** @brief 화면을 엽니다. 문서를 만들지 못하면 오류를 한 번 남기고 다시 시도하지 않습니다. 열었으면 true 입니다. */
        [[nodiscard]] bool openScreen();
        /** @brief 화면을 닫고 핸들을 잊습니다. */
        void closeScreen();
        /** @brief 줄 슬롯 @p slot 을 줄 @p pLine 으로 맞춥니다(nullptr 이면 접습니다). 바뀐 필드만 씁니다. */
        void applySlot( UIScreen& screen, uint32 slot, const UISubtitleLine* pLine, float32 sizeScale, float32 backgroundOpacity );

        UISystem&              _uiSystem;
        vector<UISubtitleLine> _listActive;              ///< 보이는 줄입니다(오래된 것이 앞).
        vector<UISubtitleLine> _listQueued;              ///< 자리를 기다리는 줄입니다(먼저 온 것이 앞).
        UIBrush                _authoredBackground;      ///< 문서에 적힌 줄 바탕입니다(알파는 설정이 바꿉니다).
        float32                _authoredTextFontSize;    ///< 문서에 적힌 텍스트 크기입니다(크기 설정이 곱합니다).
        float32                _authoredSpeakerFontSize; ///< 문서에 적힌 화자 이름 크기입니다.
        UIScreenHandle         _screen;
        IDAllocator<uint32>    _lineIDAllocator;
        bool                   _bDocumentFailed; ///< 문서를 만들지 못했는지 나타냅니다. 같은 오류를 매 프레임 내지 않기 위한 값입니다.
    };
} // namespace sw
