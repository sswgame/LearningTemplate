/**
 * @file AssetGUID.h
 * @brief 에셋의 정체성(무작위 128비트 GUID)과 그 글 표기 · 파일 속 자리 규칙입니다.
 * @details 경로는 옮기면 바뀐다 — 경로를 id 로 쓰면 옮길 때마다 리다이렉트 줄이 영구히 쌓인다. GUID 는 에셋이 처음 생길 때 한 번 만들고
 *          내용이 바뀌어도 · 옮겨도 그대로다. 내용이 바뀌었는지는 따로 내용 해시(`ContentHash`)가 말한다. 유니티 `.meta` 의 guid, Godot 4 의 `uid://` 와 같은 자리다.
 *          - **빈 값이 없다.** 기본 생성자가 없고 만드는 길은 `create`(새 정체성) · `tryParse`(적힌 정체성) · `fromName`(이름에서 만든 결정적 정체성 — 임포트 산출물 · 내용 주소 파일) 셋뿐이다 — 없음은 `optional<AssetGUID>` 로 적는다.
 *          - **글 표기는 하나다.** 소문자 16진 32 자(하이픈 없음, 유니티 표기). 대문자 · 하이픈 · 모두 0 은 받지 않는다 — 같은 GUID 를 두 철자로 적으면 글로 대조하는 린트 · 색인이 갈린다.
 *          - **자리.** 우리 형식 XML 에셋은 파일 머리의 처리 지시 `<?asset guid="…"?>`(XML 선언 바로 뒤)에, 원본(png · ogg · glb …)은 곁의 `.meta` 서술(`AssetMeta`)에 둔다.
 *            루트 속성으로 두지 않는 까닭: 종류마다 로더가 받는 루트 속성을 따로 적고(70 여 종류를 모두 고쳐야 한다), 데이터 상속(`_base` 합치기)이 부모의 루트 속성을 자식에게 베껴
 *            두 에셋이 한 정체성을 갖게 된다. 처리 지시는 파서가 문서에 올리지 않으므로 로더 · 합치기가 보지 않는다(Godot 4 도 uid 를 리소스 칸이 아니라 파일 머리 줄에 둔다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class AssetGUID
     * @brief 에셋 하나의 정체성입니다. 파일 머리말 참고.
     */
    class SW_API AssetGUID
    {
    public:
        /** @brief 글 표기의 길이(소문자 16진 32 자)입니다. */
        static constexpr size_t kTextLength = 32;

        /** @brief 새 정체성을 만듭니다(운영체제 난수 128 비트). 손으로 만든 에셋의 정체성을 만드는 길은 이것 하나다. */
        [[nodiscard]] static AssetGUID create();
        /**
         * @brief 이름 @p name 에서 결정적으로 만든 정체성입니다(UUID v5 처럼 이름 기반 — 같은 이름은 늘 같은 GUID). 대소문자는 가리지 않는다(리소스 id 색인과 같다).
         * @details 내용 주소 파일(`textures_raw/_shared/` — 경로가 곧 내용이라 두 사람이 따로 임포트해도 같은 파일이다)만 쓴다. 손으로 만든 에셋은 `create` 다(이름을 바꿔도 정체성이 그대로여야 한다).
         */
        [[nodiscard]] static AssetGUID fromName( string_view name );
        /**
         * @brief 정체성 @p nameSpace 안의 이름 @p name 에서 결정적으로 만든 정체성입니다(UUID v5 의 이름공간 + 이름).
         * @details 커밋하지 않는 임포트 산출물의 정체성이다 — 원본 GUID 와 산출물 이름(`.mesh` · `/materials/mi_eyes.material`)에서 나오므로 다시 임포트해도 · 다른 컴퓨터에서도 · 원본을 옮겨도 같다.
         *          유니티가 하위 에셋을 (원본 guid, 이름에서 만든 fileID)로 가리키는 것을 GUID 하나로 접은 것이다. 파이썬 도구(`asset_identity.py` 의 `make_output_guid`)가 같은 규칙이다.
         */
        [[nodiscard]] static AssetGUID fromName( const AssetGUID& nameSpace, string_view name );
        /** @brief 글 표기 @p text 를 읽습니다. 소문자 16진 32 자가 아니거나 모두 0 이면 없음입니다(대소문자 · 하이픈을 고쳐 읽지 않는다). */
        [[nodiscard]] static optional<AssetGUID> tryParse( string_view text );

        /** @brief 글 표기(소문자 16진 32 자)입니다. */
        [[nodiscard]] string toString() const;
        /** @brief 해시 키(`DefaultHash`)입니다. 무작위 값이라 두 반쪽을 섞기만 한다. */
        [[nodiscard]] uint64 computeHash() const { return _high ^ _low; }

        [[nodiscard]] bool operator==( const AssetGUID& other ) const { return _high == other._high && _low == other._low; }
        [[nodiscard]] bool operator!=( const AssetGUID& other ) const { return ( *this == other ) == false; }
        /** @brief 정렬 순서(앞 64 비트 먼저)입니다 — 색인을 늘 같은 순서로 쓴다. */
        [[nodiscard]] bool operator<( const AssetGUID& other ) const { return _high != other._high ? _high < other._high : _low < other._low; }

    private:
        AssetGUID( uint64 high, uint64 low )
            : _high{ high }
            , _low{ low }
        {
        }
        /** @brief 이름 해시 두 개(섞기 전)로 만든 정체성입니다 — 모두 0(글 표기가 받지 않는다)은 정해진 값으로 비켜 결정성을 지킨다. */
        static AssetGUID fromNameHash( uint64 highHash, uint64 lowHash );

    private:
        uint64 _high;
        uint64 _low;
    };

    /** @brief 우리 형식 XML 글에서 GUID 머리를 찾은 결과입니다. */
    enum class [[nodiscard]] AssetGUIDHeaderState : uint8
    {
        Found,   ///< 머리가 있고 GUID 를 읽었다
        Missing, ///< 머리가 없다(아직 정체성을 받지 않은 파일)
        Invalid, ///< 머리 자리에 `<?asset …?>` 가 있는데 읽지 못했다(꼴이 틀렸다 · GUID 가 글 표기가 아니다)
    };

    /**
     * @struct AssetGUIDHeader
     * @brief 우리 형식 XML 글의 GUID 머리(`<?asset guid="…"?>`)를 찾은 결과와, 그 머리의 읽기 · 쓰기입니다. 글만 다루고 파일을 열지 않는다.
     * @details 자리는 하나다 — 맨 앞(BOM · XML 선언 · 공백 뒤)의 첫 처리 지시. 그 뒤의 글은 보지 않는다(주석이 머리 모양을 적어도 된다 — 파서는 처리 지시를 문서에 올리지 않는다).
     *          파이썬 도구(`Scripts/lint/asset_identity.py`)가 같은 규칙으로 읽고 쓴다.
     */
    struct SW_API AssetGUIDHeader
    {
        AssetGUIDHeaderState _state{ AssetGUIDHeaderState::Missing };
        optional<AssetGUID>  _guid; ///< `Found` 일 때만 찬다

        /** @brief @p xmlText 의 GUID 머리를 찾습니다. */
        static AssetGUIDHeader read( string_view xmlText );
        /** @brief @p xmlText 에 @p guid 머리를 쓴 글입니다. 이미 머리가 있으면 그 자리를 바꾸고, 없으면 XML 선언 뒤(없으면 맨 앞)에 한 줄 넣는다. 줄 끝은 글의 것(CRLF · LF)을 따른다. */
        static string write( string_view xmlText, const AssetGUID& guid );
        /**
         * @brief 다시 쓴 글 @p newText 에 머리가 없고 있던 글 @p previousText 에 머리가 있으면 그 머리를 옮겨 쓴 글, 아니면 @p newText 그대로입니다.
         * @details 파서는 처리 지시를 문서에 올리지 않으므로 문서를 고쳐 통째로 다시 쓰는 저장은 머리를 잃는다 — 저장 입구(`ResourceComposer::saveText`)와
         *          생성기(머티리얼 그래프 산출물)가 이것을 지나 고쳐도 정체성이 그대로다.
         */
        static string carryOver( string_view previousText, string_view newText );
    };
} // namespace sw
