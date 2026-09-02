/**
 * @file IEditorPopup.h
 * @brief 모달 및 비도킹 플로팅 에디터 팝업 인터페이스 (Common/Gui 계층)
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @class IEditorPopup
     * @brief 모달 및 플로팅 팝업 윈도우의 추상 기본 인터페이스
     */
    class IEditorPopup
    {
    public:
        virtual ~IEditorPopup() = default;

        /** @brief 팝업의 고유 식별자(ID)를 반환합니다. */
        virtual const utf8* getPopupId() const = 0;

        /** @brief 팝업의 표시 타이틀을 반환합니다. 기본값은 ID와 동일합니다. */
        virtual const utf8* getPopupTitle() const { return getPopupId(); }

        /** @brief 팝업이 열려 있는지 여부를 반환합니다. */
        bool isOpen() const { return _bOpen; }

        /** @brief 팝업을 엽니다. */
        void open()
        {
            if ( _bOpen == false )
            {
                _bOpen = true;
                onOpen();
            }
        }

        /** @brief 팝업을 닫습니다. */
        void close()
        {
            if ( _bOpen )
            {
                _bOpen = false;
                onClose();
            }
        }

        /** @brief 팝업 열림/닫힘 상태를 토글합니다. */
        void toggle()
        {
            if ( _bOpen )
                close();
            else
                open();
        }

        /** @brief 팝업 열림 상태를 직접 설정합니다. */
        void setOpen( bool bOpen )
        {
            if ( bOpen )
                open();
            else
                close();
        }

        /** @brief ImGui에서 사용할 열림 상태 포인터를 반환합니다. */
        bool* getOpenPtr() { return &_bOpen; }

        /** @brief 팝업 렌더링 진입점입니다. (열려 있는 경우 drawContent 호출) */
        void draw()
        {
            if ( _bOpen )
                drawContent();
        }

    protected:
        explicit IEditorPopup( bool bOpenByDefault = false )
            : _bOpen{ bOpenByDefault }
        {
        }

        /** @brief 팝업 본문을 렌더링합니다. 파생 클래스에서 구현합니다. */
        virtual void drawContent() = 0;

        /** @brief 팝업이 열릴 때 호출되는 가상 메서드 */
        virtual void onOpen() {}

        /** @brief 팝업이 닫힐 때 호출되는 가상 메서드 */
        virtual void onClose() {}

    protected:
        bool _bOpen;
    };
} // namespace sw::editor
