/**
 * @file AnimationCrowd.h
 * @brief 군중 포즈 공유 — 같은 상태의 캐릭터들이 평가한 포즈 하나와 스키닝 결과 구간 하나를 나눕니다(언리얼 Animation Sharing 의 자리).
 * @details 묶음(`AnimationCrowdBucket`)의 열쇠는 (스켈레톤, 스킨 원본 메시, 클립, 재생 속도, 루트 묶기, 변형 번호)입니다. 변형 번호는 클립 한 바퀴를
 *          `variations_per_clip` 칸으로 나눈 위상이고, 묶음의 시각은 군중 시계 + 그 위상이라 어긋나지 않습니다(캐릭터의 시각과 반 칸 안).
 *          묶음은 렌더 메시(스킨 원본의 사본) 하나를 들고, 멤버는 그 메시를 그려 한 배치 · 한 결과 구간 · 한 팔레트가 됩니다.
 *          섞는 중 · 레이어 · 시퀀서 덮어쓰기 · 반복하지 않는 클립처럼 나눌 수 없는 프레임의 유닛은 사본 풀(`acquireSoloMesh`)에서 메시를 빌려
 *          혼자 평가합니다. LOD 가 아주 멀다고 본 유닛은 정점 애니메이션(VAT) 메시로 넘어가 CPU 포즈도 GPU 스키닝도 없습니다(`findVertexAnimationMesh`).
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    class AnimClip;
    class JsonValue;
    class Mesh;
    class Skeleton;

    /**
     * @enum AnimationCrowdMode
     * @brief 유닛이 군중 공유에서 어디에 있나입니다.
     */
    enum class AnimationCrowdMode : uint8
    {
        Own = 0,         ///< 공유를 켜지 않았다 — 자기 사본 메시 · 자기 포즈(예전 그대로)
        Shared,          ///< 묶음의 포즈 · 메시를 나눈다(포즈 단계를 돌지 않는다)
        Solo,            ///< 공유를 켰지만 이번 상태는 나눌 수 없다 — 사본 풀의 메시로 혼자 평가한다
        VertexAnimation, ///< 아주 멀다 — VAT 메시를 그린다(CPU 포즈 · GPU 스키닝 없음)
    };

    /**
     * @struct AnimSharedPoseRequest
     * @brief 유닛이 "이번 프레임 포즈를 나눌 수 있다" 고 답할 때의 내용입니다(`IAnimationPhaseTask::describeSharedPose`).
     */
    struct AnimSharedPoseRequest
    {
        const AnimClip* _pClip{ nullptr };              ///< 지금 재생하는 반복 클립 하나
        float32         _time{ 0.0f };                  ///< 유닛 자신의 클립 시각(초) — 변형 칸을 고르는 데 씁니다
        float32         _playRate{ 1.0f };              ///< 재생 속도 배율(열쇠의 일부)
        uint8           _bAnchorRootMotion{ SW_FALSE }; ///< 루트 모션 본을 시작 자리에 묶는가(열쇠의 일부)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationCrowdSettings
     * @brief 군중 공유 표(데이터, `engine/animation/animationcrowd.json`): `{ "variations_per_clip", "bucket_keep_seconds", "vertex_animation_frames_per_second" }`.
     */
    struct SW_API AnimationCrowdSettings
    {
        /** @brief 표 파일의 리소스 경로입니다. */
        static constexpr string_view kResourcePath = "engine/animation/animationcrowd.json";

        uint32  _variationsPerClip{ 4 };                  ///< 클립 하나를 몇 위상으로 나누나(많을수록 덜 똑같아 보이고 묶음 · 평가가 는다)
        float32 _bucketKeepSeconds{ 2.0f };               ///< 멤버가 없는 묶음을 이만큼 지난 뒤 지운다(상태가 오가며 메시를 다시 만들지 않게)
        float32 _vertexAnimationFramesPerSecond{ 15.0f }; ///< VAT 를 구울 프레임율

        /** @brief JSON 을 읽습니다. 모르는 키 · 범위 밖 값은 오류이고 false 입니다(기본값으로 돌아갑니다). */
        [[nodiscard]] bool parseJson( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );

    private:
        [[nodiscard]] bool parseRoot( const JsonValue& root, string_view sourceLabel );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationCrowdBucketKey
     * @brief 묶음의 열쇠입니다. 포인터는 정체성만 봅니다(묶음이 소유를 따로 듭니다).
     */
    struct AnimationCrowdBucketKey
    {
        const Skeleton* _pSkeleton{ nullptr };
        uint64          _skinDataID{ 0 };
        const AnimClip* _pClip{ nullptr };
        float32         _playRate{ 1.0f };
        uint32          _variation{ 0 };
        uint8           _bAnchorRootMotion{ SW_FALSE };

        bool operator==( const AnimationCrowdBucketKey& other ) const
        {
            return _pSkeleton == other._pSkeleton && _skinDataID == other._skinDataID && _pClip == other._pClip && _playRate == other._playRate &&
                   _variation == other._variation && _bAnchorRootMotion == other._bAnchorRootMotion;
        }
        /** @brief 변형 번호만 다른가(같은 상태)입니다. */
        bool isSameState( const AnimationCrowdBucketKey& other ) const
        {
            return _pSkeleton == other._pSkeleton && _skinDataID == other._skinDataID && _pClip == other._pClip && _playRate == other._playRate &&
                   _bAnchorRootMotion == other._bAnchorRootMotion;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief `AnimationCrowdBucketKey` 의 해시입니다. */
    struct AnimationCrowdBucketKeyHash
    {
        size_t operator()( const AnimationCrowdBucketKey& key ) const
        {
            size_t hash = reinterpret_cast<size_t>( key._pSkeleton ) * 1315423911u;
            hash ^= static_cast<size_t>( key._skinDataID ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
            hash ^= reinterpret_cast<size_t>( key._pClip ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
            hash ^= static_cast<size_t>( key._variation * 2u + key._bAnchorRootMotion ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
            return hash;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationCrowdBucket
     * @brief 같은 상태 · 같은 위상의 유닛들이 나누는 포즈 하나 · 렌더 메시 하나입니다. 군중(`AnimationCrowd`)이 소유합니다.
     */
    class SW_API AnimationCrowdBucket
    {
    public:
        AnimationCrowdBucket( const AnimationCrowdBucketKey& key, shared_ptr<const Skeleton> skeleton, shared_ptr<const AnimClip> clip, shared_ptr<Mesh> mesh );
        AnimationCrowdBucket( const AnimationCrowdBucket& )            = delete;
        AnimationCrowdBucket& operator=( const AnimationCrowdBucket& ) = delete;

        /** @brief 군중 시계로 이번 프레임 시각을 정하고 포즈 · 모델 공간 · 팔레트를 만듭니다(워커). */
        void evaluate( float64 clock );

        const AnimationCrowdBucketKey& getKey() const { return _key; }
        /** @brief 멤버가 그리는 메시(스킨 원본의 사본)입니다. */
        const shared_ptr<Mesh>& getMesh() const { return _mesh; }
        const Pose&             getLocalPose() const { return _localPose; }
        const vector<float4x4>& getModelSpaceTransforms() const { return _listModelSpace; }
        const vector<float4x4>& getSkinPalette() const { return _listSkinPalette; }
        const Skeleton&         getSkeleton() const { return *_skeleton; }
        const AnimClip&         getClip() const { return *_clip; }
        /** @brief 군중 시계 @p clock 에서 이 묶음의 클립 시각입니다. */
        float32 computeTime( float64 clock ) const;
        /** @brief 이번 프레임 멤버 수(나눔 · VAT 아님)와 그중 보이는 수입니다. */
        uint32 getMemberCount() const { return _memberCount; }
        uint32 getVisibleMemberCount() const { return _visibleMemberCount; }
        /** @brief 이 묶음을 가리키는 유닛 수입니다(0 이고 오래 쉬면 지운다). */
        uint32 getReferenceCount() const { return _referenceCount; }
        /** @brief 포즈를 만든 횟수입니다(진단 · 시험). */
        uint32 getEvaluationCount() const { return _evaluationCount; }

    private:
        friend class AnimationCrowd;

        AnimationCrowdBucketKey    _key;
        shared_ptr<const Skeleton> _skeleton;
        shared_ptr<const AnimClip> _clip;
        shared_ptr<Mesh>           _mesh;
        vector<int32>              _listTrackToBone;
        Pose                       _localPose;
        Pose                       _scratchTrackPose;
        vector<float4x4>           _listModelSpace;
        vector<float4x4>           _listSkinPalette;
        float64                    _idleSince;
        float64                    _variationSpan; ///< 변형 한 칸의 시각 폭(클립 길이 / 칸 수)
        uint32                     _memberCount;
        uint32                     _visibleMemberCount;
        uint32                     _referenceCount;
        uint32                     _evaluationCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationCrowd
     * @brief 묶음 · 사본 풀 · VAT 메시 캐시와 군중 시계입니다. `AnimationSystem` 이 하나 들고 평가 안에서 부릅니다(게임 스레드, 묶음 평가는 워커).
     */
    class SW_API AnimationCrowd
    {
    public:
        AnimationCrowd();
        ~AnimationCrowd();
        AnimationCrowd( const AnimationCrowd& )            = delete;
        AnimationCrowd& operator=( const AnimationCrowd& ) = delete;

        /** @brief 표를 정합니다. */
        void                          setSettings( const AnimationCrowdSettings& settings ) { _settings = settings; }
        const AnimationCrowdSettings& getSettings() const { return _settings; }

        /** @brief 프레임을 엽니다 — 시계를 흘리고 묶음의 이번 프레임 멤버 수를 비웁니다. */
        void beginFrame( float32 deltaSeconds );
        /**
         * @brief 요청에 맞는 묶음을 찾거나 만듭니다. @p pCurrent 가 같은 상태(변형만 다름)면 그대로 둡니다 — 시각이 같이 흐르므로 위상이 어긋나지 않습니다.
         * @return 묶음(소유는 군중). 스킨 원본이 없거나 클립 길이가 0 이면 nullptr 입니다.
         */
        AnimationCrowdBucket* joinBucket( AnimationCrowdBucket* pCurrent, const AnimSharedPoseRequest& request, const shared_ptr<const Skeleton>& skeleton,
                                          const shared_ptr<Mesh>& sourceMesh, const shared_ptr<const AnimClip>& clip );
        /** @brief 유닛이 묶음을 가리키기 시작 · 그쳤습니다(참조 수 — 가리키는 묶음은 지우지 않는다). */
        static void addReference( AnimationCrowdBucket* pBucket );
        static void releaseReference( AnimationCrowdBucket* pBucket );
        /** @brief 이번 프레임 멤버로 셉니다. 보이는 멤버가 있는 묶음만 평가합니다. */
        static void countMember( AnimationCrowdBucket& bucket, bool bVisible );
        /** @brief 이번 프레임 보이는 멤버가 있는 묶음을 평가합니다(`engine::runParallel`). */
        void evaluateBuckets();
        /** @brief 프레임을 닫습니다 — 멤버 · 참조가 없고 오래 쉰 묶음을 지웁니다. */
        void endFrame();

        /** @brief 혼자 평가할 유닛에 스킨 원본의 사본을 빌려줍니다(돌려받은 사본을 다시 씁니다 — 결과 구간 집합이 덜 흔들린다). */
        shared_ptr<Mesh> acquireSoloMesh( const shared_ptr<Mesh>& sourceMesh );
        /** @brief 빌린 사본을 돌려받습니다. */
        void releaseSoloMesh( shared_ptr<Mesh> mesh );
        /**
         * @brief 스킨 원본 · 스켈레톤 · 클립의 VAT 메시입니다. 쿠킹본(`<메시 경로>.<클립>.vat`, 배포본의 팩)이 있으면 읽고, 없으면 처음 쓸 때 굽습니다
         *        (`MeshVertexAnimationBaker` — 쿠커와 같은 함수). 굽지 못하면 nullptr 입니다.
         * @details 같은 (원본, 클립)을 쓰는 먼 캐릭터가 모두 이 메시 하나를 그려 한 배치가 됩니다.
         * @param meshPath 스킨 원본의 메시 경로(`.mesh`)입니다 — 쿠킹본 이름을 만듭니다. 비었으면 굽기만 합니다.
         */
        shared_ptr<Mesh> findVertexAnimationMesh( const shared_ptr<Mesh>& sourceMesh, const Skeleton& skeleton, const AnimClip& clip, bool bAnchorRootMotion,
                                                  string_view meshPath );
        /** @brief 쿠킹본에서 읽은 VAT 수입니다(진단 · 시험). */
        uint32 getCookedVertexAnimationCount() const { return _cookedVertexAnimationCount; }

        /** @brief 군중 시계(초)입니다. VAT 시계와 같은 값입니다(렌더 스냅샷으로 간다). */
        float64 getClock() const { return _clock; }
        /** @brief 시계를 정합니다(시험). */
        void setClock( float64 clock ) { _clock = clock; }
        /** @brief 묶음들입니다(렌더 빌더가 멤버 있는 묶음의 팔레트를 싣습니다). */
        const vector<unique_ptr<AnimationCrowdBucket>>& getBuckets() const { return _listBucket; }
        /** @brief 지난 `evaluateBuckets` 가 평가한 묶음 수입니다. */
        uint32 getEvaluatedBucketCount() const { return _evaluatedBucketCount; }
        /** @brief 구워 둔 VAT 메시 수입니다. */
        uint32 getVertexAnimationMeshCount() const { return static_cast<uint32>( _listVertexAnimation.size() ); }
        /** @brief 모두 비웁니다(묶음을 가리키는 유닛이 없을 때 — 시스템이 내려갈 때). */
        void clear();

    private:
        /** @brief 클립 한 바퀴 안에서 유닛 시각에 가장 가까운 변형 칸입니다. */
        uint32 selectVariation( const AnimSharedPoseRequest& request, float32 duration ) const;

        /** @brief 구운 VAT 메시 하나입니다. */
        struct VertexAnimationEntry
        {
            uint64           _skinDataID{ 0 };
            const Skeleton*  _pSkeleton{ nullptr };
            const AnimClip*  _pClip{ nullptr };
            uint8            _bAnchorRootMotion{ SW_FALSE };
            shared_ptr<Mesh> _mesh;
        };

        AnimationCrowdSettings                                                                     _settings;
        vector<unique_ptr<AnimationCrowdBucket>>                                                   _listBucket;
        unordered_map<AnimationCrowdBucketKey, AnimationCrowdBucket*, AnimationCrowdBucketKeyHash> _mapBucket;
        vector<AnimationCrowdBucket*>                                                              _listScratchEvaluate;
        unordered_map<uint64, vector<shared_ptr<Mesh>>>                                            _mapFreeSoloMesh; ///< 스킨 데이터 번호 → 돌려받은 사본
        vector<VertexAnimationEntry>                                                               _listVertexAnimation;
        float64                                                                                    _clock;
        uint32                                                                                     _evaluatedBucketCount;
        uint32                                                                                     _cookedVertexAnimationCount;
    };
} // namespace sw
