/**
 * @file EditorAssetTypeID.h
 * @brief 에디터 애셋 종류를 가리키는 값(`EditorAssetType`)과 에디터 전용 종류 id 입니다. 종류 등록 표(`EditorAssetType.h`)와 따로 둔다 —
 *        패널 기반(`IEditorPanel`)처럼 값만 드는 헤더가 등록 줄 · 확장 레지스트리 사슬을 끌어오지 않게.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Engine/Content/AssetTypeRegistry.h"

namespace sw::editor
{
    /**
     * @class EditorAssetType
     * @brief 에디터 애셋 종류 하나를 가리키는 값입니다. 빈 값이 "알 수 없음" 입니다.
     * @details 열쇠는 종류 등록의 id 다. 엔진 포맷이 있는 종류는 엔진 정의의 이름(`AssetTypeDef::_name`)을 id 로 쓰고(`EditorAssetType{ Material::kAssetType }`),
     *          엔진 정의가 없는 에디터 전용 종류(저작 셰이더 · 카탈로그 데이터)만 `EditorAssetTypeIDs` 의 이름을 쓴다.
     *          id 는 글을 복사해 둔다(`fixed_string`). 퀵 런처 색인처럼 오래 사는 값이 종류를 등록한 플러그인이 내려간 뒤에도 그 DLL 의 글을 가리키지 않게 하려는 것이다.
     */
    class EditorAssetType
    {
    public:
        /** @brief id 의 최대 길이입니다. 엔진 종류 이름 중 가장 긴 것(`VertexAnimationCookList`)보다 넉넉하다. */
        static constexpr uint32 kMaxIDLength = 40;

        EditorAssetType() = default;
        /** @brief 엔진 에셋 종류 정의가 가리키는 종류입니다. */
        explicit EditorAssetType( const AssetTypeDef& def )
            : _id{ def._name }
        {
        }
        /** @brief id 로 가리킵니다. 엔진 정의가 없는 종류(`EditorAssetTypeIDs`)와 등록 줄의 id 에 씁니다. */
        explicit EditorAssetType( string_view id )
            : _id{ id }
        {
        }

        /** @brief 어떤 종류도 가리키지 않으면 true 입니다. */
        bool isUnknown() const { return _id.empty(); }
        /** @brief 종류 등록의 id 입니다. */
        string_view getID() const { return _id.view(); }

        bool operator==( const EditorAssetType& other ) const { return _id == other._id; }
        bool operator!=( const EditorAssetType& other ) const { return ( *this == other ) == false; }

    private:
        fixed_string<kMaxIDLength> _id;
    };

    /** @brief 엔진 에셋 종류 정의가 없는 에디터 전용 종류의 id 입니다. 엔진 종류 이름과 겹치지 않습니다(겹치면 등록이 거절된다). */
    struct EditorAssetTypeIDs
    {
        static constexpr const utf8* kShader = "Shader"; ///< 저작 셰이더 소스(`.hlsl` · `.hlsli`). 엔진 종류는 쿠킹된 바이너리(`ShaderBinary`)뿐이다
        static constexpr const utf8* kData   = "Data";   ///< 카탈로그 데이터(`.xml` · `.json`)와 전용 종류가 없는 데이터 문서. 전용 창이 접미사로 고르는 문서(`canAcceptDocumentPath`)가 여기 든다
    };
} // namespace sw::editor
