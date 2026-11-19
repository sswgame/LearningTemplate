/**
 * @file EditorHandleDrag.h
 * @brief 뷰포트 손잡이 끌기의 명령과 그 순서(잡기 → 끌기들 → 놓기)입니다 — 콜라이더 크기 손잡이 · 선언형 모양 손잡이가 같은 틀을 쓴다(ImGui · 씬 없음).
 * @details 손잡이 값 형식(`THandle`)만 다르고 순서 규칙은 하나다: 누르면 잡기(되돌리기의 "전"), 끄는 동안 마우스 레이마다 끌기, 단추를 놓으면 커밋하는 놓기,
 *          대상이 사라지거나 편집이 막히면 커밋 없는 놓기. 언리얼 컴포넌트 비주얼라이저의 `HandleInputDelta` 앞뒤 트랜잭션 · 유니티 `Handles` 의 GUIUtility.hotControl 자리.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw::editor
{
    /** @brief 손잡이 도구가 보내는 명령의 종류입니다. */
    enum class EditorHandleDragCommandKind : uint8
    {
        Capture, ///< 손잡이를 눌렀다 — 대상의 시작 전 스냅샷을 찍는다(되돌리기의 "전")
        Drag,    ///< 끄는 중이다 — 마우스 레이가 가리키는 자리로 값을 고친다
        End,     ///< 놓았거나(`_bCommit`) 끊겼다 — 바뀌었고 편집이 허용되면 되돌리기를 남기고 닫는다
    };

    /** @brief 손잡이 명령 하나입니다(값만 든다). */
    template <typename THandle>
    struct EditorHandleDragCommand
    {
        THandle                     _handle{};
        Ray                         _ray{};
        GameObjectHandle            _object{};
        EditorHandleDragCommandKind _kind{ EditorHandleDragCommandKind::End };
        bool                        _bCommit{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 놓기(End)가 되돌리기를 남긴다(끊긴 끌기는 버린다)
    };

    /**
     * @class EditorHandleDragTracker
     * @brief 손잡이 도구가 내는 명령의 순서를 정합니다. 마우스 상태만 받아 ImGui · 씬 없이 테스트한다.
     */
    template <typename THandle>
    class EditorHandleDragTracker
    {
    public:
        using Command = EditorHandleDragCommand<THandle>;

        /**
         * @brief 끄는 중의 한 프레임입니다. 대상이 없어졌으면(@p bAvailable false) 커밋 없이, 단추를 놓았으면 커밋으로 닫는다. 아니면 @p pRay 가 있을 때 끌기를 낸다.
         * @return 끌기가 이어지면 true, 이 프레임에 닫았으면 false 입니다(끄지 않는 중이면 @p bAvailable).
         */
        bool step( bool bAvailable, bool bMouseDown, const Ray* pRay, vector<Command>& outListCommand )
        {
            if ( _bDragging == 0 )
                return bAvailable;
            if ( bAvailable == false || bMouseDown == false )
            {
                Command command{};
                command._kind    = EditorHandleDragCommandKind::End;
                command._object  = _object;
                command._bCommit = bAvailable; // 놓았으면 남기고, 대상이 사라졌거나 편집이 막혔으면 버린다
                outListCommand.push_back( command );
                _bDragging = 0;
                _object    = GameObjectHandle{};
                return false;
            }
            if ( pRay != nullptr )
            {
                Command command{};
                command._kind   = EditorHandleDragCommandKind::Drag;
                command._object = _object;
                command._handle = _active;
                command._ray    = *pRay;
                outListCommand.push_back( command );
            }
            return true;
        }

        /** @brief 손잡이 @p handle 을 눌렀습니다 — 잡기 명령을 내고 끌기를 시작한다. */
        void press( GameObjectHandle object, const THandle& handle, vector<Command>& outListCommand )
        {
            Command command{};
            command._kind   = EditorHandleDragCommandKind::Capture;
            command._object = object;
            command._handle = handle;
            outListCommand.push_back( command );
            _object    = object;
            _active    = handle;
            _bDragging = 1;
        }

        bool             isDragging() const { return _bDragging != 0; }
        GameObjectHandle getObject() const { return _object; }
        const THandle&   getActive() const { return _active; }

    private:
        THandle          _active{};
        GameObjectHandle _object{};
        uint8            _bDragging{ 0 }; ///< 끄는 중이다
    };
} // namespace sw::editor
