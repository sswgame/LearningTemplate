/**
 * @file GPUSceneSnapshot.h
 * @brief 게임 스레드가 만들고 렌더 스레드가 받는 씬 스냅샷입니다. 두 스레드가 공유하는 **유일한** 타입입니다.
 * @details 만드는 쪽은 `GPUSceneBuilder`(GT)이고, 받아서 GPU 에 올리는 쪽은 `GPUScene`(RT)이다. 두 클래스는 서로를 모르고
 *          이 구조체만 안다. 그래서 "GT 가 RT 의 값을 덮어쓴다", "RT 가 씬을 읽는다" 같은 실수는 컴파일되지 않는다.
 *          소유 규칙(shared_ptr 로 소유권을 함께 싣는다)은 `GPUSceneSnapshot` 주석과 Scripts/lint/gate/CheckRenderOwnership.py 를 참고한다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Vfx/VfxParticle.h"

#include <variant>

namespace sw
{
    class Material;
    class MaterialInstance;
    class Mesh;

    /// @brief 배치에 머티리얼 데이터 그룹이 없음을 나타냅니다(GPUMeshBatch::_materialGroup).
    inline constexpr uint32 kInvalidMaterialGroup = invalid_index::kUint32;
    /// @brief 배치에 셰이더 퍼뮤테이션이 없음을 나타냅니다. 이 경우 패스가 자기 PSO 로 그립니다(GPUMeshBatch::_shaderPermutation).
    inline constexpr uint32 kInvalidShaderPermutation = invalid_index::kUint32;

    /**
     * @brief GPU 인스턴스 하나를 나타냅니다. 월드 행렬, 바운드, 배치 인덱스, 머티리얼 원소 인덱스, 블렌드, 회전 시드, 스프라이트 프레임과 색을 가집니다.
     * @details HLSL 쪽은 `Resource/engine/shaders/instancedata.hlsli` 의 `InstanceData` 하나이며 그래픽스와 컴퓨트가 함께 쓴다.
     *          필드를 고치면 그 파일과 ShaderBindingValidatorTest.InstanceElementLayoutMatchesCPUStruct 의 테이블을 함께 고친다(쿠킹된 바이너리로 대조한다).
     */
    struct GPUInstance
    {
        float4x4 _world{};
        float3   _boundsCenter{};
        float32  _boundsRadius{ 1.0f };
        uint32   _meshBatchIndex{ 0 };
        uint32   _materialIndex{ 0 };
        uint32   _blendMode{ 0 }; ///< RHIBlendMode
        /**
         * @brief GPU 인스턴스 애니메이션 시드입니다. 0 이면 애니메이션이 없습니다.
         * @details instanceanimation.hlsl 이 이 값을 해시해 **인스턴스마다 다른 각속도**를 만든다. 회전은 CPU 가 매 프레임
         *          계산해 올리지 않고 GPU 가 계산한다.
         */
        uint32 _spinSeed{ 0 };
        /**
         * @brief 스프라이트 프레임(UV 사각형)과 색입니다. 지금 이 값을 읽는 셰이더는 sprite2d.hlsl 하나입니다.
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
        /** @brief 강조(상호작용 외곽선 · 감각 모드)입니다(`GPUInstanceHighlight::pack`, 0 이면 없음). 셰이더의 `highlight` 이고 `highlightoutline.hlsl` 이 읽습니다. */
        uint32 _highlight{ 0 };
        /**
         * @brief 등장 비율(0 ~ 1)입니다. 1 이면 다 보인다. 1 보다 작은 인스턴스는 디더 변형(`kAppearanceDitherDefine`)으로 그려져 그 비율만큼의 픽셀만 남는다.
         * @details 로드가 끝난 메시 · 텔레포트 · 사라지기가 이 값을 올리고 내린다(`MeshComponent::beginAppearance`). 디더 임계값은 화면 픽셀의 4x4 Bayer 행렬이라
         *          깊이 프리패스 · 그림자 · 기본 패스가 같은 픽셀을 버린다(binding.hlsli `Appearance::clipDither`). 셰이더의 `appearance` 에 해당한다.
         */
        float32 _appearance{ 1.0f };
        uint32  _arrReserved[1]{}; ///< 원소를 128바이트(16의 배수)로 맞춘다. 셰이더의 `reserved1` 에 해당한다.
    };
} // namespace sw

namespace sw
{
    static_assert( sizeof( GPUInstance ) == 128, "GPUInstance must match InstanceData (instancedata.hlsli) byte for byte" );

    /// @brief 같은 메시, 머티리얼, 퍼뮤테이션으로 그리는 인스턴스 배치입니다.
    struct GPUMeshBatch
    {
        shared_ptr<Mesh> _mesh; ///< **소유권을 함께 싣는다.** RT 가 upload() 에서 이 메시를 역참조한다.
        RHIBufferHandle  _vertexBuffer{ 0 };
        uint32           _vertexCount{ 0 };
        /**
         * @brief 정점 풀 안에서 이 메시의 시작 위치(정점 단위)입니다. 간접 인자의 startVertex 가 됩니다. 풀 밖의 메시는 0 이며 자기 정점 버퍼를 씁니다.
         * @details RT 가 upload() 에서 채운다. 같은 풀 버퍼를 쓰는 배치들은 정점 버퍼를 다시 바인딩하지 않고 멀티 드로우로 묶인다.
         */
        uint32 _firstVertex{ 0 };
        /**
         * @brief 이 배치가 그리는 메시 구간의 첫 정점(메시 안 번호)입니다. 메시 전체면 0 입니다.
         * @details RT 가 정점 풀 시작(`_firstVertex`)과 모프 풀 시작(`_morphVertexBase`)에 더하고, VAT 는 배치 표의 `_sectionVertexOffset` 으로 메시 안 번호를 되찾는다.
         */
        uint32 _sectionFirstVertex{ 0 };
        uint32 _instanceBase{ 0 };
        uint32 _instanceCount{ 0 };
        uint32 _materialIndex{ 0 };
        /**
         * @brief 모프 풀에서 이 배치 메시의 시작 오프셋(정점 단위)입니다. 0xFFFFFFFF 이면 모프를 하지 않습니다.
         * @details RT 가 `GPUScene::assignMorphBases` 로 채운다. GT 는 GPU 풀을 모른다(스냅샷 소유 규칙). upload 가 이 값을
         *          배치 테이블(`GPUBatchInfo`, g_SwBatches t9)에 옮겨 적고, 정점 셰이더가 자기 배치 번호로 읽는다(binding.hlsli MorphPool::computeElement).
         */
        uint32 _morphVertexBase{ 0xFFFFFFFFu };
        /**
         * @brief 정점 애니메이션(VAT) 테이블에서 이 배치 메시의 첫 원소입니다. 0xFFFFFFFF 이면 VAT 가 없습니다.
         * @details RT 가 `GPUScene::assignVertexAnimationBases` 로 채운다. 모프 시작과 같은 경로이며 배치 테이블 `GPUBatchInfo` 에 실린다.
         */
        uint32 _vertexAnimationBase{ 0xFFFFFFFFu };
        /**
         * @brief 변형(모프 · 스킨 · VAT)이 필요한 메시인데 풀이 받지 못했습니다(상한 초과 · 버퍼 생성 실패). 이 배치는 그리지 않습니다.
         * @details RT 가 `GPUScene::assignMorphBases` · `assignVertexAnimationBases` 로 채우고 `FrameRenderer::drawsBatchInPass` 가 거른다.
         *          바인드 · 레스트 포즈로 대신 그리지 않는다 — 풀이 이유를 오류로 남겼다. 백엔드가 GPU 변형을 못 하는 경우(능력 질의)는 풀을 쓰지 않는 것이라 여기 해당하지 않는다.
         */
        uint8        _bDeformRefused{ SW_FALSE };
        RHIBlendMode _blendMode = RHIBlendMode::Opaque;
        /**
         * @brief 인스턴스의 월드 행렬식이 음수(거울 변환)인 배치입니다. 이 배치는 컬 모드를 뒤집은 PSO 로 그립니다(언리얼 `bReverseCulling`).
         * @details 거울 변환은 삼각형 감김 방향을 뒤집는다. 컬 모드를 그대로 두면 카메라 쪽 면이 잘리고 안쪽 면이 보인다. 컬 모드는 PSO 의 상태라
         *          배치 키에 들어간다. 한 배치의 인스턴스는 모두 같은 부호다(`FrameRenderer::psoForBatch`).
         */
        uint8              _bReverseCulling{ SW_FALSE };
        RHIDescriptorIndex _materialCb = kInvalidDescriptorIndex;
        /**
         * @brief 배치가 쓰는 부모 머티리얼입니다. 셰이더 타입(머티리얼 데이터 그룹)과 텍스처 슬롯의 소유자입니다.
         * @details **소유권을 함께 싣는다.** 렌더 스레드는 이 배치가 든 패킷을 다 쓸 때까지 이 머티리얼을 역참조하므로,
         *          게임 스레드가 먼저 놓아도 살아 있어야 한다. 소유권이 패킷을 따라가므로 수명을 따로 지키는 큐가 필요 없다.
         */
        shared_ptr<Material> _material;
        /** @brief 머티리얼 데이터 그룹(셰이더 타입) 인덱스입니다(`_listMaterialGroup`). 없으면 kInvalidMaterialGroup 입니다. */
        uint32 _materialGroup{ 0xFFFFFFFFu };
        /**
         * @brief 이 배치를 그릴 셰이더 퍼뮤테이션입니다. `_pListShaderPermutation` 의 인덱스이며 `GPUScene::findShaderPermutation` 으로 찾습니다.
         * @details 없으면 kInvalidShaderPermutation 이고, 그때는 패스가 자기 PSO 로 그린다. 배치 키에 이 퍼뮤테이션
         *          해시가 들어가므로, 한 배치의 인스턴스는 반드시 모두 같은 셰이더다.
         */
        uint32 _shaderPermutation{ 0xFFFFFFFFu };
        /**
         * @brief 그룹의 GPU 구조 버퍼(g_SwMaterials, t5)와 SRV 인덱스입니다. upload 가 채웁니다.
         * @details 인스턴스의 _materialIndex 가 이 버퍼의 원소를 고른다(언리얼 GPUScene 방식). 드로우마다 CB 를 바꿔
         *          끼우지 않고, 배치마다 이 버퍼를 리플렉션 슬롯에 한 번 바인딩한다.
         */
        RHIBufferHandle    _materialBuffer{ 0 };
        RHIDescriptorIndex _materialSrv = kInvalidDescriptorIndex;
        /** @brief 그룹 버퍼의 원소 수입니다. 드로우 루트 상수 g_SwMaterialCount 로 넘겨 셰이더가 인덱스를 클램프하게 합니다. */
        uint32 _materialCount{ 0 };
        /**
         * @brief 머티리얼 텍스처의 백엔드 SRV 인덱스(서수 순서)입니다. 네이티브 bindless 가 아닌 백엔드에서만 씁니다.
         * @details DX11 과 GL 은 셰이더가 전역 인덱스를 해석하지 못해서 엔진이 t16..t31 에 직접 바인딩해야 한다.
         *          렌더 스레드는 씬을 볼 수 없으므로(Material* 를 따라갈 수 없다) 값으로 실어 보낸다.
         * @note 초기값은 칸마다 적는다 — 빠진 칸은 0(유효한 SRV 번호)이 되어 다른 텍스처를 건다. 칸 수가 바뀌면 아래 static_assert 가 먼저 멈춘다.
         */
        RHIDescriptorIndex _arrMaterialTexSrv[shaderslot::kMaterialTextureCount] = {
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        static_assert( shaderslot::kMaterialTextureCount == 16, "_arrMaterialTexSrv 의 초기값을 칸 수만큼 적는다" );
        /** @brief 배치의 머티리얼 인스턴스입니다. RT 가 upload() 에서 updateRHI 합니다. 이 shared_ptr 이 수명을 잡고 있어 패킷이 살아 있는 동안 유지됩니다. */
        shared_ptr<MaterialInstance> _materialInstance;

        /**
         * @brief 두 배치가 머티리얼 쪽에서 멀티 드로우 하나로 묶일 수 있는지 판단합니다. 버퍼, SRV, 원소 수, 텍스처가 같아야 하고, 머티리얼 CB 는
         *        셰이더가 그 슬롯을 바인딩할 때만(@p bShaderBindsMaterialCb) 비교합니다.
         * @details GPUScene 경로의 셰이더는 머티리얼을 구조 버퍼(`g_SwMaterials`, t5)에서 인스턴스의 `_materialIndex` 로 읽으므로 CB 슬롯이 없다.
         *          그런 셰이더에서 `_materialCb` 를 키에 넣으면 깊이순으로 섞인 투명 배치들이 이미지에 영향이 없는 값 때문에 하나씩 따로 그려진다
         *          (큐브 8000개와 도형 8종 씬에서 배치 1795개가 드로우 1206회였고, 이 값을 빼면 3회이면서 화면은 같다).
         */
        static bool canShareMaterialBinding( const GPUMeshBatch& head, const GPUMeshBatch& other, bool bShaderBindsMaterialCb )
        {
            const bool bDifferentMaterialBinding = other._materialBuffer != head._materialBuffer || other._materialSrv != head._materialSrv || other._materialCount != head._materialCount;
            if ( bDifferentMaterialBinding )
                return false;
            if ( bShaderBindsMaterialCb && other._materialCb != head._materialCb )
                return false;
            for ( uint32 texIndex = 0; texIndex < shaderslot::kMaterialTextureCount; ++texIndex )
            {
                if ( other._arrMaterialTexSrv[texIndex] != head._arrMaterialTexSrv[texIndex] )
                    return false;
            }
            return true;
        }
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
     * @brief 머티리얼 데이터 원소 하나입니다. (머티리얼, 인스턴스) 쌍의 **소유권**을 가집니다.
     * @details 키(`GPUMaterialElementKey`)는 정체성이라 생포인터이고, 원소는 렌더 스레드가 `getBuffer()` 로 읽으므로
     *          소유권을 가진다. 인스턴스가 없으면 머티리얼 자신이 원소다.
     */
    struct GPUMaterialElement
    {
        shared_ptr<Material>         _material;
        shared_ptr<MaterialInstance> _instance;
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
        /// @brief (경로, define) 해시입니다. PSO 캐시 키이자 **배치 병합 키**입니다.
        uint64 _hash{ 0 };
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
    struct GPUInstanceRun
    {
        uint32 _start{ 0 };
        uint32 _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUSkinPalette
     * @brief 스킨드 메시 하나의 팔레트 구간입니다. 행은 `GPUSceneSnapshot::_pListSkinPaletteRow` 의 float4 이고, 본 하나가 세 행입니다(행벡터 4x4 의 0, 1, 2번 열).
     */
    // SW_OWNERSHIP_RAW_OK( _pMesh ): 정체성 키다. RT 의 모프 풀이 자기 구간과 짝짓는 데만 쓰고 역참조하지 않는다. 메시 소유권은 배치(`GPUMeshBatch::_mesh`)가 싣는다.
    struct GPUSkinPalette
    {
        const Mesh* _pMesh{ nullptr };
        uint32      _firstRow{ 0 };
        uint32      _boneCount{ 0 };
        uint32      _firstMorphWeight{ 0 }; ///< `GPUSceneSnapshot::_pListMorphWeight` 안에서 이 메시의 모프 가중치가 시작하는 위치
        uint32      _morphWeightCount{ 0 }; ///< 모프 가중치 수(0이면 모프 없음)
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
     * @details GT 가 그 뷰의 시점과 시선 축으로 꼬리를 다시 정렬해 만든다(`GPUSceneBuilder::computeViewTransparentOrders`). RT 는 순번 테이블을 정렬 디스패치에,
     *          꼬리 슬롯 테이블을 컬링이 없는 백엔드의 인스턴스 슬롯 스트림에, 배치 순서를 투명 패스 드로우 순서에 쓴다.
     *          메시 투명 배치가 없고 입자만 있는 뷰는 순번 · 꼬리 슬롯이 nullptr 이고 배치 순서가 비며 입자 자리만 싣는다.
     */
    struct GPUViewTransparentOrder
    {
        shared_ptr<const vector<uint32>>   _pListRank;        ///< 꼬리 인스턴스(꼬리 시작 기준)마다 이 뷰에서 그리는 순번(0이면 가장 먼저, 곧 가장 멀다)
        shared_ptr<const vector<uint32>>   _pListTailSlot;    ///< 배치마다 안쪽을 이 뷰 순서로 다시 배치한 꼬리 인스턴스 번호(전역). 꼬리 길이와 같다.
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
     * @brief 방출기 하나의 입자 그리기입니다. 투명 정렬 후보 하나이며, 투명 패스가 메시 투명 배치 사이의 제자리(`GPUTransparentParticleSlot`)에 빌보드로 그립니다(`VfxParticleRenderer`).
     * @details 입자는 방출기 로컬 좌표이고 `_world` 가 월드로 옮깁니다. 입자의 출처는 방출기의 시뮬레이션 대상(`VfxSimulationTarget`)마다 하나다 —
     *          CPU 방출기는 입자 배열을 **공유합니다**(방출기가 단계마다 새 배열을 내므로 RT 가 읽는 동안 GT 가 고쳐 쓰지 않는다 — `VfxComponent`),
     *          GPU 방출기는 누적 단계 수의 요청을 싣고 RT 가 컴퓨트로 입자를 만들어 간접 인자로 그린다(`VfxGPUSimulation`).
     *          텍스처는 경로로 싣고 RT 가 텍스처 캐시에서 빌립니다(캔버스 경로 이미지와 같다).
     */
    struct GPUParticleBatch
    {
        /** @brief 입자의 출처입니다 — CPU 시뮬레이션의 입자 배열(비지 않는다 — 빈 방출기는 싣지 않는다)이거나 GPU 시뮬레이션의 요청이다. */
        using Source = std::variant<shared_ptr<const vector<GPUParticle>>, VfxGPUEmitterRequest>;

        Source        _source{};              ///< 입자의 출처(위)
        float4x4      _world{};               ///< 방출기 행렬(행벡터 규약)
        hashed_string _texturePath{};         ///< 입자 텍스처(리소스 id)
        float32       _sizeScale{ 1.0f };     ///< 방출기 행렬의 최대 축 배율 — 입자 크기에 곱한다
        uint8         _bAdditive{ SW_FALSE }; ///< 더하기 블렌드(`VfxBlendMode::Additive`)면 SW_TRUE
        uint8         _bRibbon{ SW_FALSE };   ///< 띠(`VfxRenderMode::Ribbon`)면 SW_TRUE — 입자는 태어난 순서이고 이웃끼리 잇는다(입자 둘 이상)

        /** @brief CPU 시뮬레이션의 입자입니다. GPU 시뮬레이션이면 nullptr 입니다. */
        const vector<GPUParticle>* findCPUParticles() const
        {
            const shared_ptr<const vector<GPUParticle>>* pShared = std::get_if<shared_ptr<const vector<GPUParticle>>>( &_source );
            return pShared != nullptr ? pShared->get() : nullptr;
        }
        /** @brief GPU 시뮬레이션의 요청입니다. CPU 시뮬레이션이면 nullptr 입니다. */
        const VfxGPUEmitterRequest* findGPURequest() const { return std::get_if<VfxGPUEmitterRequest>( &_source ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GPUVfxMeshEmitter
     * @brief GPU 로 시뮬레이션하는 메시 방출기 하나입니다. 입자 그리기가 아니라 메시 인스턴스 배치(`MeshInstanceWriter::GPU`)의 인스턴스 칸에 쓴다.
     * @details GT 는 배치 항목마다 그 프레임의 인스턴스 칸(`getInstances()` 의 번호, 불투명 구간)을 찾아 싣는다 — 칸은 배치 구성이 바뀔 때마다 옮겨진다.
     *          RT 는 시뮬레이션 뒤 · 인스턴스 애니메이션과 컬링 앞에 그 칸들의 행렬 · 바운드 · 색을 쓰고(vfxmeshinstance.hlsl), 컬링이 간접 인자를 만든다.
     */
    struct GPUVfxMeshEmitter
    {
        VfxGPUEmitterRequest             _request;            ///< 시뮬레이션 요청(`GPUParticleBatch` 의 GPU 출처와 같은 뜻)
        shared_ptr<const vector<uint32>> _pListInstanceSlot;  ///< 풀 자리마다 인스턴스 칸(`invalid_index::kUint32` 면 이번 프레임에 실리지 않았다)
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
     *          - 렌더 스레드가 역참조하는 CPU 객체(메시, 머티리얼, 인스턴스)는 **shared_ptr 로 소유권을 함께 싣는다.**
     *            패킷이 살아 있는 동안(렌더 큐 깊이만큼) 게임 스레드가 놓아도 살아 있다. 생포인터는 정렬 키 같은
     *            **정체성**에만 쓰고 스레드를 넘어 역참조하지 않는다.
     *          - RT 가 upload() 에서 만드는 `GPUScene` 의 값(인스턴스와 간접 인자 버퍼, 간접 개수, 컬 뷰)은 여기 없다.
     *            그래서 옮겨질 수 없고, "한쪽만 만드는 값을 스냅샷이 매 프레임 덮어쓴다" 는 버그가 타입으로 막힌다.
     *            배치의 GPU 필드(`_vertexBuffer`, `_materialCb` 등)는 RT 가 받은 사본에 덧쓰는 값이다.
     *          - 모듈(게임 DLL)이 만든 객체를 엔진이 마지막까지 가질 수 있으므로, 싣는 객체는 Engine 의
     *            create() 팩토리로 만든다(제어 블록이 Engine.dll 에 있다).
     *          검사는 Scripts/lint/gate/CheckRenderOwnership.py 가 한다.
     */
    struct GPUSceneSnapshot
    {
        /**
         * @brief 인스턴스 배열입니다. **값이 아니라 공유합니다.** RT 는 읽기만 하므로 프레임마다 복사할 이유가 없습니다.
         *
         * @details 값으로 두면 `exportCPUSnapshot` 이 매 프레임 통째로 복사한다. 아무것도 움직이지 않는 정적 씬에서도
         *          인스턴스당 55ns(엔티티 8000개면 441µs)가 들고, 그것이 정적 씬에서 **게임 스레드의 유일한 실제 작업**이 된다.
         *
         *          배치와 머티리얼 목록은 그대로 값이다. **RT 가 GPU 핸들을 거기에 덧쓰므로**
         *          공유하면 안 된다(`GPUMeshBatch::_firstVertex`, `_materialCb` 주석 참고). 대신 개수가
         *          적어 복사가 싸다. 큰 것만 공유하고 작은 것은 복사하는 것이 이 타입의 규칙이다.
         *          배치 목록은 아래 비공개 `_listBatch` 한 벌이다(불투명 · 투명은 그 구간 뷰).
         */
        shared_ptr<const vector<GPUInstance>> _pListInstance;
        vector<GPUMaterialGroup>              _listMaterialGroup; ///< 셰이더 타입별 머티리얼 원소
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
        /// @brief 마지막 populateFromScene 이 내용을 바꿨는지 나타냅니다. RT 는 0이면 인스턴스 재업로드를 생략합니다.
        uint8 _bCPUDirty{ SW_TRUE };
        /**
         * @brief 1이면 인스턴스 배열 **전체**가 바뀌었고, 0이면 `_listDirtyInstanceRun` 만 바뀌었습니다.
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
        vector<GPUInstanceRun> _listDirtyInstanceRun;
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
        vector<GPUVfxMeshEmitter> _listVfxMeshEmitter;

        /** @brief 입자 그리기 · 그 자리 · GPU 메시 방출기를 함께 비웁니다(앞의 둘은 길이 · 순서가 같아야 한다). */
        void clearParticles()
        {
            _listParticleBatch.clear();
            _listParticleSlot.clear();
            _listVfxMeshEmitter.clear();
        }

        /** @brief 인스턴스 배열을 반환합니다. 아직 발행 전이면 빈 배열입니다. */
        const vector<GPUInstance>& getInstances() const
        {
            static const vector<GPUInstance> s_listEmpty{};
            return ( _pListInstance != nullptr ) ? *_pListInstance : s_listEmpty;
        }

        /** @brief 모든 배치(불투명 다음 투명)를 반환합니다. 간접 슬롯 순서와 같습니다. */
        const vector<GPUMeshBatch>& getAllBatches() const { return _listBatch; }
        /** @brief 모든 배치의 원소를 고쳐 쓰는 뷰입니다. RT 가 GPU 필드를 덧쓸 때 씁니다. 배치를 더하거나 빼지는 못합니다. */
        vector_reference<GPUMeshBatch> getMutableBatches() { return vector_reference<GPUMeshBatch>( _listBatch ); }
        /** @brief 불투명 배치 구간 `[0, 불투명 수)` 입니다. 목록을 바꾸면 무효입니다. */
        vector_reference<const GPUMeshBatch> getOpaqueBatches() const { return vector_reference<const GPUMeshBatch>( _listBatch ).first( _opaqueBatchCount ); }
        /** @brief 투명 배치 구간 `[불투명 수, 끝)` 입니다. 번호는 이 구간 안의 번호입니다(`GPUViewTransparentOrder::_listBatchOrder`). */
        vector_reference<const GPUMeshBatch> getTransparentBatches() const { return vector_reference<const GPUMeshBatch>( _listBatch ).subspan( _opaqueBatchCount ); }
        /** @brief 불투명 배치 수입니다. 투명 배치의 전역 번호는 이 값부터 시작합니다. */
        uint32 getOpaqueBatchCount() const { return _opaqueBatchCount; }
        /**
         * @brief 불투명 인스턴스 수입니다. 투명 인스턴스(투명 꼬리)는 이 번호부터 시작합니다.
         * @details 따로 기록하지 않고 마지막 불투명 배치의 인스턴스 구간 끝에서 읽는다. 배치의 인스턴스 구간은 0 부터 빈틈없이 이어지므로
         *          (`appendBatch` 가 지킨다) 이 값이 곧 불투명 인스턴스 수다.
         */
        uint32 getOpaqueInstanceCount() const { return computeInstanceEnd( _opaqueBatchCount ); }

        /**
         * @brief 배치를 끝에 붙입니다. 불투명 배치는 투명 배치보다 먼저 붙여야 하고, 인스턴스 구간은 앞 배치의 끝에서 시작해야 합니다.
         * @details 투명 배치 뒤에 불투명 배치를 붙이면 구간 경계가 깨지므로 멈춘다(간접 슬롯 순서가 불투명 다음 투명이다).
         *          인스턴스 구간에 빈틈이 있으면 `getOpaqueInstanceCount` 가 틀리므로 그것도 멈춘다.
         */
        void appendBatch( GPUMeshBatch&& batch )
        {
            SW_ASSERT( batch._instanceBase == computeInstanceEnd( static_cast<uint32>( _listBatch.size() ) ) );
            if ( batch._blendMode != RHIBlendMode::Transparent )
            {
                SW_ASSERT( _opaqueBatchCount == _listBatch.size() );
                ++_opaqueBatchCount;
            }
            _listBatch.push_back( std::move( batch ) );
        }
        /** @brief 배치를 모두 지웁니다. */
        void clearBatches()
        {
            _listBatch.clear();
            _opaqueBatchCount = 0;
        }
        /** @brief 투명 배치를 지우고 불투명 배치만 남깁니다(투명 꼬리를 다시 붙이기 전). 용량은 남습니다. */
        void truncateToOpaqueBatches() { _listBatch.resize( _opaqueBatchCount ); }

    private:
        /** @brief 앞쪽 `batchCount` 개 배치가 차지한 인스턴스 구간의 끝입니다(배치가 없으면 0). */
        uint32 computeInstanceEnd( uint32 batchCount ) const
        {
            if ( batchCount == 0 )
                return 0;
            const GPUMeshBatch& lastBatch = _listBatch[batchCount - 1];
            return lastBatch._instanceBase + lastBatch._instanceCount;
        }

        /**
         * @brief 배치 목록 **한 벌**입니다. 불투명 배치 `[0, _opaqueBatchCount)` 다음에 투명 배치가 오며, 간접 슬롯과 일치합니다.
         * @details 불투명 · 투명 목록을 따로 복사해 두지 않는다. 사본은 패킷마다 함께 복사되고(배치마다 shared_ptr 셋), RT 가 덧쓰는
         *          GPU 필드를 사본마다 다시 맞춰야 했다. 구간 뷰만 내주므로 어긋날 사본이 없다. 고치는 길은 위 함수뿐이라 경계가 깨지지 않는다.
         */
        vector<GPUMeshBatch> _listBatch;
        uint32               _opaqueBatchCount{ 0 }; ///< 앞쪽 불투명 배치 수. 불투명 · 투명의 경계는 이 값 하나다(인스턴스 · 원소의 경계는 여기서 끌어낸다).
    };
} // namespace sw
