/**
 * @file GPUSceneSnapshot.h
 * @brief 게임 스레드가 만들고 렌더 스레드가 받는 씬 스냅샷입니다. 두 스레드가 공유하는 **유일한** 타입입니다.
 * @details 만드는 쪽은 `GPUSceneBuilder`(GT)이고, 받아서 GPU 에 올리는 쪽은 `GPUScene`(RT)이다. 두 클래스는 서로를 모르고
 *          이 구조체만 안다. 그래서 "GT 가 RT 의 값을 덮어쓴다", "RT 가 씬을 읽는다" 같은 실수는 컴파일되지 않는다.
 *          소유 규칙(shared_ptr 로 소유권을 함께 싣는다)은 `GPUSceneSnapshot` 주석을 참고한다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Name/hashed_string.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Presentation/InstanceCustomData.h"
#include "Engine/VFX/VFXParticle.h"

#include <variant>

namespace sw
{
    class Material;
    class MaterialInstance;
    class Mesh;

    /// @brief 배치에 머티리얼 데이터 그룹이 없음을 나타냅니다(GPUMeshBatch::_materialGroup).
    inline constexpr uint32 kInvalidMaterialGroup = constant::invalid_index::kUint32;
    /// @brief 배치에 셰이더 퍼뮤테이션이 없음을 나타냅니다. 이 경우 패스가 자기 PSO 로 그립니다(GPUMeshBatch::_shaderPermutation).
    inline constexpr uint32 kInvalidShaderPermutation = constant::invalid_index::kUint32;

    /**
     * @brief GPU 인스턴스 하나를 나타냅니다. 월드 행렬, 바운드, 배치 인덱스, 머티리얼 원소 인덱스, 블렌드, 회전 시드, 스프라이트 프레임과 색을 가집니다.
     * @details HLSL 쪽은 `Content/engine/shaders/instance_data.hlsli` 의 `InstanceData` 하나이며 그래픽스와 컴퓨트가 함께 쓴다.
     *          필드를 고치면 그 파일과 ShaderBindingValidatorTest.InstanceElementLayoutMatchesCPUStruct 의 테이블을 함께 고친다(쿠킹된 바이너리로 대조한다).
     */
    struct GPUInstance
    {
        float4x4 _world{};
        float3   _boundsCenter{};
        float32  _boundsRadius{ 1.0f };
        uint32   _meshBatchIndex{ 0 };
        uint32   _materialIndex{ 0 };
        uint32   _blendMode{ 0 }; ///< BlendMode
        /**
         * @brief GPU 인스턴스 애니메이션 시드입니다. 0 이면 애니메이션이 없습니다.
         * @details instance_animation.hlsl 이 이 값을 해시해 **인스턴스마다 다른 각속도**를 만든다. 회전은 CPU 가 매 프레임
         *          계산해 올리지 않고 GPU 가 계산한다.
         */
        uint32 _spinSeed{ 0 };
        /**
         * @brief 스프라이트 프레임(UV 사각형)과 색입니다. 지금 이 값을 읽는 셰이더는 sprite_2d.hlsl 하나입니다.
         * @details 머티리얼 인스턴스가 아니라 여기에 싣는 이유는 `GPUSpriteInstanceData` 주석에 있다. 배치 키(머티리얼 인스턴스)를 건드리지
         *          않으므로 같은 텍스처의 스프라이트는 프레임과 색이 달라도 한 배치이고, 값이 바뀌면 그 인스턴스만 더티 구간으로 올라간다.
         */
        GPUSpriteInstanceData _sprite{};
        /**
         * @brief 정점 애니메이션(VAT) 시간 오프셋(초)입니다. VAT 가 걸린 메시만 읽습니다(binding.hlsli `InstanceData::loadAnimatedVertex`).
         * @details 셰이더는 VAT 시계(`GPUSceneSnapshot::_vertexAnimationTime`)에 이 값을 더한 시각의 프레임을 그린다. 군중 시스템이 VAT 로 넘길 때
         *          그 캐릭터의 클립 시각에 맞춰 한 번 기록하고, 그 뒤로는 바뀌지 않아 인스턴스를 다시 올리지 않는다. 셰이더의 `vertexAnimationPhase` 에 해당한다.
         */
        float32 _vertexAnimationPhase{ 0.0f };
        /** @brief 강조(상호작용 외곽선 · 감각 모드)입니다(`GPUInstanceHighlight::pack`, 0 이면 없음). 셰이더의 `highlight` 이고 `highlight_outline.hlsl` 이 읽습니다. */
        uint32 _highlight{ 0 };
        /**
         * @brief 등장 비율(0 ~ 1)입니다. 1 이면 다 보인다. 1 보다 작은 인스턴스는 디더 변형(`kAppearanceDitherDefine`)으로 그려져 그 비율만큼의 픽셀만 남는다.
         * @details 로드가 끝난 메시 · 텔레포트 · 사라지기가 이 값을 올리고 내린다(`MeshComponent::beginAppearance`). 디더 임계값은 화면 픽셀의 4x4 Bayer 행렬이라
         *          깊이 프리패스 · 그림자 · 기본 패스가 같은 픽셀을 버린다(binding.hlsli `Appearance::clipDither`). 셰이더의 `appearance` 에 해당한다.
         */
        float32 _appearance{ 1.0f };
        /**
         * @brief 스킨 칸(`MeshComponent::getSkinSlot` — 유닛 · 군중 묶음마다 게임 스레드가 준 오래 가는 번호)입니다. 스킨 결과가 없으면 무효입니다. 셰이더의 `skinSlot` 에 해당한다.
         * @details 스킨 결과는 메시가 아니라 칸마다 하나라(모프 풀이 칸마다 결과 구간, RT 의 칸 → 시작 표 `g_SwSkinResultOffsets`) 같은 메시를 그리는 캐릭터들이 한 배치다.
         */
        uint32 _skinSlot{ constant::invalid_index::kUint32 };
        /** @brief 인스턴스 사용자 값입니다(`InstanceCustomData` — 담는 법 없는 실수라 컴포넌트의 형식을 그대로 싣는다). 스프라이트 칸처럼 내용이고 배치 키가 아닙니다. 셰이더의 `customData0` · `customData1` 입니다. */
        InstanceCustomData _customData{};
        /**
         * @brief 움직임 기록의 열쇠입니다(`MeshComponent::getMotionHistoryKey` — 컴포넌트 일련번호 + 순간이동 차례). 0 이면 기록이 없다(인스턴스 배치 항목 · GPU 가 쓰는 칸).
         * @details 렌더 쪽(`GPUInstanceMotionHistory`)이 이 열쇠로 지난번 그린 월드 행렬을 찾아 화면 속도의 지난 위치를 만든다. 인스턴스 번호는 배치를 다시 나누면 바뀌므로 열쇠가
         *          따로 있다(언리얼 프리미티브 프록시가 PreviousLocalToWorld 를 들고 있는 것과 같은 자리 — 여기서는 게임 스레드가 정체성만 싣고 기록은 렌더 쪽이 소유한다).
         *          순간이동이 차례를 올려 열쇠가 바뀌면 기록이 없는 것으로 보여 그 프레임의 속도가 카메라 움직임뿐이다. 셰이더는 읽지 않는다.
         */
        uint64 _motionKey{ 0 };
        /** @brief 원소를 16 바이트 단위로 맞추는 칸입니다(HLSL 구조 버퍼 원소 정렬). 0 으로 둔다. */
        uint64 _motionPad{ 0 };
    };
} // namespace sw

namespace sw
{
    /// @brief GPU 인스턴스 LOD 그룹이 받는 메시의 최대 단계 수(LOD 0 포함)입니다. 더 많으면 그 메시는 CPU 가 단계를 고른다(`GPUSceneBuilder::applyGPULODGroup`).
    inline constexpr uint32 kGPULODMaxLevels = 8;

    static_assert( sizeof( GPUInstance ) == 176, "GPUInstance must match InstanceData (instance_data.hlsli) byte for byte" );
    static_assert( sizeof( InstanceCustomData ) == sizeof( float4 ) * InstanceCustomData::kCount, "InstanceCustomData must be exactly the float4 slots of InstanceData" );

    /**
     * @brief 같은 메시, 머티리얼, 퍼뮤테이션으로 그리는 인스턴스 배치입니다. 게임 스레드가 정한 값만 든다.
     * @details 렌더 스레드가 쓰는 GPU 값(정점 · 인덱스 버퍼 · 풀 시작 · 머티리얼 버퍼 · CB · 텍스처 슬롯)은 `GPUMeshBatchBinding` 으로 떼었다 — 그래서 배치 목록을
     *          불변 공유 값으로 실을 수 있다(`GPUSceneSnapshot::getAllBatches`).
     */
    struct GPUMeshBatch
    {
        shared_ptr<Mesh> _mesh;            ///< **소유권을 함께 싣는다.** RT 가 upload() 에서 이 메시를 역참조한다.
        uint32           _indexCount{ 0 }; ///< 이 배치가 그리는 인덱스 수(구간 · LOD 단계, 아니면 LOD 0 전체)
        /// @brief 이 배치가 그리는 메시 구간 · LOD 단계의 첫 인덱스(메시의 그릴 인덱스 흐름 안 번호)입니다. 메시 전체면 0 입니다. RT 가 인덱스 풀 시작에 더한다(`GPUMeshBatchBinding::_firstIndex`).
        uint32 _sectionFirstIndex{ 0 };
        /**
         * @brief 인스턴스 배열 안에서 이 배치의 구간입니다. 배치의 인스턴스는 연속이고 구간은 앞 배치의 끝에서 빈틈없이 이어집니다.
         * @details 인스턴스 구간과 가시 구간(`_visibleBase`)은 **다른 축**이다. LOD 그룹의 낮은 단계 배치는 인스턴스를 가지지 않고(`_instanceCount` 0) 그룹의 인스턴스를
         *          공유하지만 컬링이 단계마다 가시 칸을 쓰므로 자기 가시 구간이 있다.
         */
        uint32 _instanceBase{ 0 };
        uint32 _instanceCount{ 0 };
        /**
         * @brief 가시 목록(컬링이 압축해 쓰는 인스턴스 번호 목록 `g_VisibleInstanceIDs`) 안에서 이 배치 구간의 시작입니다. 구간은 앞 배치의 끝에서 빈틈없이 이어집니다.
         * @details 간접 인자의 시작 인스턴스(`startInstanceLocation`)가 되고, 컬링이 `_visibleBase + 자리` 에 쓴다. LOD 그룹이 없으면 `_instanceBase` 와 같다.
         */
        uint32 _visibleBase{ 0 };
        /// @brief 가시 구간의 길이(이 배치에 보일 수 있는 인스턴스 수의 상한)입니다. LOD 그룹이 없으면 `_instanceCount` 와 같다.
        uint32 _visibleCapacity{ 0 };
        /**
         * @brief LOD 그룹의 머리 배치(단계 0)에만 있는 값으로 그룹의 단계 수입니다. 0 이면 그룹이 아닙니다(CPU 가 단계를 고르거나 단계가 하나뿐인 메시).
         * @details 그룹의 단계 배치는 머리 바로 뒤에 `_lodLevelCount - 1` 개 이어지고(같은 머티리얼 · 같은 메시, 정점 구간만 단계의 것) 인스턴스는 머리 배치 번호를
         *          `_meshBatchIndex` 로 가진다. 컬링(`gpu_cull.hlsl`)이 인스턴스마다 화면 크기로 단계를 골라 그 배치의 가시 구간에 쓴다.
         */
        uint32 _lodLevelCount{ 0 };
        /// @brief 그룹의 낮은 단계(1 이상) 배치에만 있는 값으로, 이 단계로 넘어가는 화면 크기 임계값입니다(`MeshLOD::_screenSize`).
        float32 _lodScreenSize{ 0.0f };
        /** @brief 배치 머리 인스턴스의 머티리얼 원소(`_materialGroup` 그룹의 `_listEntry` 인덱스)입니다. RT 가 이 원소의 (프록시, 사본)으로 `GPUMeshBatchBinding` 의 CB 와 텍스처 슬롯을 채운다. */
        uint32    _materialIndex{ 0 };
        BlendMode _blendMode = BlendMode::Opaque;
        /**
         * @brief 인스턴스의 월드 행렬식이 음수(거울 변환)인 배치입니다. 이 배치는 컬 모드를 뒤집은 PSO 로 그립니다(언리얼 `bReverseCulling`).
         * @details 거울 변환은 삼각형 감김 방향을 뒤집는다. 컬 모드를 그대로 두면 카메라 쪽 면이 잘리고 안쪽 면이 보인다. 컬 모드는 PSO 의 상태라
         *          배치 키에 들어간다. 한 배치의 인스턴스는 모두 같은 부호다(`FramePSOManager::psoForBatch`).
         */
        uint8 _bReverseCulling{ SW_FALSE };
        /**
         * @brief 정적 메시(`MeshComponent::isStatic`)의 배치입니다. 배치 키라 불투명 배치는 정적과 움직이는 것이 섞이지 않는다(반투명은 그림자 패스가 그리지 않아 섞일 수 있다).
         * @details 그림자 캐시가 정적 배치만 캐시 층에, 움직이는 배치만 그 위에 그린다(`FramePassExecutor::drawShadowTiles`).
         */
        uint8 _bStaticShadowCaster{ SW_FALSE };
        /** @brief 머티리얼 데이터 그룹(셰이더 타입) 인덱스입니다(`GPUSceneSnapshot::getMaterialGroups`). 없으면 kInvalidMaterialGroup 입니다. */
        uint32 _materialGroup{ 0xFFFFFFFFu };
        /**
         * @brief 이 배치를 그릴 셰이더 퍼뮤테이션입니다. `_pListShaderPermutation` 의 인덱스이며 `GPUScene::findShaderPermutation` 으로 찾습니다.
         * @details 없으면 kInvalidShaderPermutation 이고, 그때는 패스가 자기 PSO 로 그린다. 배치 키에 이 퍼뮤테이션
         *          해시가 들어가므로, 한 배치의 인스턴스는 반드시 모두 같은 셰이더다.
         */
        uint32 _shaderPermutation{ 0xFFFFFFFFu };
        /**
         * @brief 이 배치가 그릴 인스턴스 칸을 가질 수 있으면 true 입니다(가시 구간이 비지 않았다).
         * @details CPU 인스턴스 수(`_instanceCount`)가 아니라 이것으로 거른다. LOD 그룹의 낮은 단계 배치는 인스턴스를 가지지 않지만(`_instanceCount` 0) 컬링이 채우는 구간이 있다.
         */
        bool hasDrawSlots() const { return _visibleCapacity != 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUMaterialElementKey
     * @brief 머티리얼 데이터 원소 하나를 가리키는 키입니다. (머티리얼, 인스턴스) 쌍입니다.
     * @details 인스턴스가 없으면 머티리얼 자신이 원소다. 인스턴스는 CB 값만 덮어쓰므로 부모 머티리얼과 함께 봐야 한다.
     */
    // SW_OWNERSHIP_RAW_OK( _pMaterial, _pInstance ): 정체성 키다. 비교만 하고 **역참조하지 않는다**. 소유권을 실으면 키가 수명을 붙들어,
    //                                               회수되어야 할 머티리얼이 스냅샷이 사는 동안 살아남는다(그것이 원소 테이블이 따로 있는 이유다).
    struct GPUMaterialElementKey
    {
        Material*         _pMaterial{ nullptr };
        MaterialInstance* _pInstance{ nullptr };
        /** @brief 두 포인터가 모두 같으면 true 를 반환합니다. */
        bool operator==( const GPUMaterialElementKey& other ) const
        {
            return _pMaterial == other._pMaterial && _pInstance == other._pInstance;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUMaterialElement
     * @brief 머티리얼 데이터 원소 하나입니다. (머티리얼, 인스턴스) 쌍의 정체성과 렌더 쪽 (프록시, 사본)입니다.
     * @details 키는 정체성이라 생포인터이고 역참조하지 않는다. 렌더 스레드가 읽는 것은 `_binding` 뿐이다 — Material 객체는 패킷에 실리지 않는다.
     *          게임 쪽 소유(사본을 새로 만들 대상)는 레지스트리가 가진다(`GPUMaterialRegistry::refreshMaterialBindings`). 인스턴스가 없으면 머티리얼 자신이 원소다.
     *          `_key._pMaterial` 이 nullptr 이면 회수된 빈 슬롯이다.
     */
    struct GPUMaterialElement
    {
        GPUMaterialElementKey _key;
        MaterialRenderBinding _binding;
    };
} // namespace sw

namespace sw
{
    /// @brief GPUMaterialElementKey 의 해시입니다. 포인터 둘을 섞어 만듭니다.
    struct GPUMaterialElementKeyHash
    {
        /** @brief 키의 해시를 반환합니다. */
        size_t operator()( const GPUMaterialElementKey& key ) const
        {
            size_t hash = reinterpret_cast<size_t>( key._pMaterial ) * 1315423911u;
            hash ^= reinterpret_cast<size_t>( key._pInstance ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
            return hash;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUShaderPermutation
     * @brief 머티리얼이 요구하는 셰이더 변형 하나입니다. 경로와 정적 define 으로 이루어집니다.
     * @details 언리얼의 `FMaterialShaderMap` 에 해당한다. 같은 .hlsl 이라도 정적 스위치가 다르면 **다른 셰이더**라서
     *          다른 PSO 로 그려야 한다. 주의: 셰이더 **경로**만 보면 유리 머티리얼처럼 always-define 을 가진 것이
     *          불투명 머티리얼과 한 배치로 합쳐지고, 배치는 PSO 하나로 그리므로 그 정보가 그냥 버려진다.
     *
     *          렌더 스레드는 씬을 볼 수 없으므로(Material* 을 따라갈 수 없다) 값으로 실어 보낸다.
     */
    struct GPUShaderPermutation
    {
        /// @brief 머티리얼이 선언한 셰이더입니다. 패스가 자기 셰이더를 쓰는 경우(그림자, 뎁스)에는 무시됩니다.
        string _shaderPath;
        /// @brief always-define, 정적 스위치, 멀티컴파일 선택을 합친 것입니다. 그대로 PSO 의 셰이더 define 이 됩니다.
        vector<string> _listDefine;
        /// @brief 머티리얼이 패스 위에 덮는 렌더 상태입니다(`Material::getRenderStateOverride`). 패스 → 머티리얼 순서로 합쳐 변형 PSO 를 만듭니다(`FramePSOManager::createMaterialPSOVariant`).
        RenderStateOverride _renderStateOverride{};
        /// @brief (경로, define, 렌더 상태) 해시입니다. PSO 캐시 키이자 **배치 병합 키**입니다.
        uint64 _hash{ 0 };
        /**
         * @brief 이 변형을 처음 낸 머티리얼이 스위치를 바꾸기 직전에 쓰던 퍼뮤테이션 해시입니다(없으면 0). 배치 키가 아니다.
         * @details 새 변형의 PSO 가 빌더에서 `Building` 인 동안 드로우가 직전 변형의 `Ready` PSO 로 그린다(`FramePSOManager::psoForBatch`) — 물체가 사라지지 않고 한두 프레임 늦게 바뀐다
         *          (언리얼 PSO 프리캐시의 "준비 안 된 PSO 는 기본 머티리얼로" 보다 덜 튄다). 같은 변형을 다른 머티리얼이 함께 쓰면 처음 낸 쪽의 직전 변형이다 — 경로가 같아
         *          셰이더는 같고 define 만 다르며, 그 몇 프레임의 차이만 남는다.
         */
        uint64 _fallbackHash{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUMaterialGroup
     * @brief 셰이더 타입(머티리얼 셰이더 경로)별 머티리얼 데이터 원소 목록입니다. CPU 스냅샷의 일부입니다.
     * @details 원소 순서가 곧 materialIndex 다. 같은 셰이더를 쓰는 머티리얼과 인스턴스는 구조체 레이아웃이 같아 한 버퍼에 쌓인다.
     */
    struct GPUMaterialGroup
    {
        string                     _shaderPath;
        vector<GPUMaterialElement> _listEntry;
        // 원소 인덱스 테이블(키에서 인덱스, 마지막 사용 빌드, 프리리스트, 직전 조회)은 **빌더의 것**이다
        // (`GPUSceneBuilder::MaterialGroupState`). 스냅샷에는 RT 가 읽는 것만 싣는다. 테이블을 여기 두면
        // 프레임마다 패킷으로 복사된다(그룹마다 맵 하나와 벡터 둘).
    };
} // namespace sw

namespace sw
{
    /** @brief 인스턴스 배열에서 바뀐 구간 `[_start, _start + _count)` 입니다. */
    struct GPUInstanceDirtyRange
    {
        uint32 _start{ 0 };
        uint32 _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUSkinPalette
     * @brief 스킨드 메시 하나의 팔레트 구간입니다. 행은 `GPUSceneSnapshot::_pListSkinPaletteRow` 의 float4 이고, 본 하나가 세 행입니다(행벡터 4x4 의 0, 1, 2번 열, 또는 `_bDualQuaternion` 이면 실수부 · 허수부 · 스케일).
     */
    struct GPUSkinPalette
    {
        shared_ptr<Mesh> _mesh{};                                       ///< 스키닝할 메시(원본 — 칸마다 사본을 두지 않는다). RT 의 모프 풀이 원본 데이터 · 정점 수를 읽는다
        uint32           _skinSlot{ constant::invalid_index::kUint32 }; ///< 이 팔레트의 스킨 칸(인스턴스의 `GPUInstance::_skinSlot` 과 같은 번호)
        /// @brief 변형할 LOD 단계(`MeshComponent::getLODIndex` — 그 칸이 그리는 단계 하나만 스키닝한다, 언리얼 스킨 캐시와 같다). 무효면 모든 단계(멤버마다 단계가 다른 군중 묶음)입니다.
        uint32 _lodIndex{ constant::invalid_index::kUint32 };
        uint32 _firstRow{ 0 };
        uint32 _boneCount{ 0 };
        uint32 _firstMorphWeight{ 0 }; ///< `GPUSceneSnapshot::_pListMorphWeight` 안에서 이 메시의 모프 가중치가 시작하는 위치
        uint32 _morphWeightCount{ 0 }; ///< 모프 가중치 수(0이면 모프 없음)
        /// @brief 본 세 행이 듀얼 쿼터니언(실수부 · 허수부 · 스케일)이다. 아니면 행렬 세 열이다(`SkinningMethod`, mesh_skin.hlsl 가 인스턴스마다 고른다).
        uint8 _bDualQuaternion{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUTransparentParticleSlot
     * @brief 입자 그리기 하나가 투명 큐에서 그려지는 자리입니다. 메시 투명 배치와 방출기를 한 순서로 섞는 단위입니다(언리얼 · 유니티의 프리미티브 단위 정렬).
     * @details 목록은 그리는 순서로 놓이고 `_drawPosition` 이 줄지 않는다. 투명 패스는 그 뷰의 배치 순서에서 `_drawPosition` 번째 배치를 그리기 **전에**
     *          이 입자를 그린다(배치 수와 같으면 모든 배치 뒤). 배치와 방출기는 배치의 첫 인스턴스(그 뷰에서 가장 먼저 그리는 것)의 키와
     *          방출기 위치의 키로 비교한다(`GPUTransparentSorter::placeParticles`) — 배치 안을 가르지 않는다.
     */
    struct GPUTransparentParticleSlot
    {
        uint32 _particleBatchIndex{ 0 }; ///< `GPUSceneSnapshot::_listParticleBatch` 의 번호
        uint32 _drawPosition{ 0 };       ///< 이 뷰의 투명 배치 순서에서 이 입자보다 먼저 그리는 배치 수
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUViewTransparentOrder
     * @brief 추가 뷰 하나의 투명 그리기 순서입니다. 주 뷰의 순서(꼬리 인스턴스 배치)는 그대로 두고, 이 뷰가 다르게 그릴 것만 싣습니다.
     * @details GT 가 그 뷰의 시점과 시선 축으로 꼬리를 다시 정렬해 만든다(`GPUSceneBuilder::computeViewTransparentOrders`). RT 는 순번 테이블을 정렬 디스패치(instance_sort.hlsl)에,
     *          배치 순서를 투명 패스 드로우 순서에 쓴다. 컬링이 없는 백엔드만 순번으로 배치 안을 다시 놓은 인스턴스 슬롯 스트림을 RT 에서 만든다
     *          (`GPUTransparentSorter::buildViewTailSlots`) — 배치 안 정렬은 GPU 정렬과 CPU 정렬 중 한 곳에서만 돈다.
     *          메시 투명 배치가 없고 입자만 있는 뷰는 순번이 nullptr 이고 배치 순서가 비며 입자 자리만 싣는다.
     */
    struct GPUViewTransparentOrder
    {
        shared_ptr<const vector<uint32>>   _pListRank;        ///< 꼬리 인스턴스(꼬리 시작 기준)마다 이 뷰에서 그리는 순번(0이면 가장 먼저, 곧 가장 멀다)
        vector<uint32>                     _listBatchOrder;   ///< 이 뷰에서 투명 배치를 그리는 순서(`GPUSceneSnapshot::getTransparentBatches` 의 번호)
        vector<GPUTransparentParticleSlot> _listParticleSlot; ///< 이 뷰에서 입자 그리기의 순서와 자리(`_drawPosition` 은 `_listBatchOrder` 안의 자리)
        uint64                             _viewID{ 0 };      ///< `RenderViewRequest::_viewID`
        uint32                             _tailBase{ 0 };    ///< 꼬리의 첫 인스턴스 번호(불투명 인스턴스 수)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUParticleBatch
     * @brief 방출기 하나의 입자 그리기입니다. 투명 정렬 후보 하나이며, 투명 패스가 메시 투명 배치 사이의 제자리(`GPUTransparentParticleSlot`)에 빌보드로 그립니다(`VFXParticleRenderer`).
     * @details 입자는 방출기 로컬 좌표이고 `_world` 가 월드로 옮깁니다. 입자의 출처는 방출기의 시뮬레이션 대상(`VFXSimulationTarget`)마다 하나다 —
     *          CPU 방출기는 입자 배열을 **공유합니다**(방출기가 단계마다 새 배열을 내므로 RT 가 읽는 동안 GT 가 고쳐 쓰지 않는다 — `VFXComponent`),
     *          GPU 방출기는 누적 단계 수의 요청을 싣고 RT 가 컴퓨트로 입자를 만들어 간접 인자로 그린다(`VFXGPUSimulation`).
     *          텍스처는 경로로 싣고 RT 가 텍스처 캐시에서 빌립니다(캔버스 경로 이미지와 같다).
     */
    struct GPUParticleBatch
    {
        /** @brief 입자의 출처입니다 — CPU 시뮬레이션의 입자 배열(비지 않는다 — 빈 방출기는 싣지 않는다)이거나 GPU 시뮬레이션의 요청이다. */
        using Source = std::variant<shared_ptr<const vector<GPUParticle>>, VFXGPUEmitterRequest>;

        Source        _source{};              ///< 입자의 출처(위)
        float4x4      _world{};               ///< 방출기 행렬(행벡터 규약)
        hashed_string _texturePath{};         ///< 입자 텍스처(리소스 id)
        float32       _sizeScale{ 1.0f };     ///< 방출기 행렬의 최대 축 배율 — 입자 크기에 곱한다
        uint8         _bAdditive{ SW_FALSE }; ///< 더하기 블렌드(`VFXBlendMode::Additive`)면 SW_TRUE
        uint8         _bRibbon{ SW_FALSE };   ///< 띠(`VFXRenderMode::Ribbon`)면 SW_TRUE — 입자는 태어난 순서이고 이웃끼리 잇는다(입자 둘 이상)

        /** @brief CPU 시뮬레이션의 입자입니다. GPU 시뮬레이션이면 nullptr 입니다. */
        const vector<GPUParticle>* findCPUParticles() const
        {
            const shared_ptr<const vector<GPUParticle>>* pShared = std::get_if<shared_ptr<const vector<GPUParticle>>>( &_source );
            return pShared != nullptr ? pShared->get() : nullptr;
        }
        /** @brief GPU 시뮬레이션의 요청입니다. CPU 시뮬레이션이면 nullptr 입니다. */
        const VFXGPUEmitterRequest* findGPURequest() const { return std::get_if<VFXGPUEmitterRequest>( &_source ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUVFXMeshEmitter
     * @brief GPU 로 시뮬레이션하는 메시 방출기 하나입니다. 입자 그리기가 아니라 메시 인스턴스 배치(`MeshInstanceWriter::GPU`)의 인스턴스 칸에 쓴다.
     * @details GT 는 배치 항목마다 그 프레임의 인스턴스 칸(`getInstances()` 의 번호, 불투명 구간)을 찾아 싣는다 — 칸은 배치 구성이 바뀔 때마다 옮겨진다.
     *          RT 는 시뮬레이션 뒤 · 인스턴스 애니메이션과 컬링 앞에 그 칸들의 행렬 · 바운드 · 색을 쓰고(vfx_mesh_instance.hlsl), 컬링이 간접 인자를 만든다.
     */
    struct GPUVFXMeshEmitter
    {
        VFXGPUEmitterRequest             _request;            ///< 시뮬레이션 요청(`GPUParticleBatch` 의 GPU 출처와 같은 뜻)
        shared_ptr<const vector<uint32>> _pListInstanceSlot;  ///< 풀 자리마다 인스턴스 칸(`constant::invalid_index::kUint32` 면 이번 프레임에 실리지 않았다)
        float4x4                         _world{};            ///< 방출기 행렬(행벡터 규약)
        float32                          _meshRadius{ 0.0f }; ///< 메시 원점에서 가장 먼 바운드 모서리(크기 1)
        float32                          _sizeScale{ 1.0f };  ///< 방출기 행렬의 최대 축 배율
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUSceneSnapshot
     * @brief 게임 스레드가 만들어 렌더 패킷에 실어 렌더 스레드로 **옮기는 모든 데이터**입니다.
     *
     * @details 소유 규칙이 이 타입에 적혀 있다.
     *          - 머티리얼 · 인스턴스는 싣지 않는다. 렌더 쪽 객체와 불변 사본(`MaterialRenderBinding`)만 싣는다(게임 스레드 객체를 렌더 스레드가 읽지 않는다).
     *          - 렌더 스레드가 역참조하는 CPU 객체(메시, 머티리얼 프록시 · 사본)는 **shared_ptr 로 소유권을 함께 싣는다.**
     *            패킷이 살아 있는 동안(렌더 큐 깊이만큼) 게임 스레드가 놓아도 살아 있다. 생포인터는 정렬 키 같은
     *            **정체성**에만 쓰고 스레드를 넘어 역참조하지 않는다.
     *          - RT 가 upload() 에서 만드는 `GPUScene` 의 값(인스턴스와 간접 인자 버퍼, 간접 개수, 컬 뷰)은 여기 없다.
     *            그래서 옮겨질 수 없고, "한쪽만 만드는 값을 스냅샷이 매 프레임 덮어쓴다" 는 버그가 타입으로 막힌다.
     *            배치의 GPU 값(정점 버퍼 · 머티리얼 CB 등)도 여기 없다 — `GPUScene` 이 배치 번호로 따로 든다(`GPUMeshBatchBinding`).
     *          - 모듈(게임 DLL)이 만든 객체를 엔진이 마지막까지 가질 수 있으므로, 싣는 객체는 Engine 의
     *            create() 팩토리로 만든다(제어 블록이 Engine.dll 에 있다).
     */
    struct GPUSceneSnapshot
    {
        /**
         * @brief 인스턴스 배열입니다. **값이 아니라 공유합니다.** RT 는 읽기만 하므로 프레임마다 복사할 이유가 없습니다.
         *
         * @details 값으로 두면 `exportCPUSnapshot` 이 매 프레임 통째로 복사한다. 아무것도 움직이지 않는 정적 씬에서도
         *          인스턴스당 55ns(엔티티 8000개면 441µs)가 들고, 그것이 정적 씬에서 **게임 스레드의 유일한 실제 작업**이 된다.
         *
         *          배치 목록도 공유한다(아래 비공개 `_pListBatch`, 불투명 · 투명은 그 구간 뷰). RT 가 쓰는 GPU 값은 `GPUScene` 이 배치 번호로 따로 든다
         *          (`GPUMeshBatchBinding`) — 예전에는 그 값을 배치에 덧써서 공유하지 못하고 패킷마다 통째로 베꼈다(배치마다 shared_ptr 원자 연산).
         *          머티리얼 그룹 · 원소 목록도 같은 규칙으로 공유한다(`getMaterialGroups` · `editMaterialGroups`). 렌더 스레드는 그룹을 읽기만 한다 —
         *          덧쓰는 칸이 없어 배치처럼 뗄 것이 없다(원소의 상수 버퍼는 `MaterialRenderProxy` 가, 그룹 버퍼는 `GPUScene` 이 따로 든다).
         */
        shared_ptr<const vector<GPUInstance>> _pListInstance;
        /**
         * @brief 퍼뮤테이션 목록입니다. 배치의 `_shaderPermutation` 이 가리킵니다. **불변 목록을 공유합니다.**
         * @details 문자열 경로와 define 목록이 든 값이라 프레임마다 복사하면 그 문자열들이 모두 힙 할당이다(프레임당 약 30회).
         *          빌더는 새 퍼뮤테이션이 나타날 때만 목록을 새로 만들어 바꿔 끼운다(copy-on-write). RT 는 읽기만 한다.
         */
        shared_ptr<const vector<GPUShaderPermutation>> _pListShaderPermutation;
        /// @brief 스킨드 메시마다의 팔레트 구간입니다. 작아서 복사합니다.
        vector<GPUSkinPalette> _listSkinPalette;
        /**
         * @brief 이번 프레임의 스킨 팔레트 행 전부입니다(본 하나가 float4 세 개). **공유합니다.** RT 는 읽기만 하고, GT 는 프레임마다 새 배열을 만듭니다.
         * @details 팔레트는 애니메이션 시스템이 틱 뒤에 만들고 빌더가 여기로 옮긴다. RT 가 모프 풀의 스킨 구간 순서로 다시 올린다(`GPUMeshMorphPool::uploadSkinPalettes`).
         */
        shared_ptr<const vector<float4>> _pListSkinPaletteRow;
        /**
         * @brief 이번 프레임의 모프 타깃 가중치 전부입니다(스킨드 메시마다 `GPUSkinPalette::_firstMorphWeight` 부터). **공유합니다.** 팔레트 행과 같은 규칙입니다.
         * @details 유닛(`SkeletalMeshComponent::getMorphWeights`)이 내고, RT 가 팔레트 버퍼 뒤에 실어 스키닝 컴퓨트가 레스트에 차이를 더한다.
         */
        shared_ptr<const vector<float32>> _pListMorphWeight;
        /// @brief GPU 회전을 요청한 인스턴스 수입니다. 0이면 애니메이션 디스패치를 건너뜁니다.
        uint32 _spinInstanceCount{ 0 };
        /**
         * @brief 정점 애니메이션(VAT) 시계(초)입니다. 게임 스레드의 군중 시계(`AnimationCrowd::getClock`)를 그대로 옮긴 값으로, 인스턴스 시간 오프셋과 더하면 CPU 가
         *        본 클립 시각과 같습니다. 패스마다 PassCB `g_SwVertexAnimationTime` 으로 전달됩니다.
         */
        float32 _vertexAnimationTime{ 0.0f };
        /**
         * @brief 정적 그림자 세대입니다. 정적 메시가 움직이거나 바뀌거나 더해지거나 빠질 때 오른다(`GPUSceneBuilder::updateStaticShadowGeneration`).
         * @details 그림자 캐시(`ShadowCachePolicy`)가 이 값이 바뀌면 캐시 층을 다시 그린다 — 언리얼이 정적 프리미티브의 등록 · 이동으로 캐시한 그림자 맵을 버리는 것과 같은 자리다.
         */
        uint64 _staticShadowGeneration{ 0 };
        /**
         * @brief 인스턴스 **번호의 배치**가 바뀔 때마다 오르는 세대입니다(배치를 처음부터 다시 만든 빌드). 제자리 갱신(움직임 · 색)은 올리지 않는다.
         * @details 인스턴스 번호에 묶어 둔 GPU 상태(LOD 전환 상태 `GPUCullViewResources::_lodState`)가 이 값이 달라지면 낡은 것이라 비운다.
         */
        uint32 _instanceLayoutGeneration{ 0 };
        /**
         * @brief LOD 단계 전환의 크로스페이드 시간(초)입니다. 0 이면 전환 없이 바로 바꾼다(`GPUSceneBuilder::setGPUInstanceLODCrossfadeSeconds`).
         * @details 빌더가 정하고 스냅샷이 싣는다 — 같은 값이 후보의 디더 변형 여부(GT)와 컬링의 전환 진행량(RT)을 가른다. 어긋나면 디더 변형이 없는 배치가 상보 값을 받는다.
         */
        float32 _lodCrossfadeSeconds{ 0.0f };
        /// @brief 마지막 populateFromScene 이 내용을 바꿨는지 나타냅니다. RT 는 0이면 인스턴스 재업로드를 생략합니다.
        uint8 _bCPUDirty{ SW_TRUE };
        /**
         * @brief 이 스냅샷의 인스턴스에 있는 강조 방식의 비트입니다(`1 << InstanceHighlightStyle`). 빌더가 인스턴스를 다시 채울 때마다 구한다.
         * @details 강조 패스는 불투명 배치를 모두 다시 그리고 정점 셰이더가 강조 아닌 인스턴스를 버린다 — 강조된 것이 없어도 씬의 모든 정점(스키닝 포함)을 두 번 더 돌았다
         *          (기사 200 명 부하에서 2.6 ms). 그 방식의 인스턴스가 없으면 패스가 그리지 않는다(`FramePassExecutor::executeHighlightPass`).
         */
        uint8 _highlightStyleMask{ 0 };
        /**
         * @brief 1이면 이 스냅샷의 메시(동적 메시 밖)는 게임 스레드가 넘기기 전에 만들었다(`GPUUploadQueue::shouldCreateMeshes`) — 렌더 스레드는 다시 만들지 않는다.
         * @details 메시 하나를 만드는 쪽은 하나다(언리얼 `BeginInitResource` 가 한 번만 들어가는 것과 같은 자리). 0이면(GL · 큐를 끈 구성 · 큐 없는 화면 밖 캡처) 렌더 스레드가 만든다.
         *          예전에는 두 쪽이 모두 `Mesh::initRHI` 를 불렀고, 게임 스레드 생성이 실패한 메시는 두 스레드가 함께 다시 시도할 수 있었다.
         */
        uint8 _bMeshesCreatedBeforeHandoff{ SW_FALSE };
        /**
         * @brief 1이면 인스턴스 배열 **전체**가 바뀌었고, 0이면 `_listDirtyInstanceRange` 만 바뀌었습니다.
         * @details 전체 재구축은 1이고, 물체만 움직인 프레임은 0이다. 버퍼를 새로 만든 프레임도 받는 쪽이
         *          전체로 취급해야 한다. 새 버퍼에는 아직 아무것도 없기 때문이다.
         */
        uint8 _bAllInstancesDirty{ SW_TRUE };
        /**
         * @brief 바뀐 인스턴스 구간들입니다(`_bAllInstancesDirty` 가 0일 때만 의미가 있습니다).
         * @details 하나만 움직여도 인스턴스 버퍼 **전체**를 다시 올리면 8000개 중 10개만 움직여도 800개를 움직일 때와
         *          같은 100µs 가 든다. 그래서 바뀐 구간만 올린다. 구간이 너무 잘게 흩어지면
         *          작은 업로드가 오히려 비싸므로, 빌더가 개수 상한을 넘기면 전체로 돌린다.
         */
        vector<GPUInstanceDirtyRange> _listDirtyInstanceRange;
        /// @brief 이번 프레임에 그리는 추가 뷰마다의 투명 순서입니다. 뷰가 투명을 보지 않거나 꼬리가 없으면 비어 있으며, 그 뷰는 주 순서로 그립니다.
        vector<GPUViewTransparentOrder> _listViewTransparentOrder;
        /**
         * @brief 방출기마다의 입자 그리기입니다. 주 시점에서 먼 것부터(투명 정렬 규칙 `GPUTransparentSorter::isDrawnBefore`) 놓입니다. 작아서 복사합니다.
         * @details 메시 배치와 달리 씬이 그대로인 프레임에도 매번 다시 모은다(입자는 단계마다 움직인다 — `GPUSceneBuilder::populateParticleBatches`).
         */
        vector<GPUParticleBatch> _listParticleBatch;
        /**
         * @brief 주 시점에서 입자 그리기의 자리입니다. `_listParticleBatch` 와 길이 · 순서가 같고, `_drawPosition` 은 주 투명 배치 순서 안의 자리입니다.
         * @details 메시 배치가 그대로인 프레임에도 카메라와 방출기가 움직이므로 입자 그리기와 함께 매번 다시 정한다(`GPUSceneBuilder::populateParticleBatches`).
         */
        vector<GPUTransparentParticleSlot> _listParticleSlot;
        /** @brief GPU 로 시뮬레이션하는 메시 방출기들입니다. 입자 그리기와 같이 매번 다시 모은다(인스턴스 칸은 배치 구성을 따른다). */
        vector<GPUVFXMeshEmitter> _listVFXMeshEmitter;

        /** @brief 셰이더 타입별 머티리얼 원소 목록입니다(배치의 `_materialGroup` 이 가리킨다). 아직 없으면 빈 목록입니다. */
        const vector<GPUMaterialGroup>& getMaterialGroups() const
        {
            static const vector<GPUMaterialGroup> s_listEmpty{};
            return ( _pListMaterialGroup != nullptr ) ? *_pListMaterialGroup : s_listEmpty;
        }
        /**
         * @brief 고칠 머티리얼 그룹 목록입니다(게임 스레드의 `GPUMaterialRegistry` 만 부른다). 공유 중이면 베낀 새 목록을 둔다(copy-on-write).
         * @details 고치는 프레임(새 원소 · 회수 · 사본이 바뀐 원소)에만 부르게 해야 공유가 산다 — 바뀌지 않은 프레임은 목록 포인터 하나만 패킷으로 간다.
         *          use_count 가 1 이면 다른 참조가 없다(새 참조는 게임 스레드의 내보내기에서만 생긴다).
         */
        vector<GPUMaterialGroup>& editMaterialGroups()
        {
            if ( _pListMaterialGroup == nullptr )
                _pListMaterialGroup = make_shared<vector<GPUMaterialGroup>>();
            else if ( _pListMaterialGroup.use_count() > 1 )
                _pListMaterialGroup = make_shared<vector<GPUMaterialGroup>>( *_pListMaterialGroup );
            return *_pListMaterialGroup;
        }

        /** @brief 입자 그리기 · 그 자리 · GPU 메시 방출기를 함께 비웁니다(앞의 둘은 길이 · 순서가 같아야 한다). */
        void clearParticles()
        {
            _listParticleBatch.clear();
            _listParticleSlot.clear();
            _listVFXMeshEmitter.clear();
        }

        /** @brief 인스턴스 배열을 반환합니다. 아직 발행 전이면 빈 배열입니다. */
        const vector<GPUInstance>& getInstances() const
        {
            static const vector<GPUInstance> s_listEmpty{};
            return ( _pListInstance != nullptr ) ? *_pListInstance : s_listEmpty;
        }

        /** @brief 모든 배치(불투명 다음 투명)를 반환합니다. 간접 슬롯 순서와 같습니다. */
        const vector<GPUMeshBatch>& getAllBatches() const
        {
            static const vector<GPUMeshBatch> s_listEmpty{};
            return ( _pListBatch != nullptr ) ? *_pListBatch : s_listEmpty;
        }
        /**
         * @brief 모든 배치의 원소를 고쳐 쓰는 뷰입니다(게임 스레드 빌더의 제자리 메시 교체). 배치를 더하거나 빼지는 못합니다.
         * @details 공유 중인 목록(패킷 · 렌더 스레드가 쥐었다)이면 먼저 베낀다(copy-on-write) — 고치는 프레임에만 새 목록이 발행된다.
         */
        vector_reference<GPUMeshBatch> getMutableBatches() { return vector_reference<GPUMeshBatch>( editBatches() ); }
        /** @brief 불투명 배치 구간 `[0, 불투명 수)` 입니다. 목록을 바꾸면 무효입니다. */
        vector_reference<const GPUMeshBatch> getOpaqueBatches() const { return vector_reference<const GPUMeshBatch>( getAllBatches() ).first( _opaqueBatchCount ); }
        /** @brief 투명 배치 구간 `[불투명 수, 끝)` 입니다. 번호는 이 구간 안의 번호입니다(`GPUViewTransparentOrder::_listBatchOrder`). */
        vector_reference<const GPUMeshBatch> getTransparentBatches() const { return vector_reference<const GPUMeshBatch>( getAllBatches() ).subspan( _opaqueBatchCount ); }
        /** @brief 불투명 배치 수입니다. 투명 배치의 전역 번호는 이 값부터 시작합니다. */
        uint32 getOpaqueBatchCount() const { return _opaqueBatchCount; }
        /**
         * @brief 불투명 인스턴스 수입니다. 투명 인스턴스(투명 꼬리)는 이 번호부터 시작합니다.
         * @details 따로 기록하지 않고 마지막 불투명 배치의 인스턴스 구간 끝에서 읽는다. 배치의 인스턴스 구간은 0 부터 빈틈없이 이어지므로
         *          (`appendBatch` 가 지킨다) 이 값이 곧 불투명 인스턴스 수다.
         */
        uint32 getOpaqueInstanceCount() const { return computeInstanceEnd( _opaqueBatchCount ); }

        /**
         * @brief 배치를 끝에 붙입니다. 불투명 배치는 투명 배치보다 먼저 붙여야 하고, 인스턴스 구간과 가시 구간은 각각 앞 배치의 끝에서 시작해야 합니다.
         * @details 투명 배치 뒤에 불투명 배치를 붙이면 구간 경계가 깨지므로 멈춘다(간접 슬롯 순서가 불투명 다음 투명이다).
         *          인스턴스 구간에 빈틈이 있으면 `getOpaqueInstanceCount` 가 틀리고, 가시 구간에 빈틈이 있으면 가시 목록 크기(`getVisibleSlotCount`)가 틀리므로 그것도 멈춘다.
         *          두 구간이 한 단언이 아닌 이유는 둘이 다른 축이기 때문이다(LOD 그룹의 낮은 단계 배치는 인스턴스 구간이 비고 가시 구간만 있다).
         */
        void appendBatch( GPUMeshBatch&& batch )
        {
            const uint32 batchCount = static_cast<uint32>( getAllBatches().size() );
            SW_ASSERT( batch._instanceBase == computeInstanceEnd( batchCount ) );
            SW_ASSERT( batch._visibleBase == computeVisibleEnd( batchCount ) );
            if ( batch._blendMode != BlendMode::Transparent )
            {
                SW_ASSERT( _opaqueBatchCount == batchCount );
                ++_opaqueBatchCount;
            }
            editBatches().push_back( std::move( batch ) );
        }
        /**
         * @brief 가시 목록의 칸 수입니다(모든 배치의 가시 구간 합). 컬링의 가시 목록 버퍼와 인스턴스 슬롯 스트림이 이 길이다.
         * @details LOD 그룹이 없으면 인스턴스 수와 같고, 그룹이 있으면 낮은 단계 배치의 구간만큼 더 길다.
         */
        uint32 getVisibleSlotCount() const { return computeVisibleEnd( static_cast<uint32>( getAllBatches().size() ) ); }
        /** @brief 다음에 붙일 배치의 가시 구간 시작입니다(= 지금까지의 가시 칸 수). */
        uint32 getNextVisibleBase() const { return getVisibleSlotCount(); }
        /** @brief LOD 그룹(`GPUMeshBatch::_lodLevelCount`)이 하나라도 있으면 true 입니다. 불투명 배치만 훑는다. */
        bool hasLODGroups() const
        {
            for ( const GPUMeshBatch& batch : getOpaqueBatches() )
            {
                if ( batch._lodLevelCount > 1 )
                    return true;
            }
            return false;
        }
        /** @brief 배치를 모두 지웁니다. */
        void clearBatches()
        {
            // 공유 중이면 베끼지 않고 새 빈 목록을 둔다(지울 내용을 베낄 까닭이 없다). 혼자 쥐었으면 용량을 남긴다.
            if ( _pListBatch != nullptr && _pListBatch.use_count() == 1 )
                _pListBatch->clear();
            else
                _pListBatch = make_shared<vector<GPUMeshBatch>>();
            _opaqueBatchCount = 0;
        }
        /** @brief 투명 배치를 지우고 불투명 배치만 남깁니다(투명 꼬리를 다시 붙이기 전). 혼자 쥔 목록이면 용량은 남습니다. */
        void truncateToOpaqueBatches() { editBatches().resize( _opaqueBatchCount ); }

    private:
        /**
         * @brief 고칠 배치 목록입니다. 아무도 같이 쥐지 않았으면 그대로, 쥐었으면(패킷 · 렌더 스레드) 베낀 새 목록을 둔다(copy-on-write).
         * @details use_count 가 1 이면 다른 참조가 없다 — 새 참조는 이 스냅샷을 베끼는 쪽(게임 스레드의 `exportCPUSnapshot`)에서만 생긴다.
         *          렌더 스레드가 마지막 참조를 놓는 중이면 2 로 보여 한 번 더 베낄 뿐이다(틀리지 않는다).
         */
        vector<GPUMeshBatch>& editBatches()
        {
            if ( _pListBatch == nullptr )
                _pListBatch = make_shared<vector<GPUMeshBatch>>();
            else if ( _pListBatch.use_count() > 1 )
                _pListBatch = make_shared<vector<GPUMeshBatch>>( *_pListBatch );
            return *_pListBatch;
        }
        /** @brief 앞쪽 `batchCount` 개 배치가 차지한 가시 구간의 끝입니다(배치가 없으면 0). */
        uint32 computeVisibleEnd( uint32 batchCount ) const
        {
            if ( batchCount == 0 )
                return 0;
            const GPUMeshBatch& lastBatch = getAllBatches()[batchCount - 1];
            return lastBatch._visibleBase + lastBatch._visibleCapacity;
        }
        /** @brief 앞쪽 `batchCount` 개 배치가 차지한 인스턴스 구간의 끝입니다(배치가 없으면 0). */
        uint32 computeInstanceEnd( uint32 batchCount ) const
        {
            if ( batchCount == 0 )
                return 0;
            const GPUMeshBatch& lastBatch = getAllBatches()[batchCount - 1];
            return lastBatch._instanceBase + lastBatch._instanceCount;
        }

    private:
        /**
         * @brief 배치 목록 **한 벌**입니다(공유 값). 불투명 배치 `[0, _opaqueBatchCount)` 다음에 투명 배치가 오며, 간접 슬롯과 일치합니다.
         * @details 불투명 · 투명 목록을 따로 두지 않고 구간 뷰만 내준다. 고치는 길은 위 함수뿐이라(모두 `editBatches` 를 거친다) 경계가 깨지지 않고,
         *          공유 중인 목록을 제자리에서 고치는 일이 없다. 바뀌지 않은 프레임의 내보내기(`GPUSceneBuilder::exportCPUSnapshot`)는 포인터 하나다.
         */
        shared_ptr<vector<GPUMeshBatch>> _pListBatch;
        /** @brief 셰이더 타입별 머티리얼 원소 목록입니다(공유 값, `getMaterialGroups` · `editMaterialGroups`). */
        shared_ptr<vector<GPUMaterialGroup>> _pListMaterialGroup;
        uint32                               _opaqueBatchCount{ 0 }; ///< 앞쪽 불투명 배치 수. 불투명 · 투명의 경계는 이 값 하나다(인스턴스 · 원소의 경계는 여기서 끌어낸다).
    };
} // namespace sw
