/**
 * @file ModelImportConfig.h
 * @brief 모델 임포트 규칙(`Config/Editor/ModelImportConfig.json`)을 읽고 원본 경로에 맞는 규칙을 고릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

namespace sw
{
    class JsonValue;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum ModelRecenter
     * @brief 합친 메시를 원점 기준으로 옮기는 방법입니다(엔진 공간의 경계 상자 기준).
     */
    enum class ModelRecenter : uint8
    {
        None = 0,     ///< 원본 노드 변환 그대로입니다.
        Xz,           ///< 경계 상자의 XZ 중심을 원점으로 옮깁니다. Y 는 그대로입니다.
        BottomCenter, ///< XZ 중심을 원점으로, 가장 낮은 Y 를 0 으로 옮깁니다.
    };

    /**
     * @struct ModelImportRule
     * @brief 모델 원본 하나에 적용하는 임포트 규칙입니다. 규칙이 없는 원본은 기본값(옮기지 않음 · 애니메이션 모두 · ACL)입니다.
     * @details 적용 순서는 `translation` 다음 `recenter` 입니다(스킨드 모델에는 둘 다 쓸 수 없습니다 — 바인드 행렬이 어긋납니다).
     *          애니메이션 키(JSON): `animations`(불, 기본 참) · `clips`(가져올 클립 이름 배열, 비면 모두 — 원본에 없는 이름은 임포트 오류) ·
     *          `animation_codec`(코덱 이름, `AnimCodecRegistry` 에 없는 이름은 설정 오류) · `animation_sample_rate`(초당 표본) ·
     *          `animation_precision` · `animation_shell_distance`(미터, `AnimCodecSettings`) · `root_motion_bone`(루트 모션 트랙이 될 본 — 비면 없음) ·
     *          `attachments`(불, 기본 참 — 본 아래 스킨 없는 메시를 따로 임포트).
     */
    struct ModelImportRule
    {
        string                _name;
        AssetImportPathFilter _filter;
        /** @brief 모든 노드의 월드 위치에 더하는 이동입니다(glTF 원본 공간 — 축 변환 전). 원본이 배치 오프셋을 품고 있을 때 씁니다. */
        float32        _arrTranslation[3];
        vector<string> _listClipName;
        string         _animationCodec;
        string         _rootMotionBone;
        float32        _animationSampleRate;
        float32        _animationPrecision;
        float32        _animationShellDistance;
        ModelRecenter  _recenter;
        uint8          _bImportAnimations;
        uint8          _bImportAttachments;

        ModelImportRule()
            : _name{}
            , _filter{}
            , _arrTranslation{ 0.0f, 0.0f, 0.0f }
            , _listClipName{}
            , _animationCodec{ "acl" }
            , _rootMotionBone{}
            , _animationSampleRate{ 30.0f }
            , _animationPrecision{ 0.0001f }
            , _animationShellDistance{ 0.1f }
            , _recenter{ ModelRecenter::None }
            , _bImportAnimations{ SW_TRUE }
            , _bImportAttachments{ SW_TRUE }
        {
        }

        /** @brief 원본 해시에 섞을 애니메이션 · 부착 규칙의 글입니다(규칙만 바꿔도 다시 임포트하게). */
        string makeAnimationHashText() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ModelImportConfig
     * @brief ModelImportConfig.json 의 `rules` 배열을 읽습니다. 첫 매칭이 이깁니다.
     */
    class ModelImportConfig
    {
    public:
        ModelImportConfig();
        ~ModelImportConfig() = default;

        /** @brief 파일에서 설정을 읽습니다. */
        [[nodiscard]] bool loadFromFile( string_view configPath );

        /** @brief JSON 문자열에서 설정을 읽습니다. 모르는 `recenter` 값, 숫자 셋이 아닌 `translation`, 객체가 아닌 규칙은 오류입니다. */
        [[nodiscard]] bool loadFromJsonString( string_view jsonString );

        /** @brief 리소스 루트 기준 경로(예: "game/x/models_raw/a.glb")에 처음으로 맞는 규칙입니다. 없으면 기본 규칙입니다. */
        ModelImportRule findMatchingRule( string_view relativePath ) const;

        const vector<ModelImportRule>& getRules() const { return _listRule; }

        /** @brief 규칙의 애니메이션 · 부착 키를 읽습니다. 모르는 코덱 · 양수가 아닌 숫자는 false 입니다. */
        [[nodiscard]] static bool parseAnimationKeys( const JsonValue& ruleValue, ModelImportRule& inoutRule );

        /** @brief `recenter` 값 이름을 읽습니다("none" · "xz" · "bottom-center"). 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool parseRecenter( string_view text, ModelRecenter& outRecenter );

        /** @brief 에디터 설정을 거치지 않은 기본 경로(`<프로젝트>/Config/Editor/ModelImportConfig.json`)입니다. */
        static string makeDefaultConfigPath();

    private:
        vector<ModelImportRule> _listRule;
    };
} // namespace sw::editor
