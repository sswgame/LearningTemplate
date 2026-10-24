/**
 * @file AnimationGraphAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 애니메이션 그래프 XML 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Graph/AnimationBoneMask.h"
#include "Engine/Animation/Graph/BlendSpace.h"
#include "Engine/Common/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @enum AnimationLayerBlend
     * @brief 레이어가 아래 포즈에 합쳐지는 방법입니다(XML "blend": "Override", "Additive"). 모르는 표기는 로드 오류입니다.
     */
    enum class AnimationLayerBlend : uint8
    {
        Override = 0, ///< 마스크 안의 본을 가중치만큼 레이어 포즈로 섞습니다(상체만 조준).
        Additive,     ///< 레이어 클립의 시각 0 포즈 기준 차이를 가중치만큼 더합니다(숨쉬기, 맞음 움찔).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationGraphLayerDef
     * @brief 그래프의 레이어 하나입니다(XML "layers" 항목). 상태 기계 결과 위에 클립 하나를 마스크 안의 본에 가중치만큼 얹습니다.
     * @details XML 형식은 `<layer name="UpperBody" clip="Aim" mask="UpperBody" weight="1" blend="Override" loop="true" weight_param="AimWeight" />` 이다.
     *          name, clip, weight, blend 는 필수다. "mask" 를 적지 않으면 모든 본이고, 적었는데 "bone_masks" 에 없으면 로드 오류다.
     *          "weight_param" 이 있으면 매 프레임 그 파라미터 값(0..1 로 자름)이 가중치다(게임 코드가 레이어 번호를 몰라도 파라미터로 켜고 끈다).
     *          언리얼 Layered Blend Per Bone, 유니티 Animator 레이어(마스크, 가중치, Override/Additive)에 해당한다.
     */
    struct AnimationGraphLayerDef
    {
        hashed_string       _name{};
        hashed_string       _clipName{};
        hashed_string       _maskName{};        ///< 비었으면 모든 본이다.
        hashed_string       _weightParameter{}; ///< 비었으면 `_weight` 를 그대로 쓴다.
        float32             _weight{ 1.0f };
        float32             _halfLifeSeconds{ 0.0f }; ///< XML "half_life"(선택, 0 보다 커야 함). 다시 시작할 때 끊긴 포즈에서 이어 가는 반감기다. 적지 않으면 즉시 새 포즈다
        AnimationLayerBlend _blend{ AnimationLayerBlend::Override };
        uint8               _bLoop{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    class XMLNode;

    /**
     * @enum AnimationBlendSpaceNotifyMode
     * @brief 블렌드 스페이스 상태가 표본 클립의 알림을 내는 방법입니다(XML "blend_space"."notify": "all", "highest_weight", "none"). 모르는 표기는 로드 오류입니다.
     * @details 언리얼 블렌드 스페이스의 Notify Trigger Mode 와 같다. 키를 적지 않으면 `All` 이다 — 언리얼의 기본값이고, 유니티 블렌드 트리도 섞이는 클립의 이벤트를 모두 낸다.
     *          알림의 출처(`AnimationFiredNotify::_pSource`)는 표본 클립이고 시각은 그 클립의 시각이다. 가중치는 상태 가중치 × 표본 가중치다.
     */
    enum class AnimationBlendSpaceNotifyMode : uint8
    {
        All = 0,       ///< 가중치가 0 보다 큰 표본의 알림을 모두 냅니다(받는 쪽이 가중치로 거릅니다).
        HighestWeight, ///< 가중치가 가장 큰 표본 하나의 알림만 냅니다(발소리가 두 번 울리지 않습니다).
        None,          ///< 알림을 내지 않습니다.
    };

    /**
     * @struct AnimationGraphBlendSpaceDef
     * @brief 블렌드 스페이스 상태의 데이터입니다(XML 노드의 "blend_space"). 축 파라미터 이름과 표본(클립 이름, 위치)을 가집니다.
     * @details XML 형식은 `<blend_space x="Speed" y="Direction" notify="highest_weight"><samples><sample clip="Idle" x="0" y="0" />…</samples></blend_space>` 이다. "y" 를 적지 않으면 1D 다.
     *          표본이 없거나, 클립 이름이나 "x" 파라미터가 비거나, "notify" 표기를 모르면 로드 오류다.
     */
    struct AnimationGraphBlendSpaceDef
    {
        BlendSpace                    _space;                                            /**< 표본과 축 수입니다. 표본이 없으면 블렌드 스페이스 상태가 아닙니다. */
        hashed_string                 _parameterX;                                       /**< x 축 파라미터 이름(`AnimationParameterSet`)입니다. */
        hashed_string                 _parameterY;                                       /**< y 축 파라미터 이름입니다. 1D 면 비어 있습니다. */
        AnimationBlendSpaceNotifyMode _notifyMode{ AnimationBlendSpaceNotifyMode::All }; /**< 표본 클립의 알림을 내는 방법(XML "notify", 선택)입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimationPoseNodeType
     * @brief 포즈를 직접 평가해 내는 노드의 종류입니다(XML 노드의 "pose_node"."type"). 모르는 표기는 로드 오류입니다.
     * @details 종류마다 런타임 구현(`IAnimationPoseNode`)이 하나다. 모션 매칭 같은 새 노드는 여기에 종류를 더하고 `AnimationPoseNodeSet` 이 만든다.
     */
    enum class AnimationPoseNodeType : uint8
    {
        None = 0,   ///< 포즈 노드가 아닙니다(이름이 곧 클립이거나 블렌드 스페이스인 보통 상태).
        BlendPoses, ///< "blend_poses" — 입력 클립 둘을 파라미터 가중치로 섞습니다(`BlendPosesNode`).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationGraphPoseNodeDef
     * @brief 포즈 노드 상태의 데이터입니다(XML 노드의 "pose_node").
     * @details XML 형식은 `<pose_node type="blend_poses" weight="WaveWeight"><inputs><item>Idle</item><item>Wave</item></inputs></pose_node>` 이다.
     *          "blend_poses" 는 입력이 정확히 둘이고 "weight" 파라미터 이름이 있어야 한다. 어기면 로드 오류다(기본값으로 채우지 않는다).
     */
    struct AnimationGraphPoseNodeDef
    {
        AnimationPoseNodeType _type{ AnimationPoseNodeType::None }; /**< 노드 종류입니다. None 이면 포즈 노드 상태가 아닙니다. */
        vector<hashed_string> _listInput;                           /**< 입력 클립 이름입니다(그래프 플레이어의 클립 풀이로 찾습니다). */
        hashed_string         _weightParameter;                     /**< 섞기 가중치 파라미터 이름(`AnimationParameterSet`)입니다. */
    };
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 그래프 노드입니다. */
    struct AnimationGraphNode
    {
        string _name;    /**< 노드 이름. 클립을 묶는 열쇠입니다. */
        int32  _id{ 0 }; /**< 그래프 안에서 유일한 노드 id 입니다. 1 이상이어야 합니다. */
        /** @brief 그래프 에디터에서의 노드 위치입니다. XML 키는 "x", "y" 그대로라 파일 형식은 바뀌지 않습니다. */
        float2 _position{ 40.0f, 40.0f };
        /**
         * @brief 이 상태에 있는 동안 들어갈 동기 그룹 이름입니다(XML "sync_group", 선택). 같은 그룹의 재생은 가장 무거운 쪽의 위상을 따릅니다.
         * @details 언리얼 애님 그래프의 Sync Group, 유니티 블렌드 트리의 동기화에 해당한다. 비어 있으면 컴포넌트가 정한 그룹을 그대로 쓴다.
         */
        hashed_string _syncGroup{};
        /** @brief 블렌드 스페이스 상태의 데이터입니다(XML "blend_space", 선택). 표본이 없으면 이름이 곧 클립인 보통 상태입니다. */
        AnimationGraphBlendSpaceDef _blendSpace{};
        /** @brief 포즈 노드 상태의 데이터입니다(XML "pose_node", 선택). 블렌드 스페이스와 함께 적으면 로드 오류입니다. */
        AnimationGraphPoseNodeDef _poseNode{};
        /**
         * @brief 이 상태의 반복 여부입니다(XML "loop", 선택). -1 은 적지 않았다는 뜻입니다. 직접 재생은 호출한 쪽이 정하고, "끝나면 다음"으로 들어오면 반복하지 않습니다.
         */
        int8 _loopOverride{ -1 };

        /** @brief 블렌드 스페이스 상태인지 반환합니다. */
        bool hasBlendSpace() const { return _blendSpace._space.getSampleCount() > 0; }
        /** @brief 포즈 노드 상태인지 반환합니다. */
        bool hasPoseNode() const { return _poseNode._type != AnimationPoseNodeType::None; }
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimationConditionOp
     * @brief 전이 조건의 비교입니다(XML "op": ">", "<", ">=", "<=", "==", "!=", "trigger"). 모르는 표기는 로드 오류입니다.
     */
    enum class AnimationConditionOp : uint8
    {
        None = 0,     ///< 조건이 없습니다. 현재 상태가 반복 없이 끝나면 넘어갑니다("끝나면 다음").
        Greater,      ///< 파라미터가 값보다 크다.
        Less,         ///< 파라미터가 값보다 작다.
        GreaterEqual, ///< 파라미터가 값보다 크거나 같다.
        LessEqual,    ///< 파라미터가 값보다 작거나 같다.
        Equal,        ///< 파라미터가 값과 같다.
        NotEqual,     ///< 파라미터가 값과 다르다.
        Trigger,      ///< 트리거가 켜져 있습니다. 전이가 일어나면 끕니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimationBlendMode
     * @brief 전이가 포즈를 잇는 방식입니다(XML 링크의 "blend_mode": "crossfade", "inertialization"). 모르는 표기는 로드 오류입니다.
     * @details 키를 적지 않은 링크는 `Crossfade` 다 — 이 필드의 데이터 기본값이며, 이 필드가 생기기 전 파일의 뜻과 같다.
     */
    enum class AnimationBlendMode : uint8
    {
        Crossfade = 0,   ///< 두 재생을 함께 샘플해 "blend" 길이 동안 섞습니다.
        Inertialization, ///< 새 재생 하나만 샘플하고, 끊긴 순간과의 차이를 "half_life" 반감기로 풀어 냅니다(`PoseInertializer`).
    };
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 그래프 링크(상태 전이)입니다. */
    struct SW_API AnimationGraphLink
    {
        int32   _id{ 0 };               /**< 그래프 안에서 유일한 링크 id 입니다. 1 이상이어야 합니다. */
        int32   _fromNode{ 0 };         /**< 출발 노드 id 입니다. */
        int32   _toNode{ 0 };           /**< 도착 노드 id 입니다. */
        float32 _blendSeconds{ -1.0f }; /**< 크로스페이드 길이(XML "blend", 선택). 음수면 플레이어 기본값입니다. */
        /** @brief inertialization 반감기(초, XML "half_life")입니다. "blend_mode" 가 "inertialization" 이면 반드시 0 보다 큰 값을 적어야 합니다(없으면 로드 오류). */
        float32              _halfLifeSeconds{ 0.0f };
        hashed_string        _parameter;         /**< 조건 파라미터 이름(XML "condition"."param")입니다. */
        float32              _threshold{ 0.0f }; /**< 조건 비교값(XML "condition"."value")입니다. */
        AnimationConditionOp _op{ AnimationConditionOp::None };
        AnimationBlendMode   _blendMode{ AnimationBlendMode::Crossfade }; /**< 포즈를 잇는 방식(XML "blend_mode", 선택)입니다. */

        /** @brief 조건이 지금 파라미터 값으로 참인지 봅니다. 조건이 없으면 false 입니다(끝나면 다음은 따로 봅니다). */
        bool isConditionMet( float32 parameterValue ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationGraphAsset
     * @brief BlendSpace 와 SpriteAnimator 가 노드 이름을 함께 쓰는 XML 그래프입니다.
     */
    class SW_API AnimationGraphAsset
    {
    public:
        /** @brief 애니메이션 그래프 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".animgraph.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `AnimationGraphAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "AnimationGraphAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief 빈 그래프를 만듭니다. */
        AnimationGraphAsset() = default;

        /** @brief 리소스 id 의 XML 을 읽습니다(낱개 파일 또는 팩). OS 경로는 받지 않습니다 — 디스크 파일은 `loadFromDiskFile` 입니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief **디스크 파일**의 XML 을 읽습니다. 리소스 id 로 풀지 않고 팩을 보지 않습니다(에디터 문서 · 도구 · 테스트용). */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief XML 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief XML 본문을 파싱합니다. */
        [[nodiscard]] bool parseXML( string_view xml );
        /** @brief XML 본문을 만듭니다. */
        string toXML() const;
        /** @brief 노드 이름 목록을 채웁니다. */
        void collectNodeNames( vector<string>& outListName ) const;
        /** @brief id 로 노드를 찾습니다. */
        const AnimationGraphNode* findNode( int32 nodeID ) const;
        /** @brief 이름으로 노드를 찾습니다. */
        const AnimationGraphNode* findNodeByName( string_view name ) const;
        /** @brief 진입 노드(들어오는 링크가 없는 첫 노드, 없으면 목록 앞)를 반환합니다. */
        const AnimationGraphNode* findEntryNode() const;
        /** @brief 조건 없는 첫 나가는 링크("끝나면 다음")입니다. 없으면 nullptr 입니다. */
        const AnimationGraphLink* findFinishLink( int32 fromNodeID ) const;
        /** @brief 조건 표기를 읽습니다. 모르는 표기면 false 입니다. */
        [[nodiscard]] static bool parseConditionOp( string_view text, AnimationConditionOp& outOp );
        /** @brief 조건 표기를 씁니다. */
        static const utf8* getConditionOpText( AnimationConditionOp op );
        /** @brief 전이 방식 표기("crossfade", "inertialization")를 읽습니다. 모르는 표기면 false 입니다. */
        [[nodiscard]] static bool parseBlendMode( string_view text, AnimationBlendMode& outMode );
        /** @brief 전이 방식 표기를 씁니다. */
        static const utf8* getBlendModeText( AnimationBlendMode mode );
        /** @brief 레이어 합치기 표기("Override", "Additive")를 씁니다. */
        static const utf8* getLayerBlendText( AnimationLayerBlend blend );
        /** @brief 이름으로 본 마스크를 찾습니다. 없으면 nullptr 입니다. */
        const AnimationBoneMaskDef* findBoneMask( const hashed_string& name ) const;
        /** @brief 이름으로 레이어를 찾습니다. 없으면 nullptr 입니다. */
        const AnimationGraphLayerDef* findLayer( const hashed_string& name ) const;

        vector<AnimationGraphNode>     _listNode;     /**< 노드 목록입니다. */
        vector<AnimationGraphLink>     _listLink;     /**< 링크 목록입니다. */
        vector<AnimationBoneMaskDef>   _listBoneMask; /**< 본 마스크 목록입니다(XML "bone_masks"). 레이어가 이름으로 가리킵니다. */
        vector<AnimationGraphLayerDef> _listLayer;    /**< 레이어 목록입니다(XML "layers"). 적은 순서대로 얹습니다. */

    private:
        /** @brief 이미 파싱된 XML 루트에서 노드와 링크를 읽습니다. 모르는 조건 표기나 잘못된 블렌드 스페이스면 false 를 반환합니다. @p sourceLabel 은 오류에 적는 이름입니다. */
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );
        /** @brief "nodes" 의 노드를 읽어 목록에 더합니다. 깨진 노드는 오류를 남기고 건너뛰며 false 를 돌려줍니다. */
        [[nodiscard]] bool parseNodes( const XMLNode& root, string_view sourceLabel );
        /** @brief "links" 의 링크를 읽어 목록에 더합니다. 깨진 링크는 오류를 남기고 건너뛰며 false 를 돌려줍니다. */
        [[nodiscard]] bool parseLinks( const XMLNode& root, string_view sourceLabel );
        /** @brief "bone_masks" 를 읽어 목록에 더합니다. 이름이 비거나 겹치거나 본문이 깨졌으면 오류를 남기고 건너뛰며 false 를 돌려줍니다. */
        [[nodiscard]] bool parseBoneMasks( const XMLNode& root );
        /** @brief "layers" 를 읽어 목록에 더합니다(`parseBoneMasks` 뒤에 부른다 — 마스크 이름을 확인한다). 깨진 레이어는 오류를 남기고 건너뛰며 false 를 돌려줍니다. */
        [[nodiscard]] bool parseLayerDefs( const XMLNode& root );
        /** @brief 노드의 "blend_space" 객체를 읽습니다. 잘못되었으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parseBlendSpace( const XMLNode& blendSpaceXML, AnimationGraphNode& inoutNode );
        /** @brief 노드의 "pose_node" 객체를 읽습니다. 모르는 종류이거나 종류가 요구하는 값이 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePoseNode( const XMLNode& poseNodeXML, AnimationGraphNode& inoutNode );
        /** @brief "bone_masks" 와 "layers" 를 읽습니다. 이름이 비거나 겹치거나, 레이어가 없는 마스크를 가리키면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool parseLayers( const XMLNode& root );
        /** @brief "bone_masks" 와 "layers" 를 씁니다. */
        void writeLayers( const XMLNode& root ) const;
        /** @brief 목록을 모두 비웁니다. */
        void clear();
    };
} // namespace sw
