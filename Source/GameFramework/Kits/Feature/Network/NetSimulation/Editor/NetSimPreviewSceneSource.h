/**
 * @file NetSimPreviewSceneSource.h (키트의 편집기 쪽 — 한 프로세스 재생 탭만 쓴다)
 * @brief 하니스 월드에 `SceneManager` 의 미리보기 씬을 빌려 줍니다. 에디터의 한 프로세스 재생이 월드마다 자기 렌더 타깃에 그리는 바닥입니다(ServerDebug S8).
 * @details 미리보기 씬은 활성 씬과 따로 렌더된다(씬마다 렌더러 — `EditorMultiWorld` 단계 2). 정책은 틱 없음(하니스가 매니저를 직접 틱한다) · 물리 켬 ·
 *          오디오 끔(오디오 엔진이 하나라 월드끼리 덮어쓴다) · 플레이 없음(하니스가 `beginPlay` 를 부른다)이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

namespace sw
{
    class SceneManager;

    /**
     * @class NetSimPreviewSceneSource
     * @brief 월드마다 미리보기 씬 하나를 만들어 빌려 주고, 돌려받으면 내립니다. 씬 키로 렌더 뷰를 찾게 합니다.
     */
    class NetSimPreviewSceneSource final : public INetSimSceneSource
    {
    public:
        /** @brief @p sceneManager 는 이 객체보다 오래 살아야 합니다. */
        explicit NetSimPreviewSceneSource( SceneManager& sceneManager );
        ~NetSimPreviewSceneSource() override;

        Scene* acquireScene( NetSimRole role, int32 worldIndex ) override;
        void   releaseScene( Scene& scene ) override;

        /** @brief @p worldIndex 월드에 빌려 준 씬의 키입니다(`SceneManager::getPreviewSceneKey`). 빌려 준 씬이 없으면 비어 있습니다. */
        optional<uint32> findSceneKey( int32 worldIndex ) const;
        /** @brief 빌려 준 씬 수입니다. */
        uint32 getLentCount() const { return static_cast<uint32>( _listLent.size() ); }

    private:
        struct Lent
        {
            Scene* _pScene{ nullptr };
            int32  _worldIndex{ constant::invalid_index::kInt32 };
            uint32 _sceneKey{ 0 };
        };

        SceneManager* _pSceneManager;
        vector<Lent>  _listLent;
    };
} // namespace sw
