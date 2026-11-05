/**
 * @file AudioEventEditModel.h
 * @brief 오디오 이벤트 창의 "고치기" 상태입니다 — 파일에서 읽은 라이브러리 사본, 고치는 이벤트, 저장하지 않은 표시.
 * @details 엔진 설명자는 오디오 스레드가 참조하므로 사본을 고치고 저장할 때 파일로 쓴다. ImGui 없이 값만 다룬다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioEvent.h"

namespace sw::editor
{
    /**
     * @class AudioEventEditModel
     * @brief 라이브러리 사본 하나를 고치는 세션입니다. 라이브러리 이름이 비어 있으면 고치는 중이 아닙니다.
     */
    class AudioEventEditModel
    {
    public:
        AudioEventEditModel();

        /** @brief @p libraryName 파일을 읽어 고치기 시작합니다. 읽지 못하면 고치기를 끝내고 false 입니다. */
        bool begin( const hashed_string& libraryName, const hashed_string& eventName );
        /** @brief 사본을 버리고 고치기를 끝냅니다. */
        void end();
        /** @brief 사본을 파일에 씁니다. 검사를 통과하지 못하면 쓰지 않고 표시도 그대로입니다. */
        [[nodiscard]] bool save();

        bool isDirty() const { return _bDirty == SW_TRUE; }
        void markEdited() { _bDirty = SW_TRUE; }
        /** @brief @p libraryName 을 고치는 중이면 true 입니다. */
        bool isEditing( const hashed_string& libraryName ) const { return _libraryName.empty() == false && _libraryName == libraryName; }
        bool isEditingAny() const { return _libraryName.empty() == false; }

        AudioEventLibrary&       editLibrary() { return _library; }
        const AudioEventLibrary& getLibrary() const { return _library; }
        const hashed_string&     getEventName() const { return _eventName; }
        void                     selectEvent( const hashed_string& eventName ) { _eventName = eventName; }
        /** @brief 고치는 이벤트입니다. 지웠거나 없으면 nullptr 입니다. */
        AudioEventDesc* findEvent() { return _library.findEvent( _eventName ); }

        /** @brief 고치는 이벤트를 복제하고 사본을 고릅니다. */
        void duplicateEvent();
        /** @brief 고치는 이벤트를 지우고 첫 이벤트를 고릅니다. 없는 이벤트면 false 입니다. */
        bool removeEvent();

    private:
        AudioEventLibrary _library;
        hashed_string     _libraryName; ///< `_library` 를 읽은 리소스
        hashed_string     _eventName;   ///< 사본에서 고치는 이벤트
        uint8             _bDirty : 1;  ///< 사본을 고친 뒤 아직 쓰지 않았다
    };
} // namespace sw::editor
