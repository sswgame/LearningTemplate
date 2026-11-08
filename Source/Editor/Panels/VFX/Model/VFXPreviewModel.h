/**
 * @file VFXPreviewModel.h
 * @brief VFX 미리보기 창의 모델 — 효과 파일의 방출기 표, 미리보기 씬의 효과 오브젝트 놓기 · 지금 문서 재생 걸기, 방출기 격리, 캐릭터 타깃(효과를 태워 원을 도는 애니메이션 몸). ImGui 를 모릅니다.
 * @details 패널(`VFXPreviewPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) 단추 · 목록을 그리기만 한다 — 래그돌 미리보기(`RagdollPreviewModel`)와 같은 선이다
 *          (언리얼 Niagara 에디터의 시스템 툴킷 상태와 뷰포트가 나뉜 것과 같다). 씬은 패널이 넘긴다(모델은 씬을 갖지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    class Scene;
    class VFXComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 방출기 하나의 요약(목록 표시용)입니다. */
    struct VFXPreviewEmitterRow
    {
        string _name{};
        string _texturePath{};
        uint32 _maxParticles{ 0 };
        bool   _bAdditive{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool   _bLooping{ false };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @class VFXPreviewModel
     * @brief VFX 미리보기 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class VFXPreviewModel
    {
    public:
        /** @brief 격리한 방출기가 없음을 뜻하는 번호입니다(모든 방출기가 보인다). */
        static constexpr int32 kNoSolo = -1;
        /** @brief 타깃이 도는 원의 반지름(m)입니다. 효과 프레이밍(`VFXPreviewPanel` 의 1.5 m 구) 안에 든다. */
        static constexpr float32 kTargetOrbitRadius = 1.0f;
        /** @brief 타깃의 각속도(rad/s)입니다 — 걷는 속도(약 1 m/s)로 돌아 트레일이 보인다. */
        static constexpr float32 kTargetOrbitSpeed = 1.0f;
        /** @brief 효과가 타깃 몸 원점에서 올라선 높이(m)입니다 — 가슴께라 발밑 그림자에 묻히지 않는다. */
        static constexpr float32 kTargetAttachHeight = 1.0f;

        VFXPreviewModel();

        /** @brief 효과 파일 @p assetPath 를 읽어 방출기 표를 다시 채우고, 다음 `syncEffect` 가 효과를 다시 걸게 합니다. */
        ToolAssetLoadResult loadAsset( string_view assetPath );
        /** @brief 새 미리보기 씬 @p scene 에 효과 오브젝트 하나(`PreviewSubject`)를 스폰합니다. 효과 파일은 `syncEffect` 가 건다. */
        void populateScene( Scene& scene );
        /** @brief 다음 `syncEffect` 가 효과를 다시 걸게 합니다(Reload). */
        void requestResync() { _previewEffectPath.clear(); }
        /**
         * @brief @p pScene 의 효과가 효과 파일 @p assetPath 를 재생하게 합니다(경로가 바뀌었거나 다시 읽었을 때만).
         * @details 문서를 읽지 못했으면 부르는 쪽이 부르지 않는다. 미리보기 씬은 플레이하지 않으므로(틱만) 재생은 여기서 시작한다.
         */
        void syncEffect( Scene* pScene, string_view assetPath );
        /** @brief @p pScene 의 효과입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        [[nodiscard]] static VFXComponent* findSubject( Scene* pScene );

        /**
         * @brief 읽은 효과 파일의 내용이 그 뒤에 바뀌었으면(카탈로그 편집기의 저장 · 바깥 편집) true 를 돌려주고 새 내용 해시를 기억합니다. 패널은 true 면 다시 읽고 다시 건다(자동 갱신).
         * @details 저장 뒤 Reload 를 누르던 왕복을 없앤다(언리얼 Niagara · 유니티 파티클 미리보기는 저장하면 그 자리에서 바뀐다). 파일 감시 사건이 아니라 내용 해시 비교라
         *          에디터 밖의 저장도 같은 길이다. 도장(크기 · 쓰기 시각)을 비교하면 같은 틱 안에 같은 크기로 다시 쓴 저장을 놓쳤다 — 해시 캐시(`ContentHash::computeFile`)가
         *          자기 쓰기는 쓰기 관찰자로, 바깥 쓰기는 시간 창으로 가려 도장이 그대로인 동안은 파일을 열지 않는다.
         *          shortcut: 창이 열린 동안 프레임마다 파일 하나의 stat 이다 — 미리보기 창이 여럿이 되면 감시 사건(`AssetHotReload`)으로 옮긴다.
         */
        [[nodiscard]] bool pollFileChanged();
        /** @brief @p diskPath 의 내용 해시가 @p inoutHash 와 다르면 true 이고 @p inoutHash 를 새 해시로 바꿉니다. 파일이 없으면 false 입니다(지운 파일은 다시 읽을 것이 없다). */
        [[nodiscard]] static bool updateContentHash( string_view diskPath, uint64& inoutHash );
        /**
         * @brief 바뀐 효과 파일 @p assetPath 를 공유 캐시에서 먼저 새 글로 다시 읽습니다(`AssetHotReload::applySavedAsset` — 감시 사건이 뒤늦게 와도 같은 저장을 두 번 읽지 않게 도장을 남긴다).
         * @details 그래야 다시 연 문서와 다시 건 효과가 새 정의다. View 가 작업 공간 헤더를 보지 않게 여기를 지난다.
         */
        static void applySavedAsset( string_view assetPath );

        const vector<VFXPreviewEmitterRow>& getEmitterRows() const { return _listEmitterRow; }
        const string&                       getStatus() const { return _status; }

        // 방출기 격리(언리얼 Niagara 에디터의 Isolate). 멈춤 · 한 프레임 걸음 · 속도는 미리보기 공통 재생 막대(`EditorPreviewPanel` — 씬 시간 배율)가 맡는다.
        /** @brief 방출기 @p emitterIndex 만 보이게 합니다. 이미 그 방출기만 보이면 격리를 풉니다(토글). 음수면 격리를 풉니다. 저장되는 효과는 바뀌지 않는다(보기 상태). */
        void toggleSolo( int32 emitterIndex );
        /** @brief 격리한 방출기 번호입니다(`kNoSolo` 면 모두 보인다). */
        int32 getSoloEmitter() const { return _soloEmitter; }
        /** @brief 격리 상태 @p soloEmitter 에서 방출기 @p emitterIndex 가 켜지는지입니다(격리가 없으면 모두 켜진다). */
        [[nodiscard]] static bool computeEmitterEnabled( int32 soloEmitter, uint32 emitterIndex );
        /**
         * @brief @p pScene 의 효과에 격리를 걸고, 캐릭터 타깃을 놓고 움직입니다. 패널이 프레임마다 효과를 건 뒤 한 번 부른다(동기화 지점).
         * @param frameDeltaSeconds 이번 에디터 프레임의 시간입니다. 씬의 시간 배율(재생 막대의 멈춤 · 한 프레임 · 속도 — `ScenePreviewPolicy::_timeScale`)을 곱해 타깃이 효과와 같은 시간으로 돈다.
         */
        void syncPlayback( Scene* pScene, float32 frameDeltaSeconds );

        // 캐릭터 타깃(애니메이션이 도는 몸에 효과를 붙여 트레일 · 잔상 · 손 효과를 본다 — 언리얼 Niagara 미리보기의 Preview Mesh)
        /** @brief 입력란이 직접 고치는 타깃 프리팹 경로입니다(Spawn 을 눌러야 적용한다). */
        string& editTargetPrefabPath() { return _targetPrefabPath; }
        /** @brief 입력란의 타깃 경로를 확정하고 다음 `syncPlayback` 이 몸을 놓게 합니다. 빈 경로면 타깃을 치웁니다(효과는 원점에 선다). */
        void spawnTargetFromField();
        /** @brief 놓은 타깃 몸이 있으면 true 입니다(감시 값 `Editor.VFXPreviewHasTarget`). */
        [[nodiscard]] bool hasTarget( Scene* pScene ) const;
        /** @brief 타깃이 원을 따라 도는 자리입니다(반지름 `kTargetOrbitRadius`, 높이 0). 각은 라디안입니다. */
        [[nodiscard]] static float3 computeOrbitPosition( float32 angle );

    private:
        /** @brief 확정한 타깃 몸을 놓거나(경로가 바뀌었을 때만) 치우고, 효과 오브젝트를 몸에 붙이거나 원점으로 되돌립니다. */
        void syncTarget( Scene& scene, VFXComponent& subject );

    private:
        vector<VFXPreviewEmitterRow> _listEmitterRow;
        string                       _previewEffectPath; ///< 미리보기 효과에 건 효과 경로(비면 다음 `syncEffect` 가 다시 건다)
        string                       _status;
        string                       _loadedDiskPath;      ///< 읽은 효과의 디스크 경로(자동 갱신이 내용 해시를 본다). 풀지 못했으면 비어 있다
        string                       _targetPrefabPath;    ///< 입력란의 타깃 프리팹 경로(Spawn 을 눌러야 적용한다)
        string                       _activeTargetPath;    ///< Spawn 으로 확정한 타깃 경로(비면 타깃 없음)
        uint64                       _loadedContentHash;   ///< 읽을 때의 내용 해시(0 = 아직 없음)
        GameObjectHandle             _target;              ///< 놓은 타깃 몸
        float32                      _orbitAngle;          ///< 타깃이 원을 도는 각(라디안)
        int32                        _soloEmitter;         ///< 격리한 방출기(`kNoSolo` 면 없음)
        uint8                        _bTargetAttached : 1; ///< 효과 오브젝트가 지금 타깃에 붙어 있다
        uint8                        _bTargetPending  : 1; ///< 다음 `syncPlayback` 이 확정 경로로 타깃을 다시 놓는다(Spawn · 새 씬). 실패해도 내린다 — 매 프레임 다시 놓지 않는다
        [[maybe_unused]] uint8       _reserved        : 6;
    };
} // namespace sw::editor
