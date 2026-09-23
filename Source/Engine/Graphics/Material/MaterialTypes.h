/**
 * @file MaterialTypes.h
 * @brief Material · MaterialInstance 가 함께 쓰는 타입과 서술체입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /// @brief 머티리얼 프로퍼티 타입 종류입니다.
    enum class MaterialPropertyType : uint8
    {
        Float,
        Float2,
        Float3,
        Float4,
        Float4x4,
        Uint,
        Uint2,
        Uint3,
        Uint4,
        Int,
        Int2,
        Int3,
        Int4,
        Bool,
        Range,
        Color,
        Enum,
        BitFlag,
        ChannelMask,
        Texture2D,
        TextureCube,
        Texture3D,
        Texture2DArray,
        Keyword,
        Unknown
    };

    /// @brief 머티리얼 품질 레벨 종류입니다.
    ENUM( Count = Count, ValueAlias = "Med:Medium" )
    enum class MaterialQualityLevel : uint8
    {
        Low    = 0,
        Medium = 1,
        High   = 2,
        Epic   = 3,
        Count
    };

    /// @brief 머티리얼 사용 플래그 종류입니다.
    ENUM( Flags )
    enum class MaterialUsageFlags : uint16
    {
        None          = 0,
        StaticMesh    = SW_BIT( 0 ),
        SkeletalMesh  = SW_BIT( 1 ),
        Instanced     = SW_BIT( 2 ),
        Particles     = SW_BIT( 3 ),
        Decal         = SW_BIT( 4 ),
        UI            = SW_BIT( 5 ),
        PostProcess   = SW_BIT( 6 ),
        LightFunction = SW_BIT( 7 ),
        MorphTargets  = SW_BIT( 8 ),
        SplineMesh    = SW_BIT( 9 ),
    };

    /// @brief 머티리얼 enum 항목(이름 + 값)입니다.
    struct MaterialEnumEntry
    {
        string _name;
        uint32 _value{ 0 };
    };

    /// @brief 머티리얼 프로퍼티입니다(타입, 기본값, 패킹 오프셋).
    struct MaterialProperty
    {
        string               _name;
        MaterialPropertyType _type;
        MaterialPropertyType _shaderType;

        /** @brief 작성 기본값입니다(XML `_defaultValue`. 로드할 때 `_value` 로 폴백합니다). */
        string _defaultValue;
        /** @brief 현재 패킹 값입니다(처음에는 `_defaultValue`). */
        string _value;

        string                    _assetPath;
        string                    _enumType;
        vector<MaterialEnumEntry> _listEnumEntry;

        string _displayName;
        string _group;
        string _tooltip;
        string _shaderKeyword;

        float32            _min;
        float32            _max;
        uint32             _offset;
        uint32             _size;
        RHIDescriptorIndex _textureIndex;

        uint8 _bHdr      : 1;
        uint8 _bSrgb     : 1;
        uint8 _bHidden   : 1;
        uint8 _bAdvanced : 1;
        uint8 _reserved  : 4;

        /** @brief 플래그 · 범위를 끈 기본값으로 만듭니다. */
        MaterialProperty() noexcept;
    };

    /// @brief 정적 스위치입니다(켜면 셰이더 define).
    struct MaterialStaticSwitch
    {
        string _name;
        string _keyword;
        string _keywordOff;

        uint8 _bEnabled       : 1;
        uint8 _bShaderFeature : 1;
        uint8 _reserved       : 6;

        /** @brief 꺼진 스위치로 만듭니다. */
        MaterialStaticSwitch() noexcept;
    };

    /// @brief 멀티 컴파일 키워드 목록입니다.
    struct MaterialMultiCompile
    {
        string         _name;
        string         _selected;
        vector<string> _listOption;
    };

    /// @brief 스위치 + 멀티 컴파일 + 품질 퍼뮤테이션입니다.
    struct MaterialPermutationDesc
    {
        MaterialQualityLevel         _quality   = MaterialQualityLevel::High;
        uint32                       _shaderLOD = 300;
        MaterialUsageFlags           _usage     = MaterialUsageFlags::StaticMesh;
        vector<string>               _listAlwaysDefine;
        vector<MaterialStaticSwitch> _listStaticSwitch;
        vector<MaterialMultiCompile> _listMultiCompile;
    };

    /// @brief 머티리얼 에셋 서술입니다(셰이더 경로, 프로퍼티, 퍼뮤테이션).
    struct MaterialDesc
    {
        /** @brief .material 리소스에서 채웁니다(팩별 C++ 기본값 없음). */
        string                   _name;
        string                   _shaderPath;
        string                   _blendMode;
        vector<MaterialProperty> _listProperty;
        MaterialPermutationDesc  _permutations;
    };

    /** @brief 직렬화되는 인스턴스 오버라이드입니다(게임 MIC / MaterialPropertyBlock). */
    struct MaterialInstanceDesc
    {
        string _name;
        string _parentPath; ///< 선택적 힌트. 런타임에는 여전히 Material* 부모가 필요함

        /// @brief 인스턴스가 덮어쓴 프로퍼티 값입니다.
        struct Override
        {
            string _name;
            string _value;
            string _assetPath; ///< 텍스처 오버라이드 경로(선택)
        };

        vector<Override> _listOverride;

        /// @brief 인스턴스가 덮어쓴 정적 스위치입니다.
        struct KeywordOverride
        {
            string _name;
            bool   _bEnabled{ true };
        };

        vector<KeywordOverride> _listKeyword;

        /// @brief 인스턴스가 고른 멀티 컴파일 옵션입니다.
        struct MultiCompileOverride
        {
            string _name;
            string _selected;
        };

        vector<MultiCompileOverride> _listMultiCompile;

        string _quality; ///< 비어 있으면 부모를 따름
    };

    /// @brief 런타임 머티리얼 데이터입니다(패킹 버퍼 + 디스크립터).
    struct MaterialData
    {
        vector<MaterialProperty> _listProperty;
        vector<uint8>            _bytes;
    };
} // namespace sw
