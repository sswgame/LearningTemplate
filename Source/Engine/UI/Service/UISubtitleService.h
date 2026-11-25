/**
 * @file UISubtitleService.h
 * @brief 자막 서비스입니다. 화자, 텍스트, 표시 시간을 받아 화면 아래 가운데에 정한 수(`SubtitleConfig`)까지 쌓아 띄우며, 사용자 설정(끔/켬, 크기, 배경 불투명도)을 따릅니다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Presentation/ISubtitleSink.h"
#include "Engine/UI/Screen/UIOwnedScreen.h"
#include "Engine/UI/Screen/UIScreenTypes.h"
#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    struct AudioSubtitleCue;

    class AudioEngine;

    class UIScreen;
    class UISystem;

    /** @brief 자막 한 줄입니다. */
    struct SW_API UISubtitleLine
    {
        string  _speaker{};                 ///< 화자 이름입니다(현지화 키 · 메시지 · 원문 — 그릴 때 풀린다, 비면 이름 줄을 접습니다).
        string  _text{};                    ///< 줄의 글입니다(현지화 키 · 메시지 · 원문 — 그릴 때 풀린다).
        float32 _durationSeconds{ 0.0f };   ///< 보이는 시간
        float32 _remainingSeconds{ 0.0f };  ///< 남은 시간입니다(보이기 시작한 뒤부터 줄어듭니다).
        uint64  _voicePlayingID{ 0 };       ///< 이 줄을 낸 음성의 재생 id(`AudioPlayingID`)입니다. 0 이면 음성이 없는 줄입니다.
        uint32  _id{ 0 };                   ///< `post` 가 돌려준 번호
        bool    _bVoicePlaying{ false };    ///< 음성이 아직 나는 중입니다(`refreshVoiceState`). 나는 동안은 시간이 다 돼도 내리지 않는다
        bool    _bRunsInGamePause{ false }; ///< 게임이 멈춘 동안에도 시간이 흐른다 — 멈추지 않는 버스의 음성이 낸 줄이다. 그 밖의 줄은 소리 · 시퀀스와 같이 선다
    };

    /** @brief 지금 보이는 자막 줄의 배치 결과입니다(감시 값 · 진단). 마지막 UI 배치 기준입니다. */
    struct UISubtitleLayoutReport
    {
        float32 _newestTextFontSize{ 0.0f }; ///< 가장 최근 줄의 본문 글 크기(크기 설정을 곱한 값). 화면이 없으면 0
        uint32  _maxWrappedLineCount{ 0 };   ///< 보이는 줄 가운데 가장 많이 접힌 본문의 줄 수
        bool    _bOverflow{ false };         ///< 본문이 잘렸거나(`TextLayoutResult::_bTruncated`) 자기 칸 · 화면 폭을 넘었다
    };
} // namespace sw

namespace sw
{
    /**
     * @class UISubtitleService
     * @brief 자막 줄을 받아 오버레이 레이어의 자막 화면(`engine/ui/subtitles.ui.xml`)에 보여 줍니다. 엔진 범위 서브시스템 `UISubtitles` 입니다.
     * @details 언리얼의 `FSubtitleManager` 에 해당한다. 표시 규칙은 엔진 설정(`EngineConfig::_subtitles`)이 정하고, 없거나 틀리면 기동이 오류로 멈춘다.
     *          줄은 `_maxVisibleLineCount` 까지 동시에 보이고(오래된 것이 위), 넘치는 줄은 대기열에서 기다렸다가 자리가 나면 그때부터 시간을 센다.
     *          보이는 시간은 준 길이, 0 이면 읽기 시간 max( 최소 표시 시간, 글자 수 × 글자당 시간 ) 이다. 음성이 낸 줄은 그 음성이 끝날 때까지도 남는다
     *          (언리얼 Dialogue Wave 자막이 소리 길이를 따르는 것과 같다 — 짧은 음성도 읽을 시간은 준다).
     *          사용자 설정은 매 프레임 읽는다. `gv_subtitles` 를 끄면 화면은 닫지만 줄은 계속 받고 시간도 흐른다(켜면 지금 줄부터 보인다).
     *          `gv_subtitleSize` 는 문서에 적힌 글 크기에 설정의 `_arrSizeScale` 배율을 곱하고, `gv_subtitleBackgroundOpacity` 는 줄 바탕의 알파다.
     *          텍스트는 현지화 키 · 메시지 · 원문이고 글 위젯이 그릴 때 푼다 — 푼 글을 들면 보이는 동안 언어를 바꿔도 그대로다. 길이가 늘어나는 언어(의사 문화권 qps-ploc)는 문서의 최대 폭에서 접힌다.
     *          게임 스레드에서만 쓴다.
     */
    class SW_API UISubtitleService final : public Subsystem
    {
    public:
        // Subsystem — 엔진 범위의 서브시스템이다(`engine::getSubsystem<UISubtitleService>()`, 게임은 `game::getSubsystem<UISubtitleService>()`). 화면 스택(`UISystem`)에 의존하고,
        // 기동이 UI 갱신 칸(`UISystem::onUpdateBeforeScreens`)에 걸고 종료가 줄을 비우고 푼다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UISubtitles";
        // 자막 화면 문서는 엔진 기본 에셋 표가 기준이다(`UISystem::getEngineDocuments()._subtitles`). 줄 위젯 이름은 `Line<n>`(바탕), `Speaker<n>`, `Text<n>`(텍스트)입니다.

        UISubtitleService();
        ~UISubtitleService() override;
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        void                    resetGameInstanceState( GameInstanceBoundary boundary ) override;
        const utf8*             findGameInstanceState() const override;
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /**
         * @brief 표시 규칙을 정합니다. 기동(`initializeSubsystem`)이 엔진 설정으로 부르고, 테스트는 값을 직접 넣습니다.
         * @return 줄 수 · 최소 표시 시간 · 글자당 시간 · 크기 배율 가운데 0 이하가 있으면 오류를 남기고 false 이며, 규칙은 그대로입니다.
         */
        [[nodiscard]] bool    configure( const SubtitleConfig& config );
        const SubtitleConfig& getConfig() const { return _config; }

        /**
         * @brief 줄 하나를 올립니다. 자리가 있으면 다음 `update` 부터 보이고, 없으면 대기열 끝에 섭니다. `configure` 뒤에만 부릅니다.
         * @param durationSeconds 보이는 시간입니다. 0 이하면 읽기 시간(`computeReadingSeconds`)을 씁니다.
         * @return 줄 번호(1 부터)입니다.
         */
        uint32 post( string_view speaker, string_view text, float32 durationSeconds = 0.0f );
        /** @brief 줄 @p lineID 를 지금 내립니다(보이는 중이든 기다리는 중이든). 없는 번호면 아무 일도 하지 않습니다. */
        void remove( uint32 lineID );
        /** @brief 자막을 받는 자리(`ISubtitleSink`)입니다. 기동이 이 멤버를 그 타입으로 건다(`SubsystemScope::exposeMember`). */
        ISubtitleSink& getSubtitleSink() { return _subtitleSink; }
        /**
         * @brief 음성 이벤트의 자막 신호(`AudioEngine::takeSubtitleCues`)를 받은 순서대로 올립니다. 키째 들고 표시 시간은 읽기 시간이며, 음성이 나는 동안은 남습니다.
         * @details 엔진 프레임이 UI 갱신 직전에 부른다. 자막 데이터의 기준은 이벤트 라이브러리(`AudioClipEntry::_subtitleKey`, `AudioEventDef::_subtitleSpeakerKey`)다.
         */
        void postAudioCues( const vector<AudioSubtitleCue>& listCue );
        /** @brief 음성이 낸 줄마다 그 음성이 아직 나는지 @p pAudio 에 물어 둡니다. 오디오가 없으면(nullptr) 모두 끝난 것으로 봅니다. 엔진 프레임이 프레임마다 부릅니다. */
        void refreshVoiceState( const AudioEngine* pAudio );
        /** @brief 게임이 멈췄는지 받습니다. 멈춘 동안 줄의 시간이 선다(멈추지 않는 버스의 음성이 낸 줄은 흐른다) — 시퀀스 · 게임 소리와 같은 시계다. 엔진 프레임이 `update` 앞에 부릅니다. */
        void setGamePaused( bool bPaused ) { _bGamePaused = bPaused ? SW_TRUE : SW_FALSE; }
        /** @brief 줄을 모두 지우고 화면을 닫습니다(종료 `shutdownSubsystem` 과 시나리오 단계 되돌리기가 부릅니다). */
        void clear();
        /** @brief 시간을 흘리고(꺼져 있어도 — 게임이 멈췄으면 선다) 대기열을 당긴 뒤, 설정을 따라 화면을 열고 닫고 줄 위젯을 맞춥니다. UI 갱신의 화면 틱 앞 칸이 부릅니다. */
        void update( float32 deltaSeconds );

        /** @brief 텍스트 @p text 의 읽기 시간입니다. max( 최소 표시 시간, 글자(코드 포인트) 수 × 글자당 시간 ) 입니다. */
        float32 computeReadingSeconds( string_view text ) const;
        /** @brief 크기 설정(`gv_subtitleSize` 0 작게 · 1 보통 · 2 크게)에 해당하는 글 크기 배율입니다. 범위 밖이면 가까운 끝 값을 씁니다. */
        float32 computeSizeScale( int32 sizeSetting ) const;

        /** @brief 지금 보이는(시간이 흐르는) 줄 수입니다. 설정이 꺼져 있어도 셉니다. */
        uint32 getActiveLineCount() const { return static_cast<uint32>( _listActive.size() ); }
        /** @brief 지금 보이는 줄 @p index(0 = 가장 오래된 것)입니다. */
        const UISubtitleLine& getActiveLine( uint32 index ) const { return _listActive[index]; }
        /** @brief 대기열의 줄 수입니다. */
        uint32 getQueuedLineCount() const { return static_cast<uint32>( _listQueued.size() ); }
        /** @brief 자막 화면입니다(열려 있지 않으면 무효 핸들). */
        UIScreenHandle getScreen() const { return _screen.getHandle(); }
        /** @brief 보이는 줄의 마지막 배치 결과(글 크기 · 접힌 줄 수 · 넘침)입니다. 화면이 없으면 빈 결과입니다. */
        UISubtitleLayoutReport makeLayoutReport() const;

    private:
        /** @brief 화면을 엽니다. 문서를 만들지 못하면 오류를 한 번 남기고 다시 시도하지 않습니다. 열었으면 true 입니다. */
        [[nodiscard]] bool openScreen();
        /** @brief 줄 슬롯 @p slot 을 줄 @p pLine 으로 맞춥니다(nullptr 이면 접습니다). 바뀐 필드만 씁니다. */
        void applySlot( UIScreen& screen, uint32 slot, const UISubtitleLine* pLine, float32 sizeScale, float32 backgroundOpacity );

    private:
        /**
         * @class SubtitleSink
         * @brief 줄을 올리는 `ISubtitleSink` 입니다(게임 코드의 대화 진행은 읽기 시간(0), 시퀀서 자막은 구간 길이를 준다). 서비스가 그 인터페이스를 상속하지 않고 이 멤버를 건다(수명은 서비스와 같다).
         */
        class SubtitleSink final : public ISubtitleSink
        {
        public:
            explicit SubtitleSink( UISubtitleService& owner )
                : _owner{ owner }
            {
            }

            uint32 postSubtitle( string_view speaker, string_view text, float32 durationSeconds ) override { return _owner.post( speaker, text, durationSeconds ); }
            void   removeSubtitle( uint32 lineID ) override { _owner.remove( lineID ); }
            void   clearSubtitles() override { _owner.clear(); }

        private:
            UISubtitleService& _owner;
        };

    private:
        SubtitleSink           _subtitleSink;
        SubtitleConfig         _config;                  ///< 표시 규칙(`configure` — 기동은 엔진 설정에서)
        UISystem*              _pUI;                     ///< 화면 스택(기동이 찾는다, 그 전 · 종료 뒤에는 nullptr — `post` 는 쌓기만 한다)
        Subscription           _updateSubscription;      ///< UI 갱신 칸(`UISystem::onUpdateBeforeScreens`) 구독
        vector<UISubtitleLine> _listActive;              ///< 보이는 줄입니다(오래된 것이 앞).
        vector<UISubtitleLine> _listQueued;              ///< 자리를 기다리는 줄입니다(먼저 온 것이 앞).
        UIBrush                _authoredBackground;      ///< 문서에 적힌 줄 바탕입니다(알파는 설정이 바꿉니다).
        float32                _authoredTextFontSize;    ///< 문서에 적힌 텍스트 크기입니다(크기 설정이 곱합니다).
        float32                _authoredSpeakerFontSize; ///< 문서에 적힌 화자 이름 크기입니다.
        UIOwnedScreen          _screen;
        IDAllocator<uint32>    _lineIDAllocator;
        uint8                  _bGamePaused     : 1; ///< 게임이 멈췄다(`setGamePaused`)
        uint8                  _bDocumentFailed : 1; ///< 문서를 만들지 못했는지 나타냅니다. 같은 오류를 매 프레임 내지 않기 위한 값입니다.

        SW_SERVICE_SLOT( UISubtitleService, Required );
    };
} // namespace sw
