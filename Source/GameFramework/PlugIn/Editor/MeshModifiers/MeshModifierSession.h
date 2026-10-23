/**
 * @file MeshModifierSession.h
 * @brief 메시 모디파이어 편집 세션 — 대상 메시 오브젝트, 스택, 되돌리기, 미리보기 정점, 사이드카 저장, 베이크를 한 곳에 둡니다(ImGui 를 모릅니다).
 * @details 이 확장이 기여하는 에디터 서브시스템이다(`SW_EDITOR_SUBSYSTEM`). 패널과 시각화가 `editor::findSubsystem<MeshModifierSession>()` 로 같이 본다.
 *          씬에는 아무것도 붙이지 않는다 — 스택은 메시 옆 사이드카 문서(`<메시>.modifiers.json`)에, 결과는 베이크로 새 `.mesh` 에셋이 된다.
 *          그래서 확장 모듈이 없는 런타임(게임 · Shipping)에서도 씬과 에셋이 그대로 읽힌다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace sw
{
    class GameObject;
    class Mesh;
    class MeshComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class MeshModifierSession
     * @brief 대상 하나의 모디파이어 편집 상태입니다. 바깥 입력(스플라인 · 다른 메시 · 오브젝트 변환)도 이 세션이 씬에서 읽어 줍니다.
     */
    class MeshModifierSession final : public EditorSubsystem, public IMeshModifierInputs
    {
    public:
        static constexpr const utf8* kSubsystemName = "MeshModifierSession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 되돌리기 기록의 최대 수입니다. */
        static constexpr size_t kMaxUndoCount = 64;
        /** @brief 미리보기 선을 그릴 최대 삼각형 수입니다. 넘으면 고르게 건너뜁니다. */
        static constexpr size_t kMaxPreviewTriangleCount = 20000;

        MeshModifierSession();

        /** @brief 대상 메시 컴포넌트에 걸어 둔 미리보기 메시를 원래 메시로 되돌립니다(모듈이 내려가도 씬에 미리보기가 남지 않게). */
        void shutdownSubsystem() override;

        /**
         * @brief 대상을 바꿉니다. 메시 컴포넌트의 `.mesh` 를 읽고, 사이드카가 있으면 스택을 읽습니다. 실패하면 false 이고 대상은 비웁니다.
         * @details 내장 도형("Cube" 등)은 `.mesh` 가 아니라 대상이 될 수 없다.
         */
        [[nodiscard]] bool setTarget( GameObject* pObject );
        void               clearTarget();
        /** @brief 대상 오브젝트입니다. 지워졌으면 nullptr 입니다. */
        GameObject* findTarget() const;
        bool        hasTarget() const { return _meshPath.empty() == false; }

        /** @brief 지금 스택 상태를 되돌리기 기록에 넣습니다. 편집 직전에 부릅니다. */
        void recordUndo();
        /** @brief 한 단계 되돌립니다. 기록이 없으면 false 입니다. */
        [[nodiscard]] bool undo();
        /** @brief 되돌린 것을 다시 합니다. */
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _listUndo.empty() == false; }
        bool               canRedo() const { return _listRedo.empty() == false; }

        /** @brief 스택이 바뀌었다고 알립니다. 다음 `refreshPreview` 가 다시 겁니다. */
        void markDirty() { _bPreviewDirty = true; }
        /**
         * @brief 바뀌었으면 원본 정점에 스택을 다시 겁니다(CPU 기준 경로). GPU 미리보기가 켜져 있으면 같은 적용에서 모은 입력 상자로 변형 프로그램도 다시 겁니다.
         * @details 고른 모디파이어 앞까지 건 결과(앞부분 캐시)를 들고, 앞부분 모디파이어의 저장 글 · 시간 · 메시 변환이 그대로면 고른 것부터만 다시 건다.
         *          앞부분이 읽는 씬 입력(참조 오브젝트 변환 · 곡선 · 텍스처 파일)은 캐시를 만들 때의 값이다 — 앞부분이나 고른 번호가 바뀌면 다시 읽는다.
         */
        void refreshPreview();

        /**
         * @brief 셰이딩 미리보기 — 대상 메시 컴포넌트에 스택 결과 메시를 바꿔 끼워 뷰포트가 셰이딩된 결과를 그리게 합니다.
         * @details 켜진 모디파이어가 모두 GPU 표에 있으면(`MeshModifierGPUProgram::canMake`) 레스트 정점 + GPU 모프 + 변형 프로그램으로 그린다.
         *          매개변수만 바뀌면 프로그램만 다시 건다(정점 버퍼를 다시 만들지 않는다). 아니면 CPU 결과 정점을 올린 메시를 바뀔 때마다 새로 끼운다.
         *          메시 컴포넌트의 런타임 지정(`setMesh`)이라 저장되지 않는다. 끄면 원래 메시와 와이어 미리보기만 남는다. 기본은 켬이다.
         */
        void setShadedPreviewEnabled( bool bEnabled );
        bool isShadedPreviewEnabled() const { return _bShadedPreview; }
        /** @brief GPU 변형 프로그램 미리보기가 지금 대상에 걸려 있으면 true 입니다(CPU 결과 메시가 걸려 있으면 false). */
        bool isGPUPreviewActive() const { return _originalMesh != nullptr && _gpuPreviewMesh != nullptr && _cpuPreviewMesh == nullptr; }
        /** @brief GPU 로 못 옮긴 첫 모디파이어 번호입니다(-1 이면 막는 모디파이어가 없거나 스택 설정이 막는다). 패널 안내에 씁니다. */
        int32 getGPUPreviewBlockingIndex() const { return _gpuBlockingIndex; }
        /** @brief 미리보기 결과의 월드 선분(삼각형 변)을 @p outListSegmentPoint 에 점 둘씩 채웁니다. */
        void makePreviewSegments( vector<float3>& outListSegmentPoint ) const;
        /** @brief 메시 로컬 → 월드 행렬입니다(대상이 없으면 단위 행렬). */
        float4x4 computeMeshToWorld() const;
        /** @brief 원본 메시의 로컬 경계 상자입니다. */
        const MeshModifierBounds& getSourceBounds() const { return _sourceBounds; }

        /** @brief 스택을 사이드카 문서에 씁니다. 성공하면 지금 스택이 저장된 상태가 됩니다(`isStackUnsaved`). */
        [[nodiscard]] bool saveSidecar();
        /** @brief 사이드카 문서에서 스택을 다시 읽습니다. 없으면 false 입니다. 읽으면 그 스택이 저장된 상태가 됩니다. */
        [[nodiscard]] bool loadSidecar();
        /**
         * @brief 스택이 마지막으로 읽거나 쓴 사이드카(없었으면 빈 스택)와 다르면 true 입니다. 패널의 미저장 표시 · 닫기 확인이 이 값을 본다.
         * @details 저장 글(`MeshModifierStack::makeJSONText`)끼리 비교한다 — 되돌리기로 저장한 상태에 돌아오면 다시 false 다. 대상이 없으면 false 입니다.
         */
        bool isStackUnsaved() const;
        /** @brief "Don't Save" — 스택을 마지막으로 읽거나 쓴 상태로 돌립니다(되돌리기 기록에 남긴다). 대상이 없으면 아무것도 하지 않습니다. */
        void revertStackToSaved();
        /**
         * @brief 스택을 건 결과를 새 메시 에셋(`<메시>_modified.mesh`)으로 씁니다. 원본은 덮어쓰지 않습니다.
         * @details 스킨 가중치는 정점 수가 같아 그대로 옮긴다. 모프 타깃은 위치가 바뀌어 차이가 맞지 않으므로 버리고 경고한다.
         * @param pOutPath 쓴 리소스 id 를 받습니다(선택).
         */
        [[nodiscard]] bool bake( string* pOutPath ) const;

        MeshModifierStack&       getStack() { return _stack; }
        const MeshModifierStack& getStack() const { return _stack; }
        const string&            getMeshPath() const { return _meshPath; }
        int32                    getSelectedIndex() const { return _selectedIndex; }
        void                     setSelectedIndex( int32 index ) { _selectedIndex = index; }
        float32                  getPreviewTime() const { return _previewTime; }
        void                     setPreviewTime( float32 time );
        /** @brief 미리보기 시간 칸의 상한(초)입니다. 재생은 여기서 0 으로 되감습니다. */
        static constexpr float32 kMaxPreviewTimeSeconds = 600.0f;
        /** @brief 미리보기 시간을 재생하는지입니다. 켜면 `advancePreviewTime` 이 시간을 흘려 애니메이션 모디파이어가 움직입니다. */
        bool isPreviewPlaying() const { return _bPreviewPlaying; }
        void setPreviewPlaying( bool bPlaying ) { _bPreviewPlaying = bPlaying; }
        /** @brief 재생 중이면 미리보기 시간을 @p deltaSeconds 만큼 흘립니다(패널이 프레임마다 부릅니다). */
        void advancePreviewTime( float32 deltaSeconds );

        /**
         * @brief 고른 모디파이어까지만 건 결과의 월드 선분을 @p outListSegmentPoint 에 점 둘씩 채웁니다(변형 영향 미리보기).
         * @details 고른 모디파이어가 없거나 마지막이면 비웁니다 — 전체 결과(`makePreviewSegments`)와 같기 때문이다. 결과는 스택이 바뀌거나 고른 번호가 바뀔 때만 다시 건다.
         */
        void makeStagePreviewSegments( vector<float3>& outListSegmentPoint );
        /**
         * @brief 고른 모디파이어의 입력 메시(앞선 켜진 모디파이어까지 건 결과)의 로컬 경계 상자입니다. 범위 · 격자 상자를 그릴 때 씁니다.
         * @details 고른 것이 없거나 첫 모디파이어면, 또는 결과가 무효면 원본 상자입니다. 스택이나 고른 번호가 바뀔 때만 다시 계산하고,
         *          단계 미리보기 캐시가 바로 앞까지의 결과면 그것을 재사용한다.
         */
        const MeshModifierBounds& getSelectedModifierInputBounds();

        /** @brief 뷰포트와 패널이 함께 보는 격자 제어점 번호입니다. 고른 점이 없으면 -1 입니다(패널은 읽기만 하고 범위는 그리는 쪽이 자릅니다). */
        int32 getSelectedControlPoint() const { return _selectedControlPoint; }
        void  setSelectedControlPoint( int32 index ) { _selectedControlPoint = index; }
        /** @brief 제어점을 옮길 때 대칭 짝도 함께 옮기는지입니다(패널의 Mirror 체크와 뷰포트 끌기가 같이 씁니다). */
        bool isMirrorEditing() const { return _bMirrorEditing; }
        void setMirrorEditing( bool bMirrorEditing ) { _bMirrorEditing = bMirrorEditing; }
        /** @brief 대칭 축 성분 번호(0 = X, 1 = Y, 2 = Z)입니다. */
        uint32 getMirrorAxis() const { return _mirrorAxis; }
        void   setMirrorAxis( uint32 axisIndex ) { _mirrorAxis = axisIndex <= 2u ? axisIndex : 0u; }

        // IMeshModifierInputs
        [[nodiscard]] bool tryGetWorldMatrix( string_view reference, float4x4& outMatrix ) const override;
        [[nodiscard]] bool tryGetCurveLength( string_view reference, float32& outLength ) const override;
        [[nodiscard]] bool trySampleCurve( string_view reference, float32 fraction, float3& outPosition, float3& outTangent ) const override;
        [[nodiscard]] bool tryGetMeshTriangles( string_view reference, vector<float3>& outListPosition ) const override;
        [[nodiscard]] bool trySampleTexture( string_view assetPath, const float2& uv, float4& outColor ) const override;

    private:
        void makeSourceMesh( MeshModifierMesh& outMesh ) const;
        /** @brief 미리보기 평가(메시 변환 · 씬 입력 · 시간 · 스택 입력 위치)를 만듭니다. */
        MeshModifierEvaluation makeEvaluation() const;
        /**
         * @brief 앞부분 캐시(`_prefixMesh` = 고른 모디파이어 앞까지 건 결과)를 맞춥니다.
         * @param bStackChanged 스택 · 시간이 바뀌었을 수 있으면 true(앞부분 저장 글을 비교한다). false 면 고른 번호가 바뀌었을 때만 비교한다.
         */
        void updatePrefixCache( const MeshModifierEvaluation& evaluation, bool bStackChanged );
        void makeSegments( const MeshModifierMesh& mesh, vector<float3>& outListSegmentPoint ) const;
        /** @brief 방금 건 CPU 적용의 평가 · 입력 상자로 셰이딩 미리보기를 맞춥니다(GPU 로 못 옮기면 CPU 결과 메시를 끼운다). */
        void updateShadedPreview( const MeshModifierEvaluation& evaluation, const vector<MeshModifierBounds>& listInputBounds );
        /** @brief 미리보기 메시 @p mesh 를 대상에 끼웁니다. 대상에 우리 것이 아닌 메시가 걸려 있으면 그것을 원래 메시로 잡아 둡니다. */
        void mountPreviewMesh( MeshComponent& meshComponent, const shared_ptr<Mesh>& mesh );
        /** @brief 대상에 끼워 둔 미리보기 메시를 원래 메시로 되돌립니다. 대상이 지워졌으면 잡아 둔 포인터만 놓습니다. */
        void restoreOriginalMesh();

        MeshModifierStack          _stack;
        shared_ptr<Mesh>           _gpuPreviewMesh; ///< 레스트 정점 + GPU 모프(엔진이 만든 메시라 모듈이 내려가도 소멸 코드는 엔진에 있다)
        shared_ptr<Mesh>           _cpuPreviewMesh; ///< CPU 결과 정점(GPU 로 못 옮기는 스택일 때만 있다)
        shared_ptr<Mesh>           _originalMesh;   ///< 미리보기를 끼우기 전 대상 컴포넌트의 메시
        vector<float4>             _listGPUProgram;
        int32                      _gpuBlockingIndex;
        bool                       _bShadedPreview;
        MeshModifierMesh           _previewMesh;
        MeshModifierMesh           _stagePreviewMesh;      ///< 고른 모디파이어까지만 건 결과(`makeStagePreviewSegments`)
        MeshModifierMesh           _prefixMesh;            ///< 고른 모디파이어 앞까지 건 결과(앞부분 캐시, `updatePrefixCache`)
        vector<MeshModifierBounds> _listPrefixInputBounds; ///< 앞부분을 거는 동안 모은 입력 상자(GPU 프로그램용)
        string                     _prefixKey;             ///< 앞부분 모디파이어의 한 줄 저장 글(`MeshModifierStack::makePrefixJSONText`)
        float4x4                   _prefixMeshToWorld;     ///< 앞부분을 건 메시 변환
        float32                    _prefixTime;            ///< 앞부분을 건 미리보기 시간
        int32                      _prefixEnd;             ///< 앞부분의 끝 번호(-1 이면 캐시 없음)
        MeshAssetData              _sourceData;
        vector<float3>             _listSourcePosition; ///< 원본 정점 위치(스택 입력 위치 · 원본 상자)
        vector<string>             _listUndo;
        vector<string>             _listRedo;
        string                     _savedStackText; ///< 마지막으로 읽거나 쓴 사이드카의 스택 저장 글(`isStackUnsaved` 의 기준)
        string                     _meshPath;
        MeshModifierBounds         _sourceBounds;
        GameObjectHandle           _target;
        float32                    _previewTime;
        int32                      _selectedIndex;
        MeshModifierBounds         _inputBounds;       ///< 고른 모디파이어의 입력 상자 캐시(`getSelectedModifierInputBounds`, 앞부분 캐시에서 센다)
        int32                      _stagePreviewIndex; ///< `_stagePreviewMesh` 를 만든 고른 번호(-1 이면 없음)
        int32                      _selectedControlPoint;
        uint32                     _mirrorAxis;
        bool                       _bPreviewDirty;
        bool                       _bStagePreviewDirty;
        bool                       _bInputBoundsDirty;
        bool                       _bMirrorEditing;
        bool                       _bPreviewPlaying;
    };
} // namespace sw::editor
