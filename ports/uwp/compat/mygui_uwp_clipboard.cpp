#include <MyGUI_WindowsClipboardHandler.h>

namespace MyGUI
{
	WindowsClipboardHandler::WindowsClipboardHandler()
		: mHwnd(0)
	{
	}

	void WindowsClipboardHandler::initialise()
	{
	}

	void WindowsClipboardHandler::shutdown()
	{
	}

	void WindowsClipboardHandler::handleClipboardChanged(std::string_view, std::string_view)
	{
	}

	void WindowsClipboardHandler::handleClipboardRequested(std::string_view, std::string&)
	{
	}
} // namespace MyGUI
