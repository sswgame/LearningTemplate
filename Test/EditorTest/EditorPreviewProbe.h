/**
 * @file EditorPreviewProbe.h
 * @brief 뷰포트 미리보기가 메서드 메타(`EditorPreview`)로 컴포넌트를 찾는지 보는 시험용 컴포넌트 둘입니다(코드젠을 탑니다).
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

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
