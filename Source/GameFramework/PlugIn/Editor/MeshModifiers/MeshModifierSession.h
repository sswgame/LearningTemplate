/**
 * @file MeshModifierSession.h
 * @brief 메시 모디파이어 편집 세션 — 대상 메시 오브젝트, 스택, 되돌리기, 미리보기 정점, 사이드카 저장, 굽기를 한 곳에 둡니다(ImGui 를 모릅니다).
 * @details 이 확장이 기여하는 에디터 서브시스템이다(`SW_EDITOR_SUBSYSTEM`). 패널과 시각화가 `editor::getService<MeshModifierSession>()` 로 같이 본다.
 *          씬에는 아무것도 붙이지 않는다 — 스택은 메시 옆 사이드카 문서(`<메시>.modifiers.json`)에, 결과는 굽기로 새 `.mesh` 에셋이 된다.
 *          그래서 확장 모듈이 없는 런타임(게임 · Shipping)에서도 씬과 에셋이 그대로 읽힌다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace sw
{
    class GameObject;
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
        /** @brief 바뀌었으면 원본 정점에 스택을 다시 겁니다(CPU 정본 경로). */
        void refreshPreview();
        /** @brief 미리보기 결과의 월드 선분(삼각형 변)을 @p outListSegmentPoint 에 점 둘씩 채웁니다. */
        void makePreviewSegments( vector<float3>& outListSegmentPoint ) const;
        /** @brief 메시 로컬 → 월드 행렬입니다(대상이 없으면 단위 행렬). */
        float4x4 computeMeshToWorld() const;
        /** @brief 원본 메시의 로컬 경계 상자입니다. */
        const MeshModifierBounds& getSourceBounds() const { return _sourceBounds; }

        /** @brief 스택을 사이드카 문서에 씁니다. */
        [[nodiscard]] bool saveSidecar() const;
        /** @brief 사이드카 문서에서 스택을 다시 읽습니다. 없으면 false 입니다. */
        [[nodiscard]] bool loadSidecar();
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
        void makeSegments( const MeshModifierMesh& mesh, vector<float3>& outListSegmentPoint ) const;

        MeshModifierStack  _stack;
        MeshModifierMesh   _previewMesh;
        MeshModifierMesh   _stagePreviewMesh; ///< 고른 모디파이어까지만 건 결과(`makeStagePreviewSegments`)
        MeshAssetData      _sourceData;
        vector<string>     _listUndo;
        vector<string>     _listRedo;
        string             _meshPath;
        MeshModifierBounds _sourceBounds;
        GameObjectHandle   _target;
        float32            _previewTime;
        int32              _selectedIndex;
        MeshModifierBounds _inputBounds;       ///< 고른 모디파이어의 입력 상자 캐시(`getSelectedModifierInputBounds`)
        int32              _stagePreviewIndex; ///< `_stagePreviewMesh` 를 만든 고른 번호(-1 이면 없음)
        int32              _inputBoundsIndex;  ///< `_inputBounds` 를 만든 고른 번호(-1 이면 없음)
        int32              _selectedControlPoint;
        uint32             _mirrorAxis;
        bool               _bPreviewDirty;
        bool               _bStagePreviewDirty;
        bool               _bInputBoundsDirty;
        bool               _bMirrorEditing;
    };
} // namespace sw::editor
