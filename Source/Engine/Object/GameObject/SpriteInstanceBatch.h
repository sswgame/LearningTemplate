/**
 * @file SpriteInstanceBatch.h
 * @brief 컴포넌트 없이 스프라이트 N 장을 그리는 렌더 프리미티브입니다 — 월드 공간 UI(HP 바 · 데미지 숫자)가 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class MeshInstanceBatch;

    /**
     * @class SpriteInstanceBatch
     * @brief 사각형 메시 · 스프라이트 머티리얼 · 텍스처 하나로 그리는 스프라이트 N 장입니다. `MeshInstanceBatch` 를 감싸 머티리얼 · 텍스처 인스턴스를 잡고 놓습니다.
     * @details **왜 자식 `SpriteComponent` 가 아닌가.** UI 컴포넌트가 스프라이트 컴포넌트를 만들어 붙이면 (1) 그것들이 씬 · 프리팹 · 플레이 스냅샷 ·
     *          핫 리로드 상태에 **저장**되어, 다시 읽은 뒤 `onBeginPlay` 가 또 만들면 두 벌이 됩니다. (2) 틱 중에는 붙일 수 없어(`addComponent` 가
     *          미뤄져 nullptr) 데미지 숫자의 자릿수가 바뀔 때마다 구조 변경을 미뤄야 합니다. (3) 계층 창에 UI 조각이 오브젝트 · 컴포넌트로 보입니다.
     *          이 배치는 컴포넌트가 아니라 **저장되지 않고**, 항목 수가 정해진 뒤에는 값만 바뀌므로(쓰지 않는 자리는 숨깁니다) 틱 중에 써도 됩니다.
     *          항목은 메시 컴포넌트와 같은 길로 GpuScene 에 들어가고(`PrimitiveRegistry` · 부분 수집 · 투명 정렬), 같은 텍스처의 스프라이트 컴포넌트와
     *          머티리얼 인스턴스를 나눠 가져 한 배치로 묶입니다(`SpriteRenderUtil::acquireTextureInstance`). 언리얼이 월드 공간 위젯 대신
     *          Paper2D 그룹 스프라이트(`UPaperGroupedSpriteComponent`)로 그리는 자리입니다.
     *
     *          스레드: `initialize` · `shutdown` 은 게임 스레드의 병렬 틱 밖(`onBeginPlay` · `onEndPlay`)에서, 항목 세터는 이 배치를 가진 컴포넌트의
     *          틱에서 불러도 됩니다(더티 표시는 원자 비트이고 항목은 이 배치의 것뿐입니다).
     */
    class SW_API SpriteInstanceBatch
    {
    public:
        SpriteInstanceBatch();
        /** @brief 잡아 둔 것이 있으면 놓습니다(`shutdown`). */
        ~SpriteInstanceBatch();

        SpriteInstanceBatch( const SpriteInstanceBatch& )            = delete;
        SpriteInstanceBatch& operator=( const SpriteInstanceBatch& ) = delete;

        /**
         * @brief 항목 @p count 개를 만들어 @p manager 의 프리미티브 등록부에 넣습니다. 이미 있으면 먼저 놓습니다.
         * @param texturePath 비어 있으면 텍스처 없이 머티리얼의 색(흰색)에 항목 색을 곱한 단색 사각형입니다(HP 바).
         * @param normalMapPath 빛 받는 머티리얼(`sprite2dlit.material`)의 노멀 맵입니다. 비어 있으면 노멀 맵 없이(N·L 없이) 비춥니다.
         * @param materialPath 비어 있으면 스프라이트 머티리얼(`SpriteRenderUtil::getSpriteMaterialPath`)입니다. 타일맵은 점 필터 · 빛 받는 머티리얼을 고릅니다.
         * @details 스프라이트 머티리얼을 캐시에서 잡고(디바이스 업로드는 캐시가 맡습니다) 공유 사각형 메시를 씁니다. 엔진 서비스가 없으면
         *          (CPU 시험) 머티리얼 없이 만듭니다 — 항목 값은 그대로 확인할 수 있습니다. 항목은 처음에 모두 숨겨져 있습니다.
         * @return 항목을 만들었으면 true 입니다(@p count 가 0 이면 false).
         */
        [[nodiscard]] bool initialize( GameObjectManager& manager, string_view texturePath, uint32 count, string_view materialPath = {},
                                       string_view normalMapPath = {} );
        /** @brief 등록부에서 빼고 머티리얼을 놓습니다. 멱등입니다. */
        void shutdown();
        /** @brief 항목이 있으면 true 입니다. */
        bool isInitialized() const { return _batch != nullptr; }

        /** @brief 항목 수입니다. */
        uint32 getCount() const;
        /**
         * @brief 항목 하나의 월드 행렬 · UV 사각형 (u, v, 폭, 높이) · 색을 적고 보이게 합니다.
         * @details 사각형 메시는 한 변 1 이므로 월드 행렬의 X · Y 스케일이 곧 폭 · 높이입니다(`makeQuadWorld`).
         */
        void setEntry( uint32 index, const float4x4& world, const float4& uvRect, const float4& tint );
        /** @brief 항목 하나를 숨기거나 다시 보입니다(쓰지 않는 자릿수 · 길이 0 인 구간). */
        void setEntryVisible( uint32 index, bool bVisible );
        /** @brief 항목 하나가 보이면 true 입니다. */
        bool isEntryVisible( uint32 index ) const;
        /** @brief 항목 전체를 숨기거나 다시 보입니다(각 항목의 보임 여부는 그대로입니다). */
        void setVisible( bool bVisible );
        /** @brief 이 배치를 든 컴포넌트입니다(`MeshInstanceBatch::setOwnerComponent`). `initialize` 전에 불러도 됩니다 — 만들 때 넘깁니다. */
        void setOwnerComponent( const Component* pOwnerComponent );
        /** @brief 항목 모두를 더티로 표시합니다 — 든 컴포넌트의 활성이 바뀌었을 때. */
        void markAllEntriesDirty();

        /**
         * @brief 정렬 레이어 · 레이어 안 순서를 정합니다(`initialize` 앞뒤 어느 쪽이든). 모르는 레이어는 오류를 남기고 `Default` 입니다.
         * @details 월드 공간 UI 는 `WorldUI` 에 둡니다 — 같은 Z 의 스프라이트와 거리로 가리면 카메라가 움직일 때 앞뒤가 뒤집힙니다.
         */
        void setSorting( const hashed_string& layerName, int32 orderInLayer );

        /** @brief 감싼 배치입니다. 시험과 빌더가 항목을 읽습니다. 없으면 nullptr 입니다. */
        const MeshInstanceBatch* getBatch() const { return _batch.get(); }

        /** @brief 중심 @p center 에 폭 @p width · 높이 @p height 인 사각형의 월드 행렬입니다(회전 없음, XY 평면). */
        static float4x4 makeQuadWorld( const float3& center, float32 width, float32 height );

    private:
        unique_ptr<MeshInstanceBatch> _batch;
        const Component*              _pOwnerComponent;      ///< 든 컴포넌트(빌림). 배치를 다시 만들어도 넘깁니다
        hashed_string                 _acquiredMaterialPath; ///< 캐시에서 잡은 머티리얼 경로입니다. 비어 있으면 잡은 것이 없습니다
        uint32                        _sortKey;              ///< 항목 모두의 투명 정렬 키(0 = 기본). 배치를 다시 만들어도 남습니다
    };
} // namespace sw
