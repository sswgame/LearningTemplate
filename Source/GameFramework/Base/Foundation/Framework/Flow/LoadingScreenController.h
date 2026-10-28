/**
 * @file LoadingScreenController.h
 * @brief 씬을 비동기로 바꾸는 동안 맨 위 층(`Loading`)에 로딩 화면을 띄우고, 화면 페이드(`ScreenFade`)의 알파를 전체 화면 검은 패널로 그립니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Serialization/Serializer/DataPath.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

#include "GameFramework/Base/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

#include <optional>

namespace sw
{
    class ScreenFade;
    class UISystem;

    /** @brief 로딩 화면의 겉모습과 시간 설정입니다. `GameInstance` 가 gamesettings 로 채웁니다. */
    struct LoadingScreenSettings
    {
        DataPath       _documentPath{};                      ///< 로딩 화면 문서(Loading 층 — 문서의 `UIScreenDesc`). 기준은 gamesettings 의 `_loadingScreen` 이고, 칸이 없으면(`isSet()` 거짓) 로딩 화면을 띄우지 않는다
        vector<string> _listTip{};                           ///< 팁 글의 현지화 키(원문). 열 때마다 하나를 고른다. 비면 팁 영역을 접는다
        float32        _minimumSeconds{ 0.5f };              ///< 최소 표시 시간(초). 빠른 로드에서 한 프레임만 깜박이지 않게 한다
        float32        _fadeInSeconds{ 0.35f };              ///< 닫은 뒤 검은 화면에서 씬으로 돌아오는 페이드 인(초)
        uint32         _tipSeed{ GameRandom::kDefaultSeed }; ///< 팁 고르기 시드. 같은 시드면 같은 순서다
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoadingScreenController
     * @brief 로딩 화면과 화면 페이드를 그립니다. 언리얼 Lyra 의 `ULoadingScreenManager` 에 해당합니다.
     * @details 게임 인스턴스 서브시스템이다(`game::findSubsystem<LoadingScreenController>()`). `GameInstance` 가 매 프레임 `update` 한다.
     *          초기화(`onInitialize` 앞)에서 gamesettings 의 `_loadingScreen` 문서와 UI 시스템을 묶어, 첫 씬 요청이 로딩 화면을 띄울 수 있다.
     *          - **로딩 화면**: `beginLoading` 이 Loading 층 화면(문서 `_documentPath`)을 연다. 이 화면은 입력을 막고 Back 으로 닫히지 않는다.
     *            `update` 는 로드가 끝났고 최소 표시 시간이 지났으면 화면을 닫고 페이드 인을 시작한다.
     *            닫기 애니메이션 역할이며, 검은 화면에서 씬이 드러난다. 페이드는 입력을 막지 않는다.
     *            문서에 `Spinner` 가 있으면 돌리고, `Tip` 이 있으면 팁 하나를 넣는다. 팁 키는 `findStringByText` 로 문자열을 찾는다(없으면 원문 그대로).
     *            진행률은 부르는 쪽이 넘긴다(`update` 의 `loadProgress` — 씬 매니저의 프리로드 진행 `SceneManager::tryGetLoadProgress`). 문서에 `Progress` 슬라이더가 있으면
     *            값이 있을 때 보이고(입력은 받지 않는다) 없으면 접는다.
     *            팁 목록은 gamesettings 의 `_loadingTips` 파일(`loadTipsFromResource`)이 채운다.
     *          - **페이드**: `ScreenFade::getOverlayAlpha` 가 0 보다 크면 Overlay 층(입력 없음, 로딩 화면 아래)에 전체 화면 검은 패널을 띄우고 불투명도를 그 알파로 둔다.
     *            0 이 되면 닫는다.
     *          UI 시스템이 없으면(전용 서버, 헤드리스) 아무것도 하지 않는다. 게임 스레드에서만 써야 한다.
     */
    class SW_GF_API LoadingScreenController final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "LoadingScreenController";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief gamesettings 의 로딩 문서와 UI 시스템을 묶습니다. UI 시스템이 없으면(전용 서버) 아무것도 띄우지 않습니다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief UI 시스템을 풉니다(연 화면을 닫는다). */
        void shutdownSubsystem() override { bindUISystem( nullptr ); }

        /** @brief 문서에서 이 이름의 위젯을 돌립니다. 속도는 초당 라디안이며 3π/2 는 270° 입니다. */
        static constexpr float32 kSpinnerRadiansPerSecond = MathUtil::kPi * 1.5f;
        /** @brief 로딩 문서에서 돌릴 위젯과 팁 글 위젯의 이름입니다. */
        static constexpr const utf8* kSpinnerWidgetName = "Spinner";
        static constexpr const utf8* kTipWidgetName     = "Tip";
        /** @brief 로딩 문서의 진행 슬라이더 이름입니다(0~1). 없으면 진행률을 그리지 않는다. */
        static constexpr const utf8* kProgressWidgetName = "Progress";

        /**
         * @brief 팁 목록 파일(`<LoadingTips><Tip text="원문"/>…</LoadingTips>`)을 읽어 @p outListTip 을 채웁니다(먼저 비운다). 파일을 못 읽거나 `text` 가 빈 팁이 있으면 오류를 남기고 false 입니다.
         * @details 글은 지역화 원문이다 — 열 때 `LocalizationManager::findStringByText` 로 지금 문화권의 글로 푼다. 수집은 현지화 프로젝트의 `assetRules`(`files`·`attribute="text"`·`elements`)로 한다.
         */
        [[nodiscard]] static bool loadTipsFromResource( const AssetID& assetID, vector<string>& outListTip );
        /** @brief `loadTipsFromResource` 의 XML 문자열 버전이며 테스트가 씁니다. */
        [[nodiscard]] static bool loadTipsFromXMLText( string_view xmlText, string_view sourceName, vector<string>& outListTip );

        LoadingScreenController();
        ~LoadingScreenController() override;
        LoadingScreenController( const LoadingScreenController& )            = delete;
        LoadingScreenController& operator=( const LoadingScreenController& ) = delete;

        /** @brief 쓸 UI 시스템을 정합니다. 이전 UI 시스템의 화면은 닫으며, nullptr 이면 아무것도 띄우지 않습니다. 테스트는 자기 것을 넘깁니다. */
        void      bindUISystem( UISystem* pUISystem );
        UISystem* getUISystem() const { return _pUISystem; }

        /** @brief 설정을 바꿉니다. 팁 시드를 다시 겁니다. */
        void                         setSettings( const LoadingScreenSettings& settings );
        const LoadingScreenSettings& getSettings() const { return _settings; }

        /** @brief 로딩을 시작합니다. 로딩 화면을 열고(이미 열려 있으면 그대로 둡니다) 표시 시간을 0 부터 셉니다. 연 화면이 없으면(문서 오류) false 입니다. */
        bool beginLoading();
        /**
         * @brief 한 프레임을 넘깁니다.
         * @param bLoading 아직 로드 중이면 true 이다. 씬 로드 요청이 남았거나 씬 매니저가 전환 중일 때이다.
         * @param loadProgress 로드 진행(0~1)입니다. 모르면 비웁니다 — 진행 슬라이더(`Progress`)를 접는다.
         * @param fade     그릴 화면 페이드이다. 로딩 화면을 닫을 때 페이드 인을 시작한다. 페이드를 진행시키는 것(`ScreenFade::update`)은 주인(전환 관리자)의 일이다.
         */
        void update( float32 deltaSeconds, bool bLoading, std::optional<float32> loadProgress, ScreenFade& fade );

        /** @brief 로딩 화면이 떠 있으면 true 입니다. */
        bool isShowing() const;
        /** @brief 이번 로딩에서 화면이 떠 있던 시간(초)입니다. */
        float32 getShownSeconds() const { return _shownSeconds; }
        /** @brief 지금 띄운 로딩 화면을 반환합니다. 없으면 nullptr 이며, 포인터는 그 호출 안에서만 씁니다. */
        UIScreen* findLoadingScreen() const;
        /** @brief 페이드 패널 화면을 반환합니다. 없으면 nullptr 이며, 페이드 알파가 0 이면 닫혀 있습니다. */
        UIScreen* findFadeScreen() const;
        /** @brief 지금 고른 팁 키입니다. 없으면 빈 글입니다. */
        const string& getCurrentTip() const { return _currentTip; }

    private:
        /** @brief 로딩 화면과 페이드 화면을 닫습니다. */
        void closeScreens();
        /** @brief 페이드 패널을 알파에 맞춥니다. 알파가 0 이면 닫고, 0 보다 크면 열고 불투명도를 맞춥니다. */
        void syncFadeOverlay( float32 alpha );
        /** @brief 진행 슬라이더(`Progress`)를 로드 진행에 맞춥니다. 진행을 모르면 접는다. 문서에 슬라이더가 없으면 아무것도 하지 않는다. */
        static void applyProgress( UIScreen& screen, std::optional<float32> loadProgress );
        /** @brief 팁을 하나 골라 `Tip` 위젯에 넣습니다. 목록이 비면 팁 위젯을 접습니다. */
        void applyTip( UIScreen& screen );

    private:
        LoadingScreenSettings _settings;
        string                _currentTip;
        UISystem*             _pUISystem;
        UIScreenHandle        _loadingScreen;
        UIScreenHandle        _fadeScreen;
        float32               _shownSeconds;
        float32               _spinnerAngle;
        GameRandom            _tipRandom; ///< 팁 고르기 난수(`setSettings` 가 시드를 다시 설정한다)
    };
} // namespace sw
