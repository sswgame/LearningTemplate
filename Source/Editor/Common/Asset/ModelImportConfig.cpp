#include "pch.h"

#include "Editor/Common/Asset/ModelImportConfig.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/Codec/AnimCodec.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Serialization/Json/ConfigKeyDoc.h"
#include "Engine/Serialization/Json/JsonDocument.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImportConfig" );

    namespace
    {
        /** @brief 파일 뿌리입니다. */
        static constexpr ConfigKeyDoc kArrModelImportRootKeyDoc[] = {
            { "rules", "object[]", "", "규칙 목록(아래 키). 첫 매칭이 이긴다. 맞는 규칙이 없는 원본은 기본값(옮기지 않음 · 애니메이션 모두 · ACL)" },
        };
        /** @brief 규칙 하나입니다. 적용 순서는 `translation` 다음 `recenter` — 스킨드 모델에는 둘 다 쓸 수 없다(바인드 행렬이 어긋난다). */
        static constexpr ConfigKeyDoc kArrModelImportRuleKeyDoc[] = {
            {                    "name",    "string",        "",                                                        "규칙 이름(로그에 나온다)"},
            {        "include_patterns",  "string[]",        "",                                "맞아야 하는 와일드카드(`*` · `?`, 대소문자 무시)"},
            {        "exclude_patterns",  "string[]",        "",                                                          "맞으면 빼는 와일드카드"},
            {           "include_paths",  "string[]",        "",                                                      "들어 있어야 하는 경로 조각"},
            {           "exclude_paths",  "string[]",        "",                                                      "들어 있으면 빼는 경로 조각"},
            {             "translation", "number[3]", "0, 0, 0",                  "모든 노드 월드 위치에 더하는 이동(glTF 원본 공간 — 축 변환 전)"},
            {                "recenter",    "string",    "none", "none · xz(경계 상자 XZ 중심을 원점) · bottom-center(XZ 중심 + 가장 낮은 Y 를 0)"},
            {              "animations",      "bool",    "true",                                                           "애니메이션을 가져온다"},
            {                   "clips",  "string[]",        "",                    "가져올 클립 이름(비면 모두 — 원본에 없는 이름은 임포트 오류)"},
            {         "animation_codec",    "string",     "acl",                   "애니메이션 코덱 이름(`AnimCodecRegistry` 에 없으면 설정 오류)"},
            {   "animation_sample_rate",    "number",      "30",                                                                    "초당 표본 수"},
            {     "animation_precision",    "number",  "0.0001",                                                "코덱 정밀도(`AnimCodecSettings`)"},
            {"animation_shell_distance",    "number",     "0.1",                                                            "코덱 껍질 거리(미터)"},
            {        "root_motion_bone",    "string",        "",                                               "루트 모션 트랙이 될 본(비면 없음)"},
            {             "attachments",      "bool",    "true",                                        "본 아래 스킨 없는 메시를 따로 임포트한다"},
            {                "fracture",    "object",        "",              "있으면 `.mesh` 옆에 `.fracture` 를 쓴다(스킨 없는 메시만, 아래 키)"},
        };
        // 기본값 글은 `FractureSettings::FractureSettings` 와 맞춘 것이다 — 그 생성자를 바꾸면 이 표도 바꾼다.
        /** @brief `fracture` 객체입니다(`FractureSettings`). */
        static constexpr ConfigKeyDoc kArrModelImportFractureKeyDoc[] = {
            {          "pattern",    "string",             "uniform",                   "uniform · clustered(맞은 자리 둘레에 몰림) · slices(격자에 흔들림)"},
            {           "volume",    "string",                "mesh", "mesh(닫힌 메시) · bounds(경계 상자) · hull(볼록 껍질) — 닫히지 않은 모델의 대리 부피"},
            {           "pieces",    "number",                  "16",                                                         "조각 수(uniform · clustered)"},
            {             "seed",    "number",                   "1",                                                                            "씨앗 난수"},
            {           "levels",  "number[]",                    "",                             "클러스터 레벨마다 클러스터 수(위 → 아래), 비면 뿌리 하나"},
            {     "impact_point", "number[3]",             "0, 0, 0",                                                      "clustered: 맞은 자리(메시 공간)"},
            {   "cluster_radius",    "number",                 "0.5",                                                         "clustered: 몰리는 반경(미터)"},
            { "cluster_fraction",    "number",                 "0.7",                                            "clustered: 반경 안에 놓을 씨앗 비율(0..1)"},
            {           "slices", "number[3]",             "4, 1, 1",                                                                 "slices: 축마다 셀 수"},
            {     "slice_jitter",    "number",                "0.15",                                                "slices: 셀 크기에 대한 흔들림(0..0.5)"},
            {   "interior_color", "number[4]", "0.62, 0.58, 0.52, 1",                                                                      "안쪽 면 정점 색"},
            {"interior_uv_scale",    "number",                   "1",                                                            "안쪽 면 UV 의 미터당 배율"},
            {  "max_hull_points",    "number",                  "48",                                                                  "조각 껍질 점의 상한"},
        };
    } // namespace

    string ModelImportRule::makeAnimationHashText() const
    {
        string text = "anim=";
        text += _bImportAnimations == SW_TRUE ? "1" : "0";
        text += ";attach=";
        text += _bImportAttachments == SW_TRUE ? "1" : "0";
        text += ";codec=" + _animationCodec + ";root=" + _rootMotionBone + ";clips=";
        for ( const string& clipName : _listClipName )
        {
            text += clipName + ",";
        }
        // 실수는 비트 그대로 섞는다 — 글자로 반올림하면 작은 변경이 같은 해시가 된다.
        const float32 arrValue[3] = { _animationSampleRate, _animationPrecision, _animationShellDistance };
        for ( const float32 value : arrValue )
        {
            uint32 bits = 0;
            Memory::copy( &bits, &value, sizeof( bits ) );
            text += ";" + to_string( bits );
        }
        return text;
    }

    string ModelImportRule::makeFractureHashText() const
    {
        if ( _bFracture == SW_FALSE )
            return {};
        string text = "fracture=";
        text += FractureSettings::getPatternName( _fracture._pattern );
        text += ";volume=";
        text += FractureSettings::getVolumeName( _fracture._volume );
        text += ";pieces=" + to_string( _fracture._pieceCount ) + ";seed=" + to_string( _fracture._seed ) + ";hull=" + to_string( _fracture._maxHullPoint ) + ";levels=";
        for ( const uint32 count : _fracture._listLevelCount )
        {
            text += to_string( count ) + ",";
        }
        text += ";slices=";
        for ( const uint32 count : _fracture._arrSliceCount )
        {
            text += to_string( count ) + ",";
        }
        const float32 arrValue[11] = { _fracture._impactPoint._x, _fracture._impactPoint._y, _fracture._impactPoint._z, _fracture._clusterRadius,
                                       _fracture._clusterFraction, _fracture._sliceJitter, _fracture._interiorUvScale, _fracture._interiorColor._x,
                                       _fracture._interiorColor._y, _fracture._interiorColor._z, _fracture._interiorColor._w };
        for ( const float32 value : arrValue )
        {
            uint32 bits = 0;
            Memory::copy( &bits, &value, sizeof( bits ) );
            text += ";" + to_string( bits );
        }
        return text;
    }

    ModelImportConfig::ModelImportConfig()
        : _listRule{}
    {
    }

    bool ModelImportConfig::loadFromFile( string_view configPath )
    {
        string text;
        if ( FileUtil::readTextFile( configPath, text ) == false || text.empty() )
        {
            SW_LOG_WARNING( "Failed to read ModelImportConfig file: %#", configPath );
            return false;
        }
        return loadFromJsonString( text );
    }

    bool ModelImportConfig::loadFromJsonString( string_view jsonString )
    {
        _listRule.clear();

        JsonDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR( "Failed to parse ModelImportConfig JSON." );
            return false;
        }
        const JsonValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR( "ModelImportConfig root is not an object." );
            return false;
        }
        if ( ConfigKeyDocUtil::hasOnlyKnownKeys( root, kArrModelImportRootKeyDoc, "ModelImportConfig" ) == false )
            return false;

        // 규칙이 조용히 기본값이 되면 임포트 결과가 말없이 바뀐다 — 망가진 규칙은 설정 전체를 거부한다.
        const JsonValue rulesValue = root.get( "rules" );
        const size_t    ruleCount  = rulesValue.isArray() ? rulesValue.size() : 0;
        for ( size_t ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex )
        {
            const JsonValue ruleValue = rulesValue.at( ruleIndex );
            if ( ruleValue.isObject() == false )
            {
                SW_LOG_ERROR( "ModelImportConfig rule %# is not an object.", ruleIndex );
                _listRule.clear();
                return false;
            }

            ModelImportRule rule;
            if ( ruleValue.has( "name" ) )
                rule._name = ruleValue.get( "name" ).asString();
            // 모르는 키는 설정 오류다 — 철자가 틀린 규칙이 조용히 기본값이 되지 않게.
            const bool bKnownKeys = ConfigKeyDocUtil::hasOnlyKnownKeys( ruleValue, kArrModelImportRuleKeyDoc, "ModelImportConfig rule" );
            if ( bKnownKeys == false || parseAnimationKeys( ruleValue, rule ) == false )
            {
                _listRule.clear();
                return false;
            }
            if ( ruleValue.has( "fracture" ) && parseFractureKeys( ruleValue.get( "fracture" ), rule ) == false )
            {
                _listRule.clear();
                return false;
            }
            rule._filter.parse( ruleValue );
            if ( ruleValue.has( "translation" ) )
            {
                const JsonValue translationValue = ruleValue.get( "translation" );
                const bool      bThreeNumbers    = translationValue.isArray() && translationValue.size() == 3 && translationValue.at( 0 ).isNumber() &&
                                           translationValue.at( 1 ).isNumber() && translationValue.at( 2 ).isNumber();
                if ( bThreeNumbers == false )
                {
                    SW_LOG_ERROR( "ModelImportConfig rule '%#': translation must be an array of three numbers.", rule._name.c_str() );
                    _listRule.clear();
                    return false;
                }
                for ( size_t axis = 0; axis < 3; ++axis )
                {
                    rule._arrTranslation[axis] = static_cast<float32>( translationValue.at( axis ).asFloat() );
                }
            }
            if ( ruleValue.has( "recenter" ) )
            {
                const string recenterText = ruleValue.get( "recenter" ).asString();
                if ( parseRecenter( recenterText, rule._recenter ) == false )
                {
                    SW_LOG_ERROR( "ModelImportConfig rule '%#': unknown recenter '%#' (none | xz | bottom-center).", rule._name.c_str(), recenterText.c_str() );
                    _listRule.clear();
                    return false;
                }
            }
            _listRule.push_back( rule );
        }
        return true;
    }

    bool ModelImportConfig::parseAnimationKeys( const JsonValue& ruleValue, ModelImportRule& inoutRule )
    {
        if ( ruleValue.has( "animations" ) )
            inoutRule._bImportAnimations = ruleValue.get( "animations" ).asBool( true ) ? SW_TRUE : SW_FALSE;
        if ( ruleValue.has( "attachments" ) )
            inoutRule._bImportAttachments = ruleValue.get( "attachments" ).asBool( true ) ? SW_TRUE : SW_FALSE;
        if ( ruleValue.has( "root_motion_bone" ) )
            inoutRule._rootMotionBone = ruleValue.get( "root_motion_bone" ).asString();
        if ( ruleValue.has( "clips" ) )
        {
            const JsonValue clips = ruleValue.get( "clips" );
            for ( size_t clipIndex = 0; clips.isArray() && clipIndex < clips.size(); ++clipIndex )
            {
                inoutRule._listClipName.push_back( clips.at( clipIndex ).asString() );
            }
        }
        if ( ruleValue.has( "animation_codec" ) )
        {
            inoutRule._animationCodec = ruleValue.get( "animation_codec" ).asString();
            if ( AnimCodecRegistry::findCodecByName( inoutRule._animationCodec ) == nullptr )
            {
                SW_LOG_ERROR( "ModelImportConfig rule '%#': unknown animation_codec '%#' (raw | acl).", inoutRule._name.c_str(), inoutRule._animationCodec.c_str() );
                return false;
            }
        }
        const utf8* const arrNumberKey[3]   = { "animation_sample_rate", "animation_precision", "animation_shell_distance" };
        float32* const    arrNumberValue[3] = { &inoutRule._animationSampleRate, &inoutRule._animationPrecision, &inoutRule._animationShellDistance };
        for ( uint32 keyIndex = 0; keyIndex < 3; ++keyIndex )
        {
            if ( ruleValue.has( arrNumberKey[keyIndex] ) == false )
                continue;
            const JsonValue value = ruleValue.get( arrNumberKey[keyIndex] );
            if ( value.isNumber() == false || value.asFloat() <= 0.0 )
            {
                SW_LOG_ERROR( "ModelImportConfig rule '%#': %# must be a positive number.", inoutRule._name.c_str(), arrNumberKey[keyIndex] );
                return false;
            }
            *arrNumberValue[keyIndex] = static_cast<float32>( value.asFloat() );
        }
        return true;
    }

    bool ModelImportConfig::parseFractureKeys( const JsonValue& fractureValue, ModelImportRule& inoutRule )
    {
        const string context = "ModelImportConfig rule '" + inoutRule._name + "' fracture";
        if ( fractureValue.isObject() == false )
        {
            SW_LOG_ERROR( "%#: must be an object.", context.c_str() );
            return false;
        }
        const bool bKnownKeys = ConfigKeyDocUtil::hasOnlyKnownKeys( fractureValue, kArrModelImportFractureKeyDoc, context );
        if ( bKnownKeys == false )
            return false;
        FractureSettings& settings = inoutRule._fracture;
        if ( fractureValue.has( "volume" ) )
        {
            const string volumeText = fractureValue.get( "volume" ).asString();
            if ( FractureSettings::parseVolume( volumeText, settings._volume ) == false )
            {
                SW_LOG_ERROR( "%#: unknown volume '%#' (mesh | bounds | hull).", context.c_str(), volumeText.c_str() );
                return false;
            }
        }
        if ( fractureValue.has( "pattern" ) )
        {
            const string patternText = fractureValue.get( "pattern" ).asString();
            if ( FractureSettings::parsePattern( patternText, settings._pattern ) == false )
            {
                SW_LOG_ERROR( "%#: unknown pattern '%#' (uniform | clustered | slices).", context.c_str(), patternText.c_str() );
                return false;
            }
        }
        const utf8* const arrIntegerKey[3] = { "pieces", "seed", "max_hull_points" };
        uint64            arrInteger[3]    = { settings._pieceCount, settings._seed, settings._maxHullPoint };
        for ( uint32 keyIndex = 0; keyIndex < 3; ++keyIndex )
        {
            if ( fractureValue.has( arrIntegerKey[keyIndex] ) == false )
                continue;
            const JsonValue value = fractureValue.get( arrIntegerKey[keyIndex] );
            if ( value.isNumber() == false || value.asFloat() < 1.0 )
            {
                SW_LOG_ERROR( "%#: %# must be a positive integer.", context.c_str(), arrIntegerKey[keyIndex] );
                return false;
            }
            arrInteger[keyIndex] = static_cast<uint64>( value.asFloat() );
        }
        settings._pieceCount                = static_cast<uint32>( arrInteger[0] );
        settings._seed                      = arrInteger[1];
        settings._maxHullPoint              = static_cast<uint32>( arrInteger[2] );
        const utf8* const arrNumberKey[4]   = { "cluster_radius", "cluster_fraction", "slice_jitter", "interior_uv_scale" };
        float32* const    arrNumberValue[4] = { &settings._clusterRadius, &settings._clusterFraction, &settings._sliceJitter, &settings._interiorUvScale };
        for ( uint32 keyIndex = 0; keyIndex < 4; ++keyIndex )
        {
            if ( fractureValue.has( arrNumberKey[keyIndex] ) == false )
                continue;
            const JsonValue value = fractureValue.get( arrNumberKey[keyIndex] );
            if ( value.isNumber() == false || value.asFloat() < 0.0 )
            {
                SW_LOG_ERROR( "%#: %# must be a non-negative number.", context.c_str(), arrNumberKey[keyIndex] );
                return false;
            }
            *arrNumberValue[keyIndex] = static_cast<float32>( value.asFloat() );
        }
        if ( fractureValue.has( "impact_point" ) && AnimJsonUtil::readFloats( fractureValue.get( "impact_point" ), &settings._impactPoint._x, 3 ) == false )
        {
            SW_LOG_ERROR( "%#: impact_point must be an array of three numbers.", context.c_str() );
            return false;
        }
        if ( fractureValue.has( "interior_color" ) && AnimJsonUtil::readFloats( fractureValue.get( "interior_color" ), &settings._interiorColor._x, 4 ) == false )
        {
            SW_LOG_ERROR( "%#: interior_color must be an array of four numbers.", context.c_str() );
            return false;
        }
        if ( fractureValue.has( "slices" ) )
        {
            float32    arrSlice[3] = { 0.0f, 0.0f, 0.0f };
            const bool bRead       = AnimJsonUtil::readFloats( fractureValue.get( "slices" ), arrSlice, 3 );
            if ( bRead == false || arrSlice[0] < 1.0f || arrSlice[1] < 1.0f || arrSlice[2] < 1.0f )
            {
                SW_LOG_ERROR( "%#: slices must be three positive integers.", context.c_str() );
                return false;
            }
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                settings._arrSliceCount[axis] = static_cast<uint32>( arrSlice[axis] );
            }
        }
        if ( fractureValue.has( "levels" ) )
        {
            const JsonValue levels = fractureValue.get( "levels" );
            const size_t    count  = levels.isArray() ? levels.size() : 0;
            if ( levels.isArray() == false )
            {
                SW_LOG_ERROR( "%#: levels must be an array of positive integers.", context.c_str() );
                return false;
            }
            for ( size_t levelIndex = 0; levelIndex < count; ++levelIndex )
            {
                const JsonValue level = levels.at( levelIndex );
                if ( level.isNumber() == false || level.asFloat() < 1.0 )
                {
                    SW_LOG_ERROR( "%#: levels must be an array of positive integers.", context.c_str() );
                    return false;
                }
                settings._listLevelCount.push_back( static_cast<uint32>( level.asFloat() ) );
            }
        }
        inoutRule._bFracture = SW_TRUE;
        return true;
    }

    ModelImportRule ModelImportConfig::findMatchingRule( string_view relativePath ) const
    {
        for ( const ModelImportRule& rule : _listRule )
        {
            if ( rule._filter.matchesPath( relativePath ) )
                return rule;
        }
        return ModelImportRule{};
    }

    bool ModelImportConfig::parseRecenter( string_view text, ModelRecenter& outRecenter )
    {
        if ( text == "none" )
            outRecenter = ModelRecenter::None;
        else if ( text == "xz" )
            outRecenter = ModelRecenter::Xz;
        else if ( text == "bottom-center" )
            outRecenter = ModelRecenter::BottomCenter;
        else
            return false;
        return true;
    }

    string ModelImportConfig::makeDefaultConfigPath()
    {
        const string projectRoot = EditorUtil::getProjectRootPath();
        if ( projectRoot.empty() )
            return {};
        return FileUtil::joinPath( FileUtil::joinPath( projectRoot, config::kDirConfigEditor ), EditorUtil::kModelImportConfigFileName );
    }
} // namespace sw::editor
