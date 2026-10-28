/**
 * @file EditorToolAssetCommands.h
 * @brief 애니메이션 그래프, 대화 그래프, 타일맵, 스프라이트 클립, 프리팹 오버라이드의 파일 입출력을 담당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Engine/Asset/AssetID.h"

namespace sw
{
    struct TileMapXMLData;

    class AnimationGraphAsset;
    class DialogueGraphAsset;
    class GameObject;
    class SequenceAsset;
    class SpriteClipAsset;
} // namespace sw

namespace sw::editor
{
    /** @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목입니다. */
    struct PrefabOverrideItem
    {
        string _componentName; ///< 화면에 보이는 이름(타입의 표시 이름)이다
        string _componentKey;  ///< 어느 컴포넌트인지 나타내는 키(`ComponentStableKey`). 같은 타입이 둘이어도 구분된다
        string _propertyName;  ///< 프로퍼티 이름. 되돌리기가 찾는 키다
        string _propertyLabel; ///< 화면에 보이는 이름이다. `DisplayName` 메타를 쓰고, 없으면 프로퍼티 이름을 다듬어 쓴다(`EditorDisplayNameUtil`). 인스펙터와 같다
        string _defaultValue;
        string _overriddenValue;
        bool   _bModified{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorToolAssetCommands
     * @brief 도구 패널의 파일 입출력을 ImGui 없이 수행합니다.
     * @details 도구 문서 다섯 종류(애니메이션 그래프, 대화 그래프, 타일맵, 스프라이트 클립, 시퀀스)의 load 와 save 는 템플릿 한 벌을 쓴다
     *          (EditorToolAssetCommands.cpp 의 loadToolDocument, saveToolDocument). 실패는 모두 같은 형식으로 알린다. 읽지 못하면 경고
     *          `Could not read the <종류> '<경로>'` 를, 저장하지 못하면 오류 `Failed to save the <종류> ...` 를 남긴다.
     */
    class EditorToolAssetCommands
    {
    public:
        /** @brief 애니메이션 그래프 JSON 을 읽습니다. path 가 비어 있으면 에디터 설정의 기본 파일을 씁니다. */
        static ToolAssetLoadResult loadAnimationGraph( AnimationGraphAsset& outData, string_view path = {} );
        /** @brief 애니메이션 그래프 JSON 을 씁니다. */
        [[nodiscard]] static bool saveAnimationGraph( const AnimationGraphAsset& data, string_view path = {} );
        /** @brief 대화 그래프 JSON 을 읽습니다. path 가 비어 있으면 기본 대화 파일을 씁니다. */
        static ToolAssetLoadResult loadDialogueGraph( DialogueGraphAsset& outData, string_view path = {} );
        /** @brief 대화 그래프 JSON 을 씁니다. */
        [[nodiscard]] static bool saveDialogueGraph( const DialogueGraphAsset& data, string_view path = {} );
        /** @brief Resource 상대 경로의 TileMap XML 을 읽습니다. 파일이 없으면 `Missing`(새 문서)이고, 있는데 읽지 못하면 `Malformed` 입니다. */
        static ToolAssetLoadResult loadTileMap( string_view assetRelativePath, TileMapXMLData& outData, string& outStatus );
        /** @brief Resource 상대 경로로 TileMap XML 을 씁니다. */
        [[nodiscard]] static bool saveTileMap( string_view assetRelativePath, const TileMapXMLData& data );
        /**
         * @brief SpriteClip JSON 을 읽습니다. path 가 비어 있으면 에디터 설정의 기본 파일을 씁니다. 결과는 `loadTileMap` 과 같은 세 가지입니다.
         *        클립 문서가 아닌 이미지 경로는 문서로 읽지 않고, 그 이미지를 아틀라스로 삼아 `Loaded` 를 반환합니다.
         * @details 형식은 런타임 타입(`SpriteClipAsset`) 하나가 읽고 쓴다. 에디터가 자기 구조체와 파서를 따로 두지 않는다.
         *          문자열 왕복(되돌리기 스냅샷)도 `SpriteClipAsset::toJSON` 과 `parseJSON` 이 한다.
         */
        static ToolAssetLoadResult loadSpriteClip( SpriteClipAsset& outData, string& outStatus, string_view path = {} );
        /** @brief SpriteClip JSON 을 씁니다. */
        [[nodiscard]] static bool saveSpriteClip( const SpriteClipAsset& data, string_view path = {} );
        /** @brief 시퀀서 JSON 을 읽습니다. 결과는 `loadTileMap` 과 같은 세 가지입니다. */
        static ToolAssetLoadResult loadSequence( sw::SequenceAsset& outAsset, string_view path );
        /** @brief 시퀀서 JSON 을 씁니다. */
        [[nodiscard]] static bool saveSequence( const sw::SequenceAsset& asset, string_view path );
        /**
         * @brief 선택한 인스턴스와 프리팹 CDO 를 비교해 오버라이드 목록을 채웁니다.
         * @details 프리팹 경로는 @p prefabPath 이고, 비어 있으면 인스턴스가 온 프리팹이다. 둘 다 없으면 목록이 빈다. 포커스된 에셋은 보지 않는다.
         *          종류가 다를 수 있기 때문이다. Prefab Editor 는 포커스가 프리팹일 때만 경로를 넘긴다.
         */
        static void collectPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
                                            string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
                                            vector<string>& outNestedPrefab );

        /**
         * @brief 인스턴스와 프리팹 기본값(CDO)의 컴포넌트 프로퍼티 차이를 모읍니다.
         * @details 컴포넌트는 안정 키(`타입#n`)로 짝짓고, 값은 `SerializerUtil` 한 벌로 비교하고 기록한다. 타입 이름으로 짝지으면 같은 타입의
         *          두 번째 컴포넌트가 첫 번째의 원형과 비교되고, 비트필드를 바이트째 비교하면 같은 바이트의 다른 플래그까지 오버라이드로 보이므로 주의한다.
         */
        static void collectComponentOverrides( GameObject* pInstance, GameObject* pCdo, vector<PrefabOverrideItem>& outListOverride );
        /** @brief 오버라이드 하나를 템플릿 기본값으로 되돌립니다. 원형(CDO)은 프리팹에서 만듭니다(`revertComponentOverride`). */
        static void revertPrefabOverride( sw::GameObject* pInstance, PrefabOverrideItem& item, string_view prefabPath );
        /**
         * @brief 오버라이드 하나를 원형(CDO)의 값으로 되돌리고 되돌리기 기록을 남깁니다. 값을 옮기지 못하면 false 이고 항목은 그대로입니다.
         * @details 그 프로퍼티만 옮기고(비트필드는 그 비트만, 컨테이너는 원소째) 컴포넌트에 알린다(`onPropertyChanged`). 알리지 않으면
         *          되돌린 값이 화면에 반영되지 않는다.
         */
        [[nodiscard]] static bool revertComponentOverride( GameObject* pInstance, GameObject* pCdo, PrefabOverrideItem& item );
        /** @brief 인스턴스 상태를 프리팹 템플릿에 저장합니다. */
        [[nodiscard]] static bool applyPrefabOverridesToTemplate( sw::GameObject* pInstance, const AssetID& assetID );
        /** @brief 인스턴스를 프리팹 CDO 로 되돌립니다. */
        [[nodiscard]] static bool revertAllPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath );
    };
} // namespace sw::editor
