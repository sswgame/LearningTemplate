#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
	/** @brief 알림 유형 */
	enum class NotificationType : uint8
	{
		Info = 0,
		Success,
		Warning,
		Error
	};

	/** @brief 개별 토스트 알림 항목 */
	struct NotificationItem
	{
		string			 _title;
		string			 _message;
		NotificationType _type{ NotificationType::Info };
		float32			 _durationSec{ 4.0f };
		float32			 _elapsedSec{ 0.0f };
		float32			 _progress{ -1.0f }; ///< 0.0 ~ 1.0 이면 프로그레스 바 표시, 음수면 미표시
	};

	/**
	 * @class EditorNotificationManager
	 * @brief 화면 우측 하단 비동기 토스트 알림 및 프로그레스 바 렌더링을 총괄하는 정적 클래스
	 */
	class EditorNotificationManager
	{
	public:
		EditorNotificationManager()	 = default;
		~EditorNotificationManager() = default;

		// Static Public API
		static void	  push( string_view title, string_view message, NotificationType type = NotificationType::Info,
							float32 durationSec = 4.0f, float32 progress = -1.0f );
		static void	  updateAndDraw( float32 deltaTime, float32 screenWidth, float32 screenHeight );
		static size_t getNotificationCount();

		// Implementation methods (owned by EditorContext)
		void   pushImpl( string_view title, string_view message, NotificationType type, float32 durationSec,
						 float32 progress );
		void   updateAndDrawImpl( float32 deltaTime, float32 screenWidth, float32 screenHeight );
		size_t getNotificationCountImpl() const { return _listNotifications.size(); }

	private:
		vector<NotificationItem> _listNotifications;
	};
} // namespace sw::editor
