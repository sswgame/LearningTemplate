/**
 * @file EditorNetSimSession.h
 * @brief 에디터의 한 프로세스 재생입니다 — 서버 월드 1 + 클라이언트 월드 N 을 `NetSimHarness` 로 한 프로세스에서 돌리고, 월드마다 미리보기 뷰포트로 그립니다(ServerDebug S8).
 * @details 멀티 프로세스 재생(`EditorNetPlaySession`)과 달리 한 디버거에서 서버 · 클라이언트 모두에 중단점을 걸 수 있고 결정적이다(언리얼 기본 PIE).
 *          - 월드의 씬은 `NetSimPreviewSceneSource` 가 빌려 준 미리보기 씬이다. 렌더는 씬마다 따로라 활성 씬과 섞이지 않는다.
 *          - 게임은 `NetSimGameRegistry` 에 올라온 것만 고른다(키트 세션을 구현한 게임만 — 게임 모듈은 활성 씬 하나를 가정한다).
 *          - 입력은 포커스된 월드 하나만 받는다(`NetSimHarness::setInputFocus`). 창이 클릭된 월드를 넘긴다.
 *          - 진행은 고정 스텝이다. 프레임 시간을 모아 스텝 간격마다 `step` 하고, 한 프레임에 `kMaxStepsPerFrame` 을 넘기지 않는다(중단점 뒤 몰아 돌지 않게).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimPreviewSceneSource.h"

namespace sw::editor
{
    class EditorPreviewViewport;

    /** @brief 한 프로세스 재생의 시작 설정입니다. */
    struct EditorNetSimSettings
    {
        string               _gameName;         ///< `NetSimGameRegistry` 의 이름(필수)
        int32                _clientCount{ 2 }; ///< 처음 붙일 클라이언트 수(1 ~ `kMaxClients`)
        NetSimLinkConditions _conditions{};     ///< 모든 클라이언트 회선의 조건
        NetSimConfig         _harness{};        ///< 하니스 설정. `_pSceneSource` 는 세션이 채운다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorNetSimSession
     * @brief 하니스 · 씬 공급자 · 월드마다 뷰포트를 함께 가집니다. Multiplayer 창의 In process 탭이 하나를 들고 있습니다.
     */
    class EditorNetSimSession
    {
    public:
        static constexpr int32  kMaxClients       = 4;
        static constexpr uint32 kMaxStepsPerFrame = 4;

        EditorNetSimSession();
        ~EditorNetSimSession();

        EditorNetSimSession( const EditorNetSimSession& )            = delete;
        EditorNetSimSession& operator=( const EditorNetSimSession& ) = delete;

        /** @brief 하니스를 세우고 클라이언트를 붙입니다. 실패하면 이유를 @p outError 에 적고 아무것도 남기지 않습니다. */
        [[nodiscard]] bool start( const EditorNetSimSettings& settings, string& outError );
        /** @brief 뷰포트를 놓고 하니스를 내립니다(씬은 하니스가 공급자에 돌려준다). 돌고 있지 않으면 아무것도 하지 않습니다. */
        void stop();
        bool isRunning() const { return _pHarness != nullptr; }

        /** @brief 프레임 시간 @p frameSeconds 를 모아 고정 스텝을 진행합니다. 진행한 스텝 수를 돌려줍니다. */
        uint32 update( float64 frameSeconds );
        /** @brief 입력을 받을 월드를 정합니다(0 = 서버, N = 클라이언트, −1 = 없음). */
        bool  setInputFocus( int32 worldIndex );
        int32 getInputFocus() const;

        /** @brief 그릴 월드 번호입니다(서버 0 다음 클라이언트 들어온 순서). */
        void collectWorldIndices( vector<int32>& outListWorldIndex ) const;
        /** @brief @p worldIndex 월드의 뷰포트입니다. 처음 부를 때 그 월드의 씬을 붙입니다. 월드가 없으면 nullptr 입니다. */
        EditorPreviewViewport* findViewport( int32 worldIndex );
        /** @brief 하니스입니다. 돌고 있지 않으면 nullptr 입니다. */
        NetSimHarness* getHarness() const { return _pHarness.get(); }

    private:
        struct WorldView
        {
            int32                             _worldIndex{ -1 };
            unique_ptr<EditorPreviewViewport> _pViewport{};
        };

        unique_ptr<NetSimPreviewSceneSource> _pSceneSource;
        unique_ptr<INetSimGame>              _pGame;
        unique_ptr<NetSimHarness>            _pHarness;
        vector<WorldView>                    _listView;
        float64                              _accumulatedSeconds;
    };
} // namespace sw::editor
