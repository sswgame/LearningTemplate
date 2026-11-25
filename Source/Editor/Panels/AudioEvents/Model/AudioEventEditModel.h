/**
 * @file AudioEventEditModel.h
 * @brief 오디오 이벤트 창의 "고치기" 상태입니다 — 파일에서 읽은 라이브러리 사본, 고치는 이벤트, 저장하지 않은 표시.
 * @details 엔진 설명자는 오디오 스레드가 참조하므로 사본을 고치고 저장할 때 파일로 쓴다. ImGui 없이 값만 다룬다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Audio/AudioEvent.h"

namespace sw::editor
{
    class DocumentEditRecorder;

    /**
     * @class AudioEventEditModel
     * @brief 라이브러리 사본 하나를 고치는 세션입니다. 라이브러리 이름이 비어 있으면 고치는 중이 아닙니다.
     */
    class AudioEventEditModel
    {
    public:
        /** @param recorder 편집 알림 창구입니다(문서 패널이 쥔다 — `EditorDocumentPanel::getDocumentEditRecorder`). 고친 뒤 이리 알려 되돌리기 · 미저장이 된다. */
        explicit AudioEventEditModel( const DocumentEditRecorder& recorder );

        /** @brief @p libraryName 파일을 읽어 고치기 시작합니다. 읽지 못하면 고치기를 끝내고 false 입니다. */
        bool begin( const hashed_string& libraryName, const hashed_string& eventName );
        /** @brief 사본을 버리고 고치기를 끝냅니다. */
        void end();
        /** @brief 사본을 파일에 씁니다. 검사를 통과하지 못하면 쓰지 않고 false 입니다(미저장 표시는 문서 패널이 든다). */
        [[nodiscard]] bool saveDocument();

        /** @brief 속성 그리드가 사본을 고쳤습니다 — "Edit Audio Event" 로 알립니다(끄는 동안은 상호작용 구간 하나, 이름은 모델이 정한다 — 감사 E-16). */
        void markPropertyEdited() const { recordDocumentEdit( "Edit Audio Event" ); }
        /** @brief @p libraryName 을 고치는 중이면 true 입니다. */
        bool isEditing( const hashed_string& libraryName ) const { return _libraryName.empty() == false && _libraryName == libraryName; }
        bool isEditingAny() const { return _libraryName.empty() == false; }

        AudioEventLibrary&       editLibrary() { return _library; }
        const AudioEventLibrary& getLibrary() const { return _library; }
        const hashed_string&     getEventName() const { return _eventName; }
        void                     selectEvent( const hashed_string& eventName ) { _eventName = eventName; }
        /** @brief 고치는 이벤트입니다. 지웠거나 없으면 nullptr 입니다. */
        AudioEventDef* findEvent() { return _library.findEvent( _eventName ); }

        /** @brief 고치는 이벤트를 복제하고 사본을 고릅니다. */
        void duplicateEvent();
        /** @brief 고치는 이벤트를 지우고 첫 이벤트를 고릅니다. 없는 이벤트면 false 입니다. */
        bool removeEvent();

        /** @brief 사본 라이브러리를 저장과 같은 XML 글로 씁니다(문서 되돌리기의 글). 고치는 중이 아니면 빈 글입니다. */
        string captureText() const;
        /** @brief 되돌리기가 준 글로 사본을 다시 읽습니다. 고치던 이벤트가 사라졌으면 첫 이벤트를 고릅니다. 고치는 중이 아니거나 읽지 못하면 false 입니다. */
        bool applyText( string_view text );

    private:
        /** @brief 문서를 고쳤음을 알립니다(이 모델의 편집 연산만 부른다 — 감사 E-16). */
        void recordDocumentEdit( string_view label ) const;

    private:
        const DocumentEditRecorder* _pRecorder; ///< 편집 알림 창구(문서 패널이 쥔다)
        AudioEventLibrary           _library;
        hashed_string               _libraryName; ///< `_library` 를 읽은 리소스
        hashed_string               _eventName;   ///< 사본에서 고치는 이벤트
    };
} // namespace sw::editor
