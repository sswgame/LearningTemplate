/**
 * @file EditorToolAssetCommands.h
 * @brief 애니메이션/대화 그래프, 타일맵, 스프라이트 클립, 프리팹 오버라이드 파일 IO
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw
{
    struct TileMapXmlData;

    class AnimationGraphAsset;
    class DialogueGraphAsset;
    class GameObject;
    class SequenceAsset;
    class SpriteClipAsset;
} // namespace sw

namespace sw::editor
{
    /** @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목 */
    struct PrefabOverrideItem
    {
        string _componentName; ///< 보이는 이름(타입)입니다
        string _componentKey;  ///< 어느 컴포넌트인지(`ComponentStableKey`). 같은 타입이 둘이어도 갈립니다
        string _propertyName;
        string _defaultValue;
        string _overriddenValue;
        bool   _bModified{ false };
    };

    /**
     * @class EditorToolAssetCommands
     * @brief 도구 패널이 쓰던 파일 IO를 ImGui 없이 수행합니다.
     */
    class EditorToolAssetCommands
    {
    public:
        /** @brief 애니메이션 그래프 JSON을 읽습니다. path가 비면 에디터 설정 기본 파일을 씁니다. */
        static ToolAssetLoadResult loadAnimationGraph( AnimationGraphAsset& outData, string_view path = {} );
        /** @brief 애니메이션 그래프 JSON을 씁니다. */
        [[nodiscard]] static bool saveAnimationGraph( const AnimationGraphAsset& data, string_view path = {} );
        /** @brief 대화 그래프 JSON을 읽습니다. path가 비면 기본 대화 파일을 씁니다. */
        static ToolAssetLoadResult loadDialogueGraph( DialogueGraphAsset& outData, string_view path = {} );
        /** @brief 대화 그래프 JSON을 씁니다. */
        [[nodiscard]] static bool saveDialogueGraph( const DialogueGraphAsset& data, string_view path = {} );
        /** @brief Resource 상대 경로의 TileMap XML을 읽습니다. 파일이 없으면 `Missing`(새 문서), 있는데 못 읽으면 `Malformed` 입니다. */
        static ToolAssetLoadResult loadTileMap( string_view assetRelativePath, TileMapXmlData& outData, string& outStatus );
        /** @brief Resource 상대 경로로 TileMap XML을 씁니다. */
        [[nodiscard]] static bool saveTileMap( string_view assetRelativePath, const TileMapXmlData& data );
        /**
         * @brief SpriteClip JSON을 읽습니다. path가 비면 에디터 설정 기본 파일을 씁니다. 결과는 `loadTileMap` 과 같은 세 갈래입니다.
         * @details 형식은 런타임 타입(`SpriteClipAsset`) 하나가 읽고 씁니다. 예전에는 에디터가 자기 구조체(`EditorSpriteClipData`)와 파서를 따로
         *          들어 런타임에는 그 파일을 읽는 곳이 없었습니다. 문자열 왕복(되돌리기 스냅샷)도 `SpriteClipAsset::toJson` · `parseJson` 입니다.
         */
        static ToolAssetLoadResult loadSpriteClip( SpriteClipAsset& outData, string& outStatus, string_view path = {} );
        /** @brief SpriteClip JSON을 씁니다. */
        [[nodiscard]] static bool saveSpriteClip( const SpriteClipAsset& data, string_view path = {} );
        /** @brief 시퀀서 JSON을 읽습니다. 결과는 `loadTileMap` 과 같은 세 갈래입니다. */
        static ToolAssetLoadResult loadSequence( sw::SequenceAsset& outAsset, string_view path );
        /** @brief 시퀀서 JSON을 씁니다. */
        [[nodiscard]] static bool saveSequence( const sw::SequenceAsset& asset, string_view path );
        /** @brief 선택 인스턴스와 프리팹 CDO를 비교해 오버라이드 목록을 채웁니다. */
        static void collectPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
                                            string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
                                            vector<string>& outNestedPrefab );

        /**
         * @brief 인스턴스와 프리팹 기본값(CDO)의 컴포넌트 프로퍼티 차이를 모읍니다.
         * @details 컴포넌트는 안정 키(`타입#n`)로 짝짓고, 값은 `SerializerUtil` 한 벌로 견주고 적습니다. 예전에는 타입 이름으로 짝지어 같은 타입의
         *          둘째 컴포넌트가 첫째의 원형과 비교됐고, 비트필드를 바이트째 견줘 같은 바이트의 다른 플래그까지 오버라이드로 보였다.
         */
        static void collectComponentOverrides( GameObject* pInstance, GameObject* pCdo, vector<PrefabOverrideItem>& outListOverride );
        /** @brief 오버라이드 하나를 템플릿 기본값으로 되돌립니다. 원형(CDO)은 프리팹에서 만듭니다(`revertComponentOverride`). */
        static void revertPrefabOverride( sw::GameObject* pInstance, PrefabOverrideItem& item, string_view prefabPath );
        /**
         * @brief 오버라이드 하나를 원형(CDO)의 값으로 되돌리고 되돌리기 기록을 남깁니다. 옮기지 못하면 false 이고 항목은 그대로입니다.
         * @details 그 프로퍼티만 옮기고(비트필드는 그 비트만, 컨테이너는 원소째) 컴포넌트에 알립니다(`onPropertyChanged`). 예전에는 타입 이름의
         *          첫 컴포넌트에, 값 경로로만 옮겨 컨테이너는 되돌리지 못했고 비트필드는 바이트째 덮었으며, 알리지 않아 위치가 화면에 들지 않았다.
         */
        [[nodiscard]] static bool revertComponentOverride( GameObject* pInstance, GameObject* pCdo, PrefabOverrideItem& item );
        /** @brief 인스턴스 상태를 프리팹 템플릿에 저장합니다. */
        [[nodiscard]] static bool applyPrefabOverridesToTemplate( sw::GameObject* pInstance, string_view prefabPath );
        /** @brief 인스턴스를 프리팹 CDO로 되돌립니다. */
        [[nodiscard]] static bool revertAllPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath );
    };
} // namespace sw::editor
