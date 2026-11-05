/**
 * @file ModelImportConfig.h
 * @brief 모델 임포트 규칙(`Config/Editor/ModelImportConfig.json`)을 읽고 원본 경로에 맞는 규칙을 고릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

#include "Engine/Destruction/Fracture/FractureSettings.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    enum class ConfigReadResult : uint8;

    class AssetPaths;

    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum ModelRecenter
     * @brief 합친 메시를 원점 기준으로 옮기는 방법입니다. 엔진 공간의 경계 상자를 기준으로 합니다.
     */
    ENUM()
    enum class ModelRecenter : uint8
    {
        None = 0,     ///< 원본 노드 변환을 그대로 둔다
        Xz,           ///< 경계 상자의 XZ 중심을 원점으로 옮긴다. Y 는 그대로 둔다
        BottomCenter, ///< XZ 중심을 원점으로, 가장 낮은 Y 를 0 으로 옮긴다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportLODRule
     * @brief 규칙의 `lods` 한 칸 — 메시 LOD 단계 하나를 만드는 단순화 설정입니다(`meshopt_simplify`). 단계는 원본에서 따로 단순화합니다.
     */
    REFLECT()
    struct ModelImportLODRule
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _triangleRatio{ 0.5f }; ///< 원본 삼각형 수에 대한 목표 비율(0..1)
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _screenSize{ 0.3f }; ///< 이 단계로 넘어가는 화면 크기 임계값(`MeshLODLevel::_screenSize`, 0..1, 단계마다 작아진다)
        PROPERTY( Min = 0.0 )
        float32 _maxError{ 0.01f }; ///< 허용 오차(메시 크기에 대한 비율). 목표 비율보다 먼저 닿으면 거기서 멈춘다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportRule
     * @brief 모델 원본 하나에 적용하는 임포트 규칙입니다. 규칙이 없는 원본은 기본값(옮기지 않음, 애니메이션 모두, ACL)을 씁니다.
     * @details 적용 순서는 `translation` 다음 `recenter` 이다. 스킨드 모델에는 둘 다 쓸 수 없다. 바인드 행렬이 어긋나기 때문이다.
     *          키는 `PROPERTY` 이고(`JSONFields`), 모르는 키나 틀린 값은 설정 오류다. 한 원본에 규칙이 둘 이상 맞으면 임포트 오류다(`excludePatterns` 로 풀어 쓴다).
     */
    REFLECT()
    struct ModelImportRule
    {
        REFLECT_BODY();

        PROPERTY()
        string _name;
        PROPERTY()
        AssetImportPathFilter _filter;
        PROPERTY()
        FractureSettings _fracture; ///< 적으면 `.mesh` 옆에 `.fracture` 를 쓴다(스킨 없는 메시만). 읽기가 `_bFracture` 를 켠다
        /** @brief 메시 LOD 1 이상(`lods`, 순서대로 단계 — 단계 사이 순서 검사가 있어 손으로 읽는다). 비면 LOD 0 하나입니다. 스킨 · 모프 · 머티리얼 구간이 없는 메시만 받는다(`Mesh::setLODs`). */
        vector<ModelImportLODRule> _listLOD;
        /** @brief 모든 노드의 월드 위치에 더하는 이동입니다. glTF 원본 공간이며 축 변환 전입니다. 원본이 배치 오프셋을 포함하고 있을 때 씁니다. */
        PROPERTY( Name = "translation" )
        float32 _arrTranslation[3];
        PROPERTY( Name = "clips" )
        vector<string> _listClipName; ///< 가져올 클립 이름(비면 모두 — 원본에 없는 이름은 임포트 오류)
        PROPERTY()
        string _animationCodec; ///< `AnimationCodecRegistry` 에 없으면 설정 오류
        PROPERTY()
        string _rootMotionBone; ///< 루트 모션 트랙이 될 본(비면 없음)
        PROPERTY( Min = 0.000001 )
        float32 _animationSampleRate; ///< 초당 표본 수
        PROPERTY( Min = 0.000001 )
        float32 _animationPrecision; ///< 코덱 정밀도(`AnimationCodecSettings`)
        PROPERTY( Min = 0.000001 )
        float32 _animationShellDistance; ///< 코덱 껍질 거리(미터)
        PROPERTY()
        string _bodySurface; ///< 장비 메시면 몸 `.mesh` 리소스 id — 몸의 스킨 · 체형 모프를 옮긴다(`CharacterMeshUtil::transferBodySurface`). 비면 하지 않는다
        PROPERTY( Min = 0.000001 )
        float32 _bodySurfaceDistance; ///< 몸 표면에 묶는 최대 거리(미터). 넘는 정점이 있으면 임포트 오류다
        PROPERTY()
        ModelRecenter _recenter;
        PROPERTY()
        uint8 _bImportAnimations : 1;
        PROPERTY()
        uint8 _bImportAttachments : 1; ///< 본 아래 스킨 없는 메시를 따로 임포트한다
        PROPERTY()
        uint8                  _bImportMaterials : 1; ///< glTF 머티리얼을 모델 옆 머티리얼로 옮긴다(VRM 툰 머티리얼은 늘 옮긴다). 끄면 머티리얼 색을 정점에 굽고 머티리얼 파일을 쓰지 않는다
        uint8                  _bFracture        : 1; ///< 규칙에 `fracture` 가 있으면 참이다. `.mesh` 옆에 `.fracture` 를 쓴다
        [[maybe_unused]] uint8 _reserved         : 4;

        ModelImportRule()
            : _name{}
            , _filter{}
            , _fracture{}
            , _listLOD{}
            , _arrTranslation{ 0.0f, 0.0f, 0.0f }
            , _listClipName{}
            , _animationCodec{ "acl" }
            , _rootMotionBone{}
            , _animationSampleRate{ 30.0f }
            , _animationPrecision{ 0.0001f }
            , _animationShellDistance{ 0.1f }
            , _bodySurface{}
            , _bodySurfaceDistance{ 0.05f }
            , _recenter{ ModelRecenter::None }
            , _bImportAnimations{ SW_TRUE }
            , _bImportAttachments{ SW_TRUE }
            , _bImportMaterials{ SW_TRUE }
            , _bFracture{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        /** @brief 원본 해시에 섞을 애니메이션과 부착 규칙의 문자열입니다. 규칙만 바꿔도 다시 임포트하게 합니다. */
        string makeAnimationHashText() const;
        /** @brief 원본 해시에 섞을 파쇄 규칙의 문자열입니다. 파쇄가 없으면 빈 문자열입니다. */
        string makeFractureHashText() const;
        /** @brief 원본 해시에 섞을 LOD 규칙의 문자열입니다. LOD 가 없으면 빈 문자열입니다. */
        string makeLODHashText() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 모델 규칙 선택의 결과입니다(`ModelImportConfig::findMatchingRule`). */
    using ModelImportRuleMatch = AssetImportRuleMatch<ModelImportRule>;

    /**
     * @class ModelImportConfig
     * @brief ModelImportConfig.json 의 `rules` 배열을 읽습니다. 한 원본에는 규칙이 하나만 맞아야 하고(순서는 뜻이 없다), 둘 이상 맞으면 오류입니다.
     */
    class ModelImportConfig
    {
    public:
        ModelImportConfig();
        ~ModelImportConfig() = default;

        /** @brief 파일에서 설정을 읽습니다. 파일이 없으면 `Missing`(규칙 없음 — 정상), 있는데 읽지 못하면 오류를 남기고 `Invalid` 입니다. */
        [[nodiscard]] ConfigReadResult loadFromFile( string_view configPath );

        /** @brief JSON 문자열에서 설정을 읽습니다. 모르는 키 · 값, 숫자 세 개가 아닌 `translation`, 객체가 아닌 규칙은 오류입니다. */
        [[nodiscard]] bool loadFromJSONString( string_view jsonString );

        /**
         * @brief 리소스 루트 기준 경로(예: "game/x/models_raw/a.glb")에 맞는 규칙을 반환합니다. 맞는 규칙이 없으면 기본 규칙(`_bMatched` 거짓)입니다.
         * @details 규칙 둘 이상이 맞으면 순서로 고르지 않고 `_error` 를 채웁니다. 호출부는 오류를 전해야 합니다(`[[nodiscard]]`).
         */
        [[nodiscard]] ModelImportRuleMatch findMatchingRule( string_view relativePath ) const;

        const vector<ModelImportRule>& getRules() const { return _listRule; }

        /** @brief 에디터 설정을 거치지 않은 기본 경로(`<프로젝트>/Config/Editor/ModelImportConfig.json`)를 반환합니다(프로젝트 루트는 기동 때 정한 값이라 워커에서 불러도 된다). */
        static string makeDefaultConfigPath();

    private:
        /**
         * @brief 규칙의 `lods` 배열을 읽습니다. 모르는 키, (0, 1) 밖의 비율 · 임계값, 음수 오차, 앞 단계보다 크거나 같은 비율 · 임계값이 있으면 false 입니다.
         */
        [[nodiscard]] static bool parseLODKeys( const JSONValue& lodsValue, ModelImportRule& inoutRule );

        vector<ModelImportRule> _listRule;
    };
} // namespace sw::editor
