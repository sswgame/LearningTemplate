/**
 * @file EditorToolAssetCommands.h
 * @brief 애니메이션/대화 그래프, 타일맵, 스프라이트 클립, 프리팹 오버라이드 파일 IO
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct TileMapXmlData;

    class AnimationGraphAsset;
    class DialogueGraphAsset;
    class GameObject;
    class SequenceAsset;
} // namespace sw

namespace sw::editor
{
    /** @brief 스프라이트 클립 프레임 */
    struct EditorSpriteClipFrame
    {
        float32 _u{ 0.0f };
        float32 _v{ 0.0f };
        float32 _w{ 1.0f };
        float32 _h{ 1.0f };
        int32   _durationMs{ 100 };
    };

    /** @brief 스프라이트 클립 트랜스폼 키 */
    struct EditorSpriteClipKey
    {
        float32 _time{ 0.0f };
        float32 _x{ 0.0f };
        float32 _y{ 0.0f };
        float32 _angleDeg{ 0.0f };
    };

    /** @brief SpriteClip.json 데이터 */
    struct EditorSpriteClipData
    {
        string                        _atlasPath;
        vector<EditorSpriteClipFrame> _listFrame;
        vector<EditorSpriteClipKey>   _listKey;
    };

    /** @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목 */
    struct PrefabOverrideItem
    {
        string _componentName;
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
        static bool loadAnimationGraph( AnimationGraphAsset& outData, string_view path = {} );
        /** @brief 애니메이션 그래프 JSON을 씁니다. */
        static bool saveAnimationGraph( const AnimationGraphAsset& data, string_view path = {} );
        /** @brief 대화 그래프 JSON을 읽습니다. path가 비면 기본 대화 파일을 씁니다. */
        static bool loadDialogueGraph( DialogueGraphAsset& outData, string_view path = {} );
        /** @brief 대화 그래프 JSON을 씁니다. */
        static bool saveDialogueGraph( const DialogueGraphAsset& data, string_view path = {} );
        /** @brief Resource 상대 경로의 TileMap XML을 읽습니다. */
        static bool loadTileMap( string_view assetRelativePath, TileMapXmlData& outData, string& outStatus );
        /** @brief Resource 상대 경로로 TileMap XML을 씁니다. */
        static bool saveTileMap( string_view assetRelativePath, const TileMapXmlData& data );
        /** @brief SpriteClip JSON을 읽습니다. path가 비면 에디터 설정 기본 파일을 씁니다. */
        static bool loadSpriteClip( EditorSpriteClipData& outData, string& outStatus, string_view path = {} );
        /** @brief SpriteClip JSON을 씁니다. */
        static bool saveSpriteClip( const EditorSpriteClipData& data, string_view path = {} );
        /** @brief SpriteClip을 JSON 문자열로 직렬화합니다. */
        static string serializeSpriteClip( const EditorSpriteClipData& data );
        /** @brief JSON 문자열을 SpriteClip으로 파싱합니다. */
        static bool parseSpriteClip( string_view json, EditorSpriteClipData& outData );
        /** @brief 시퀀서 JSON을 읽습니다. */
        static bool loadSequence( sw::SequenceAsset& outAsset, string_view path );
        /** @brief 시퀀서 JSON을 씁니다. */
        static bool saveSequence( const sw::SequenceAsset& asset, string_view path );
        /** @brief 선택 인스턴스와 프리팹 CDO를 비교해 오버라이드 목록을 채웁니다. */
        static void collectPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
                                            string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
                                            vector<string>& outNestedPrefab );
        /** @brief 한 오버라이드를 인스턴스에 템플릿 기본값으로 되돌립니다. */
        static void revertPrefabOverride( sw::GameObject* pInstance, PrefabOverrideItem& item, string_view prefabPath );
        /** @brief 인스턴스 상태를 프리팹 템플릿에 저장합니다. */
        static bool applyPrefabOverridesToTemplate( sw::GameObject* pInstance, string_view prefabPath );
        /** @brief 인스턴스를 프리팹 CDO로 되돌립니다. */
        static bool revertAllPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath );
    };
} // namespace sw::editor
