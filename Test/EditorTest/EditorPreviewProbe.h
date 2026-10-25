/**
 * @file EditorPreviewProbe.h
 * @brief 뷰포트 미리보기가 메서드 메타(`EditorPreview`)로 컴포넌트를 찾는지 보는 테스트용 컴포넌트 둘입니다(코드젠을 탑니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneObjectReference.h"

namespace sw::editortest
{
    /** @brief 대사 미리보기 메타를 단 컴포넌트입니다. 받은 대사를 적어 둡니다. */
    REFLECT()
    class EditorPreviewProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        FUNCTION( EditorPreview = "DialogueLine" )
        void showPreviewLine( string speaker, string text )
        {
            _lastSpeaker = std::move( speaker );
            _lastText    = std::move( text );
            ++_previewCount;
        }

        string _lastSpeaker;
        string _lastText;
        int32  _previewCount = 0;
    };
} // namespace sw::editortest

namespace sw::editortest
{
    /** @brief 이름과 메서드는 GameFramework 의 대사 러너와 같고 메타는 없는 컴포넌트입니다. 미리보기가 골라서는 안 됩니다. */
    REFLECT()
    class DialogueRunnerComponent : public Component
    {
    public:
        REFLECT_BODY();

        FUNCTION()
        void previewLine( string speaker, string text )
        {
            (void)speaker;
            (void)text;
            ++_previewCount;
        }

        int32 _previewCount = 0;
    };
} // namespace sw::editortest

namespace sw::editortest
{
    /** @brief 씬 파일을 넘는 오브젝트 참조 칸 하나를 가진 컴포넌트입니다(프리팹 만들기가 참조된 오브젝트를 옮기지 않는지 보는 테스트용). */
    REFLECT()
    class SceneReferenceHolderProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        SceneObjectReference _target;
    };
} // namespace sw::editortest

namespace sw::editortest
{
    /** @brief 씬 오브젝트 참조 목록 칸(`vector<SceneObjectReference>`) 하나를 가진 컴포넌트입니다(컨테이너 칸도 참조로 세는지 보는 테스트용). */
    REFLECT()
    class SceneReferenceListHolderProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        vector<SceneObjectReference> _listTarget;
    };
} // namespace sw::editortest

namespace sw::editortest
{
    /** @brief 참조 칸 하나를 품은 구조체입니다(구조체 안의 칸도 참조로 세는지 보는 테스트용). */
    REFLECT()
    struct SceneReferenceBoxProbe
    {
        REFLECT_BODY();

        PROPERTY()
        SceneObjectReference _inner;
    };
} // namespace sw::editortest

namespace sw::editortest
{
    /** @brief 참조 칸을 품은 구조체 칸 하나를 가진 컴포넌트입니다. */
    REFLECT()
    class SceneReferenceStructHolderProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        SceneReferenceBoxProbe _box;
    };
} // namespace sw::editortest

namespace sw::editortest
{
    /** @brief 환경설정 저장소 테스트용 섹션입니다(EditorPreferencesStoreTest). */
    REFLECT()
    struct EditorPreferencesProbe
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _speed{ 5.0f };

        PROPERTY()
        string _path{};

        PROPERTY()
        bool _bFlag{ true };
    };
} // namespace sw::editortest
