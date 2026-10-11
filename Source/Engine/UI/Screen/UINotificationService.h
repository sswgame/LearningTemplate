/**
 * @file UINotificationService.h
 * @brief 알림과 토스트입니다. "저장했습니다", "다시 시작하면 적용됩니다", 퀘스트 갱신, 튜토리얼 힌트를 오버레이 레이어에 최대 셋까지 쌓아 보여 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /** @brief 알림 종류입니다. 겉모습(스타일 클래스 `info`, `achievement`, `warning`, `hint`)과 기본 길이가 다릅니다. */
    ENUM()
    enum class UINotificationKind : uint8
    {
        Info,        ///< 저장이나 설정 적용 같은 알림입니다.
        Achievement, ///< 성취나 퀘스트 갱신입니다.
        Warning,     ///< 다시 시작이 필요하거나 연결이 끊긴 경우입니다.
        Hint         ///< 튜토리얼 힌트입니다(`[action=이름]` 글리프를 가집니다).
    };

    /** @brief 알림 하나입니다. 텍스트는 현지화 키이거나 텍스트 그대로이며(텍스트 위젯이 해석합니다), 리치 텍스트라서 `[action=Interact]` 가 지금 장치의 글리프가 됩니다. */
    struct UINotificationDesc
    {
        string             _text{};
        float32            _durationSeconds{ 4.0f }; ///< 보이는 시간입니다(대기열에서 기다린 시간은 세지 않습니다).
        int32              _priority{ 0 };           ///< 클수록 대기열에서 먼저 나옵니다(같으면 먼저 온 것).
        UINotificationKind _kind{ UINotificationKind::Info };
        bool               _bMergeSameText{ true }; ///< 같은 텍스트가 보이거나 기다리는 중이면 새로 쌓지 않고 횟수를 셉니다("×2"). 시간은 다시 측정합니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class UINotificationService
     * @brief `UISystem` 의 알림과 토스트입니다(`UISystem::getNotifications`, 언리얼 Lyra 의 CommonUI 메시지 스택과 유니티 토스트에 해당).
     * @details 보이는 알림은 `kMaxVisible` 개까지이고(오래된 것이 위), 나머지는 대기열(우선순위, 온 순서)에 둔다. 화면은 오버레이 레이어 문서 `engine/ui/notifications.ui.xml` 이며,
     *          입력과 포커스를 받지 않으므로 게임과 메뉴를 막지 않는다. 보일 것이 없으면 화면을 닫는다. 항목은 조각 `engine/ui/parts/notification.ui.xml`
     *          (`Message`, `Count`, 루트의 스타일 클래스 `notification <종류>`)로 만든다. 게임 스레드에서만 쓴다.
     */
    class SW_API UINotificationService
    {
    public:
        static constexpr uint32 kMaxVisible       = 3;
        static constexpr utf8   kScreenDocument[] = "engine/ui/notifications.ui.xml";
        static constexpr utf8   kEntryDocument[]  = "engine/ui/parts/notification.ui.xml";

        explicit UINotificationService( UISystem& ui );
        UINotificationService( const UINotificationService& )            = delete;
        UINotificationService& operator=( const UINotificationService& ) = delete;

        /** @brief 알림을 올립니다(보일 자리가 있으면 다음 `update` 에 보이고, 없으면 기다립니다). */
        void post( const UINotificationDesc& desc );
        /** @brief 보이는 알림의 시간을 재고, 끝난 것을 내리고, 기다리는 것을 올립니다(`UISystem::update` 가 부른다). */
        void update( float32 deltaSeconds );
        /** @brief 모두 버리고 화면을 닫습니다. */
        void clear();

        uint32 getVisibleCount() const { return static_cast<uint32>( _listVisible.size() ); }
        uint32 getQueuedCount() const { return static_cast<uint32>( _listQueued.size() ); }
        /** @brief 보이는 알림 @p index(위부터)의 텍스트와 센 횟수입니다. */
        const string& getVisibleText( uint32 index ) const { return _listVisible[index]._desc._text; }
        uint32        getVisibleCountOf( uint32 index ) const { return _listVisible[index]._count; }
        /** @brief 알림 화면입니다(보일 것이 없으면 무효). */
        UIScreenHandle getScreen() const { return _screen; }

    private:
        /** @struct Entry @brief 알림 하나의 상태입니다. 서술, 센 횟수, 보인 시간, 위젯을 가집니다. */
        struct Entry
        {
            UINotificationDesc _desc{};
            uint64             _order{ 0 };
            float32            _ageSeconds{ 0.0f };
            uint32             _count{ 1 };
            WidgetID           _widget{ kInvalidWidgetID };
        };

        /** @brief 같은 글의 알림을 찾습니다(없으면 nullptr). */
        Entry* findSameText( const string& text );
        /** @brief 알림 화면을 엽니다(이미 열렸으면 그것). 열지 못하면 nullptr 입니다. */
        UIScreen* acquireScreen();
        /** @brief 항목 위젯을 만들어 화면의 `Stack` 에 붙입니다. */
        void showEntry( Entry& entry );
        /** @brief 항목 위젯의 텍스트와 센 횟수를 씁니다. */
        void refreshEntry( const Entry& entry );

    private:
        vector<Entry>  _listVisible; ///< 보이는 알림입니다(위부터).
        vector<Entry>  _listQueued;  ///< 기다리는 알림입니다.
        UISystem&      _ui;
        uint64         _nextOrder;
        UIScreenHandle _screen;
    };
} // namespace sw
