﻿#include "pch.h"
#include "UniteWindow.h"
#include "ConfigDialog.h"

//---------------------------------------------------------------------

// デバッグ用コールバック関数。デバッグメッセージを出力する
void ___outputLog(LPCTSTR text, LPCTSTR output)
{
	::OutputDebugString(output);
}

//---------------------------------------------------------------------

AviUtlInternal g_auin;
HINSTANCE g_instance = 0;
HWND g_singleWindow = 0;
HTHEME g_theme = 0;
WNDPROC g_aviutlWindowProc = 0;
WNDPROC g_exeditWindowProc = 0;

AviUtlWindow g_aviutlWindow;
ExeditWindow g_exeditWindow;
SettingDialog g_settingDialog;

Window* g_windowArray[WindowPos::maxSize] =
{
	&g_aviutlWindow,
	&g_settingDialog,
	&g_exeditWindow,
	0,
};

int g_layoutMode = LayoutMode::horzSplit;
Borders g_borders = {};
int g_hotBorder = HotBorder::none;

int g_borderWidth = 8;
int g_captionHeight = 24;
int g_borderSnapRange = 8;
COLORREF g_fillColor = RGB(0x99, 0x99, 0x99);
COLORREF g_borderColor = RGB(0xcc, 0xcc, 0xcc);
COLORREF g_hotBorderColor = RGB(0x00, 0x00, 0x00);

int g_offset = 0; // ドラッグ処理に使う。

//---------------------------------------------------------------------

void initHook()
{
	MY_TRACE(_T("initHook()\n"));

	HMODULE user32 = ::GetModuleHandle(_T("user32.dll"));
	true_CreateWindowExA = (Type_CreateWindowExA)::GetProcAddress(user32, "CreateWindowExA");

	DetourTransactionBegin();
	DetourUpdateThread(::GetCurrentThread());

	ATTACH_HOOK_PROC(CreateWindowExA);
	ATTACH_HOOK_PROC(GetMenu);
	ATTACH_HOOK_PROC(SetMenu);
	ATTACH_HOOK_PROC(DrawMenuBar);
	ATTACH_HOOK_PROC(FindWindowExA);
	ATTACH_HOOK_PROC(FindWindowW);
	ATTACH_HOOK_PROC(GetWindow);
	ATTACH_HOOK_PROC(EnumThreadWindows);
	ATTACH_HOOK_PROC(EnumWindows);
	if (DetourTransactionCommit() == NO_ERROR)
	{
		MY_TRACE(_T("API フックに成功しました\n"));
	}
	else
	{
		MY_TRACE(_T("API フックに失敗しました\n"));
	}
}

void termHook()
{
	MY_TRACE(_T("termHook()\n"));
}

//---------------------------------------------------------------------

HWND createSingleWindow()
{
	MY_TRACE(_T("createSingleWindow()\n"));

	// 土台となるシングルウィンドウを作成する。

	WNDCLASS wc = {};
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.hCursor = ::LoadCursor(0, IDC_ARROW);
	wc.lpfnWndProc = singleWindowProc;
	wc.hInstance = g_instance;
	wc.lpszClassName = _T("AviUtl"); // クラス名を AviUtl に偽装する。「AoiSupport」用。
	::RegisterClass(&wc);

	HWND hwnd = ::CreateWindowEx(
		0,
		_T("AviUtl"),
		_T("UniteWindow"),
		WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME |
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, g_instance, 0);

	// ここでレイアウトの初期値を算出しておく。

	RECT rc; ::GetClientRect(hwnd, &rc);
	int cx = (rc.left + rc.right) / 2;
	int cy = (rc.top + rc.bottom) / 2;

	g_layoutMode = LayoutMode::horzSplit;

	g_borders.m_vertCenter = cx;
	g_borders.m_vertLeft = cy;
	g_borders.m_vertRight = cy;
	g_borders.m_vertCenterOrigin = Origin::bottomRight;
	g_borders.m_vertLeftOrigin = Origin::bottomRight;
	g_borders.m_vertRightOrigin = Origin::bottomRight;

	g_borders.m_horzCenter = cy;
	g_borders.m_horzTop = cx;
	g_borders.m_horzBottom = cx;
	g_borders.m_horzCenterOrigin = Origin::bottomRight;
	g_borders.m_horzTopOrigin = Origin::bottomRight;
	g_borders.m_horzBottomOrigin = Origin::bottomRight;

	return hwnd;
}

void normalizeLayoutVertSplit()
{
	MY_TRACE(_T("normalizeLayoutVertSplit()\n"));

	RECT rc; ::GetClientRect(g_singleWindow, &rc);

	g_borders.m_vertCenter = max(g_borders.m_vertCenter, rc.left);
	g_borders.m_vertCenter = min(g_borders.m_vertCenter, rc.right - g_borderWidth);

	g_borders.m_vertLeft = max(g_borders.m_vertLeft, rc.top);
	g_borders.m_vertLeft = min(g_borders.m_vertLeft, rc.bottom - g_borderWidth);

	g_borders.m_vertRight = max(g_borders.m_vertRight, rc.top);
	g_borders.m_vertRight = min(g_borders.m_vertRight, rc.bottom - g_borderWidth);
}

void normalizeLayoutHorzSplit()
{
	MY_TRACE(_T("normalizeLayoutHorzSplit()\n"));

	RECT rc; ::GetClientRect(g_singleWindow, &rc);

	g_borders.m_horzCenter = max(g_borders.m_horzCenter, rc.top);
	g_borders.m_horzCenter = min(g_borders.m_horzCenter, rc.bottom - g_borderWidth);

	g_borders.m_horzTop = max(g_borders.m_horzTop, rc.left);
	g_borders.m_horzTop = min(g_borders.m_horzTop, rc.right - g_borderWidth);

	g_borders.m_horzBottom = max(g_borders.m_horzBottom, rc.left);
	g_borders.m_horzBottom = min(g_borders.m_horzBottom, rc.right - g_borderWidth);
}

void normalizeLayout()
{
	MY_TRACE(_T("normalizeLayout()\n"));

	switch (g_layoutMode)
	{
	case LayoutMode::vertSplit:
		{
			normalizeLayoutVertSplit();

			break;
		}
	case LayoutMode::horzSplit:
		{
			normalizeLayoutHorzSplit();

			break;
		}
	}
}

int borderToX(LPCRECT rcClient, int border, int borderOrigin)
{
	switch (borderOrigin)
	{
	case Origin::topLeft:
		{
			return border;
		}
	case Origin::bottomRight:
		{
			return rcClient->right - border - g_borderWidth;
		}
	}

	return 0;
}

int borderToY(LPCRECT rcClient, int border, int borderOrigin)
{
	switch (borderOrigin)
	{
	case Origin::topLeft:
		{
			return border;
		}
	case Origin::bottomRight:
		{
			return rcClient->bottom - border - g_borderWidth;
		}
	}

	return 0;
}

int xToBorder(LPCRECT rcClient, int x, int borderOrigin)
{
	switch (borderOrigin)
	{
	case Origin::topLeft:
		{
			return x;
		}
	case Origin::bottomRight:
		{
			return rcClient->right - x - g_borderWidth;
		}
	}

	return 0;
}

int yToBorder(LPCRECT rcClient, int y, int borderOrigin)
{
	switch (borderOrigin)
	{
	case Origin::topLeft:
		{
			return y;
		}
	case Origin::bottomRight:
		{
			return rcClient->bottom - y - g_borderWidth;
		}
	}

	return 0;
}

void recalcLayoutVertSplit()
{
	MY_TRACE(_T("recalcLayoutVertSplit()\n"));

	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);
	int borderVertCenter = borderToX(&rcClient, g_borders.m_vertCenter, g_borders.m_vertCenterOrigin);
	int borderVertLeft = borderToY(&rcClient, g_borders.m_vertLeft, g_borders.m_vertLeftOrigin);
	int borderVertRight = borderToY(&rcClient, g_borders.m_vertRight, g_borders.m_vertRightOrigin);

	if (g_windowArray[WindowPos::topLeft])
	{
		RECT rcContainer =
		{
			rcClient.left,
			rcClient.top + g_captionHeight,
			borderVertCenter,
			borderVertLeft,
		};

		g_windowArray[WindowPos::topLeft]->resize(&rcContainer);
	}

	if (g_windowArray[WindowPos::topRight])
	{
		RECT rcContainer =
		{
			borderVertCenter + g_borderWidth,
			rcClient.top + g_captionHeight,
			rcClient.right,
			borderVertRight,
		};

		g_windowArray[WindowPos::topRight]->resize(&rcContainer);
	}

	if (g_windowArray[WindowPos::bottomLeft])
	{
		RECT rcContainer =
		{
			rcClient.left,
			borderVertLeft + g_borderWidth + g_captionHeight,
			borderVertCenter,
			rcClient.bottom,
		};

		g_windowArray[WindowPos::bottomLeft]->resize(&rcContainer);
	}

	if (g_windowArray[WindowPos::bottomRight])
	{
		RECT rcContainer =
		{
			borderVertCenter + g_borderWidth,
			borderVertRight + g_borderWidth + g_captionHeight,
			rcClient.right,
			rcClient.bottom,
		};

		g_windowArray[WindowPos::bottomRight]->resize(&rcContainer);
	}
}

void recalcLayoutHorzSplit()
{
	MY_TRACE(_T("recalcLayoutHorzSplit()\n"));

	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);
	int borderHorzCenter = borderToY(&rcClient, g_borders.m_horzCenter, g_borders.m_horzCenterOrigin);
	int borderHorzTop = borderToX(&rcClient, g_borders.m_horzTop, g_borders.m_horzTopOrigin);
	int borderHorzBottom = borderToX(&rcClient, g_borders.m_horzBottom, g_borders.m_horzBottomOrigin);

	if (g_windowArray[WindowPos::topLeft])
	{
		RECT rcContainer =
		{
			rcClient.left,
			rcClient.top + g_captionHeight,
			borderHorzTop,
			borderHorzCenter,
		};

		g_windowArray[WindowPos::topLeft]->resize(&rcContainer);
	}

	if (g_windowArray[WindowPos::topRight])
	{
		RECT rcContainer =
		{
			borderHorzTop + g_borderWidth,
			rcClient.top + g_captionHeight,
			rcClient.right,
			borderHorzCenter,
		};

		g_windowArray[WindowPos::topRight]->resize(&rcContainer);
	}

	if (g_windowArray[WindowPos::bottomLeft])
	{
		RECT rcContainer =
		{
			rcClient.left,
			borderHorzCenter + g_borderWidth + g_captionHeight,
			borderHorzBottom,
			rcClient.bottom,
		};

		g_windowArray[WindowPos::bottomLeft]->resize(&rcContainer);
	}

	if (g_windowArray[WindowPos::bottomRight])
	{
		RECT rcContainer =
		{
			borderHorzBottom + g_borderWidth,
			borderHorzCenter + g_borderWidth + g_captionHeight,
			rcClient.right,
			rcClient.bottom,
		};

		g_windowArray[WindowPos::bottomRight]->resize(&rcContainer);
	}
}

void recalcLayout()
{
	MY_TRACE(_T("recalcLayout()\n"));

	if (::IsIconic(g_singleWindow))
		return;

	switch (g_layoutMode)
	{
	case LayoutMode::vertSplit:
		{
			normalizeLayoutVertSplit();
			recalcLayoutVertSplit();

			break;
		}
	case LayoutMode::horzSplit:
		{
			normalizeLayoutHorzSplit();
			recalcLayoutHorzSplit();

			break;
		}
	}
}

int hitTestVertSplit(POINT point)
{
	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);
	int borderVertCenter = borderToX(&rcClient, g_borders.m_vertCenter, g_borders.m_vertCenterOrigin);
	int borderVertLeft = borderToY(&rcClient, g_borders.m_vertLeft, g_borders.m_vertLeftOrigin);
	int borderVertRight = borderToY(&rcClient, g_borders.m_vertRight, g_borders.m_vertRightOrigin);

	if (point.x < borderVertCenter)
	{
		if (point.y >= borderVertLeft && point.y < borderVertLeft + g_borderWidth)
			return HotBorder::vertLeft;
	}
	else if (point.x < borderVertCenter + g_borderWidth)
	{
		return HotBorder::vertCenter;
	}
	else
	{
		if (point.y >= borderVertRight && point.y < borderVertRight + g_borderWidth)
			return HotBorder::vertRight;
	}

	return HotBorder::none;
}

int hitTestHorzSplit(POINT point)
{
	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);
	int borderHorzCenter = borderToY(&rcClient, g_borders.m_horzCenter, g_borders.m_horzCenterOrigin);
	int borderHorzTop = borderToX(&rcClient, g_borders.m_horzTop, g_borders.m_horzTopOrigin);
	int borderHorzBottom = borderToX(&rcClient, g_borders.m_horzBottom, g_borders.m_horzBottomOrigin);

	if (point.y < borderHorzCenter)
	{
		if (point.x >= borderHorzTop && point.x < borderHorzTop + g_borderWidth)
			return HotBorder::horzTop;
	}
	else if (point.y < borderHorzCenter + g_borderWidth)
	{
		return HotBorder::horzCenter;
	}
	else
	{
		if (point.x >= borderHorzBottom && point.x < borderHorzBottom + g_borderWidth)
			return HotBorder::horzBottom;
	}

	return HotBorder::none;
}

int hitTest(POINT point)
{
	switch (g_layoutMode)
	{
	case LayoutMode::vertSplit: return hitTestVertSplit(point);
	case LayoutMode::horzSplit: return hitTestHorzSplit(point);
	}

	return HotBorder::none;
}

int getOffset(POINT point)
{
	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);

	switch (g_hotBorder)
	{
	case HotBorder::horzCenter: return g_borders.m_horzCenter - yToBorder(&rcClient, point.y, g_borders.m_horzCenterOrigin);
	case HotBorder::horzTop: return g_borders.m_horzTop - xToBorder(&rcClient, point.x, g_borders.m_horzTopOrigin);
	case HotBorder::horzBottom: return g_borders.m_horzBottom - xToBorder(&rcClient, point.x, g_borders.m_horzBottomOrigin);
	case HotBorder::vertCenter: return g_borders.m_vertCenter - xToBorder(&rcClient, point.x, g_borders.m_vertCenterOrigin);
	case HotBorder::vertLeft: return g_borders.m_vertLeft - yToBorder(&rcClient, point.y, g_borders.m_vertLeftOrigin);
	case HotBorder::vertRight: return g_borders.m_vertRight - yToBorder(&rcClient, point.y, g_borders.m_vertRightOrigin);
	}

	return 0;
}

void dragBorder(POINT point)
{
	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);

	switch (g_hotBorder)
	{
	case HotBorder::horzCenter:
		{
			g_borders.m_horzCenter = yToBorder(&rcClient, point.y, g_borders.m_horzCenterOrigin) + g_offset;
			break;
		}
	case HotBorder::horzTop:
		{
			g_borders.m_horzTop = xToBorder(&rcClient, point.x, g_borders.m_horzTopOrigin) + g_offset;
			if (abs(g_borders.m_horzTop - g_borders.m_horzBottom) < g_borderSnapRange)
				g_borders.m_horzTop = g_borders.m_horzBottom;
			break;
		}
	case HotBorder::horzBottom:
		{
			g_borders.m_horzBottom = xToBorder(&rcClient, point.x, g_borders.m_horzBottomOrigin) + g_offset;
			if (abs(g_borders.m_horzBottom - g_borders.m_horzTop) < g_borderSnapRange)
				g_borders.m_horzBottom = g_borders.m_horzTop;
			break;
		}
	case HotBorder::vertCenter:
		{
			g_borders.m_vertCenter = xToBorder(&rcClient, point.x, g_borders.m_vertCenterOrigin) + g_offset;
			break;
		}
	case HotBorder::vertLeft:
		{
			g_borders.m_vertLeft = yToBorder(&rcClient, point.y, g_borders.m_vertLeftOrigin) + g_offset;
			if (abs(g_borders.m_vertLeft - g_borders.m_vertRight) < g_borderSnapRange)
				g_borders.m_vertLeft = g_borders.m_vertRight;
			break;
		}
	case HotBorder::vertRight:
		{
			g_borders.m_vertRight = yToBorder(&rcClient, point.y, g_borders.m_vertRightOrigin) + g_offset;
			if (abs(g_borders.m_vertRight - g_borders.m_vertLeft) < g_borderSnapRange)
				g_borders.m_vertRight = g_borders.m_vertLeft;
			break;
		}
	}

	if (::GetKeyState(VK_SHIFT) < 0)
	{
		switch (g_hotBorder)
		{
		case HotBorder::horzTop:	g_borders.m_horzBottom = g_borders.m_horzTop; break;
		case HotBorder::horzBottom:	g_borders.m_horzTop = g_borders.m_horzBottom; break;
		case HotBorder::vertLeft:	g_borders.m_vertRight = g_borders.m_vertLeft; break;
		case HotBorder::vertRight:	g_borders.m_vertLeft = g_borders.m_vertRight; break;
		}
	}
}

BOOL getBorderRect(LPRECT rc, int border)
{
	RECT rcClient; ::GetClientRect(g_singleWindow, &rcClient);
	int borderVertCenter = borderToX(&rcClient, g_borders.m_vertCenter, g_borders.m_vertCenterOrigin);
	int borderVertLeft = borderToY(&rcClient, g_borders.m_vertLeft, g_borders.m_vertLeftOrigin);
	int borderVertRight = borderToY(&rcClient, g_borders.m_vertRight, g_borders.m_vertRightOrigin);
	int borderHorzCenter = borderToY(&rcClient, g_borders.m_horzCenter, g_borders.m_horzCenterOrigin);
	int borderHorzTop = borderToX(&rcClient, g_borders.m_horzTop, g_borders.m_horzTopOrigin);
	int borderHorzBottom = borderToX(&rcClient, g_borders.m_horzBottom, g_borders.m_horzBottomOrigin);

	switch (border)
	{
	case HotBorder::vertCenter:
		{
			rc->left = borderVertCenter;
			rc->top = rcClient.top;
			rc->right = borderVertCenter + g_borderWidth;
			rc->bottom = rcClient.bottom;

			return TRUE;
		}
	case HotBorder::vertLeft:
		{
			rc->left = rcClient.left;
			rc->top = borderVertLeft;
			rc->right = borderVertCenter;
			rc->bottom = borderVertLeft + g_borderWidth;

			return TRUE;
		}
	case HotBorder::vertRight:
		{
			rc->left = borderVertCenter + g_borderWidth;
			rc->top = borderVertRight;
			rc->right = rcClient.right;
			rc->bottom = borderVertRight + g_borderWidth;

			return TRUE;
		}
	case HotBorder::horzCenter:
		{
			rc->left = rcClient.left;
			rc->top = borderHorzCenter;
			rc->right = rcClient.right;
			rc->bottom = borderHorzCenter + g_borderWidth;

			return TRUE;
		}
	case HotBorder::horzTop:
		{
			rc->left = borderHorzTop;
			rc->top = rcClient.top;
			rc->right = borderHorzTop + g_borderWidth;
			rc->bottom = borderHorzCenter;

			return TRUE;
		}
	case HotBorder::horzBottom:
		{
			rc->left = borderHorzBottom;
			rc->top = borderHorzCenter + g_borderWidth;
			rc->right = borderHorzBottom + g_borderWidth;
			rc->bottom = rcClient.bottom;

			return TRUE;
		}
	}

	return FALSE;
}

void drawCaption(HDC dc, HWND hwnd, Window* window)
{
	// コンテナウィンドウの矩形を取得する。
	RECT rc; ::GetWindowRect(window->m_hwndContainer, &rc);

	if ((rc.bottom - rc.top) <= 0)
		return; // ウィンドウの高さが小さすぎる場合は何もしない。

	// コンテナウィンドウの上隣にあるキャプション矩形を取得する。
	::MapWindowPoints(0, hwnd, (POINT*)&rc, 2);
	rc.bottom = rc.top;
	rc.top = rc.top - g_captionHeight;

	// ウィンドウテキストを取得する。
	WCHAR text[MAX_PATH] = {};
	::GetWindowTextW(window->m_hwnd, text, MAX_PATH);

	// ウィンドウの状態から stateId を取得する。
	int stateId = CS_ACTIVE;
	if (::GetFocus() != window->m_hwnd) stateId = CS_INACTIVE;
	if (!::IsWindowEnabled(window->m_hwnd)) stateId = CS_DISABLED;

	// テーマ API を使用してタイトルを描画する。
	::DrawThemeBackground(g_theme, dc, WP_CAPTION, stateId, &rc, 0);
	::DrawThemeText(g_theme, dc, WP_CAPTION, stateId,
		text, ::lstrlenW(text), DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0, &rc);
}

//---------------------------------------------------------------------

BOOL importLayout(HWND hwnd)
{
	// ファイル選択ダイアログを表示してファイル名を取得する。

	WCHAR fileName[MAX_PATH] = {};

	WCHAR folderName[MAX_PATH] = {};
	::GetModuleFileNameW(g_instance, folderName, MAX_PATH);
	::PathRemoveExtensionW(folderName);

	OPENFILENAMEW ofn = { sizeof(ofn) };
	ofn.hwndOwner = hwnd;
	ofn.Flags = OFN_FILEMUSTEXIST;
	ofn.lpstrTitle = L"レイアウトのインポート";
	ofn.lpstrInitialDir = folderName;
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"レイアウトファイル (*.xml)\0*.xml\0" "すべてのファイル (*.*)\0*.*\0";
	ofn.lpstrDefExt = L"xml";

	if (!::GetOpenFileNameW(&ofn))
		return FALSE;

	// レイアウトファイルをインポートする。
	loadConfig(fileName, TRUE);

	// レイアウトを再計算する。
	recalcLayout();

	// 再描画する。
	::InvalidateRect(hwnd, 0, FALSE);

	return TRUE;
}

BOOL exportLayout(HWND hwnd)
{
	// ファイル選択ダイアログを表示してファイル名を取得する。

	WCHAR fileName[MAX_PATH] = {};

	WCHAR folderName[MAX_PATH] = {};
	::GetModuleFileNameW(g_instance, folderName, MAX_PATH);
	::PathRemoveExtensionW(folderName);

	OPENFILENAMEW ofn = { sizeof(ofn) };
	ofn.hwndOwner = hwnd;
	ofn.Flags = OFN_OVERWRITEPROMPT;
	ofn.lpstrTitle = L"レイアウトのエクスポート";
	ofn.lpstrInitialDir = folderName;
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"レイアウトファイル (*.xml)\0*.xml\0" "すべてのファイル (*.*)\0*.*\0";
	ofn.lpstrDefExt = L"xml";

	if (!::GetSaveFileNameW(&ofn))
		return FALSE;

	// レイアウトファイルをエクスポートする。
	saveConfig(fileName, TRUE);

	return TRUE;
}

LRESULT CALLBACK singleWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COMMAND:
		{
			MY_TRACE(_T("singleWindowProc(WM_COMMAND)\n"));

			return ::SendMessage(g_aviutlWindow.m_hwnd, message, wParam, lParam);
		}
	case WM_SYSCOMMAND:
		{
			MY_TRACE(_T("singleWindowProc(WM_SYSCOMMAND)\n"));

			switch (wParam)
			{
			case CommandID::ShowConfigDialog:
				{
					// UniteWindow の設定ダイアログを開く。
					showConfigDialog(hwnd);

					break;
				}
			case CommandID::ImportLayout:
				{
					// レイアウトファイルをインポートする。
					importLayout(hwnd);

					break;
				}
			case CommandID::ExportLayout:
				{
					// レイアウトファイルをエクスポートする。
					exportLayout(hwnd);

					break;
				}
			}

			break;
		}
	case WM_CREATE:
		{
			MY_TRACE(_T("singleWindowProc(WM_CREATE)\n"));

			g_theme = ::OpenThemeData(hwnd, VSCLASS_WINDOW);
			MY_TRACE_HEX(g_theme);

			HMENU menu = ::GetSystemMenu(hwnd, FALSE);
			::InsertMenu(menu, 0, MF_BYPOSITION | MF_STRING, CommandID::ImportLayout, _T("レイアウトのインポート"));
			::InsertMenu(menu, 1, MF_BYPOSITION | MF_STRING, CommandID::ExportLayout, _T("レイアウトのエクスポート"));
			::InsertMenu(menu, 2, MF_BYPOSITION | MF_STRING, CommandID::ShowConfigDialog, _T("UniteWindowの設定"));
			::InsertMenu(menu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, 0);

			break;
		}
	case WM_DESTROY:
		{
			MY_TRACE(_T("singleWindowProc(WM_DESTROY)\n"));

			::CloseThemeData(g_theme), g_theme = 0;

			break;
		}
	case WM_CLOSE:
		{
			MY_TRACE(_T("singleWindowProc(WM_CLOSE)\n"));

			return ::SendMessage(g_aviutlWindow.m_hwnd, message, wParam, lParam);
		}
	case WM_SETFOCUS:
		{
			MY_TRACE(_T("singleWindowProc(WM_SETFOCUS)\n"));

			::SetFocus(g_aviutlWindow.m_hwnd);

			break;
		}
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = ::BeginPaint(hwnd, &ps);
			RECT rc = ps.rcPaint;

			BP_PAINTPARAMS pp = { sizeof(pp) };
			HDC mdc = 0;
			HPAINTBUFFER pb = ::BeginBufferedPaint(dc, &rc, BPBF_COMPATIBLEBITMAP, &pp, &mdc);

			if (pb)
			{
				HDC dc = mdc;

				{
					// 背景を塗りつぶす。

					HBRUSH brush = ::CreateSolidBrush(g_fillColor);
					FillRect(dc, &rc, brush);
					::DeleteObject(brush);
				}

				{
					// ボーダーを描画する。

					HBRUSH brush = ::CreateSolidBrush(g_borderColor);

					int firstBorder = (g_layoutMode == LayoutMode::vertSplit) ? HotBorder::vertCenter : HotBorder::horzCenter;
					for (int i = 0; i < 3; i++)
					{
						int border = firstBorder + i;
						if (border == g_hotBorder) continue;
						RECT rcBorder;
						if (getBorderRect(&rcBorder, border))
							::FillRect(dc, &rcBorder, brush);
					}

					::DeleteObject(brush);
				}

				{
					// ホットボーダーを描画する。

					RECT rcHotBorder;
					if (getBorderRect(&rcHotBorder, g_hotBorder))
					{
						HBRUSH brush = ::CreateSolidBrush(g_hotBorderColor);
						::FillRect(dc, &rcHotBorder, brush);
						::DeleteObject(brush);
					}
				}

				{
					// 各ウィンドウのキャプションを描画する。

					LOGFONTW lf = {};
					::GetThemeSysFont(g_theme, TMT_CAPTIONFONT, &lf);
					HFONT font = ::CreateFontIndirectW(&lf);
					HFONT oldFont = (HFONT)::SelectObject(dc, font);

					drawCaption(dc, hwnd, &g_aviutlWindow);
					drawCaption(dc, hwnd, &g_exeditWindow);
					drawCaption(dc, hwnd, &g_settingDialog);

					::SelectObject(dc, oldFont);
					::DeleteObject(font);
				}

				::EndBufferedPaint(pb, TRUE);
			}

			EndPaint(hwnd, &ps);
			return 0;
		}
	case WM_SIZE:
		{
			recalcLayout();

			break;
		}
	case WM_SETCURSOR:
		{
			if (hwnd == (HWND)wParam)
			{
				POINT point; ::GetCursorPos(&point);
				::ScreenToClient(hwnd, &point);

				int hotBorder = hitTest(point);

				switch (hotBorder)
				{
				case HotBorder::vertCenter:
				case HotBorder::horzTop:
				case HotBorder::horzBottom:
					{
						::SetCursor(::LoadCursor(0, IDC_SIZEWE));

						return TRUE;
					}
				case HotBorder::horzCenter:
				case HotBorder::vertLeft:
				case HotBorder::vertRight:
					{
						::SetCursor(::LoadCursor(0, IDC_SIZENS));

						return TRUE;
					}
				}
			}

			break;
		}
	case WM_LBUTTONDOWN:
		{
			MY_TRACE(_T("singleWindowProc(WM_LBUTTONDOWN)\n"));

			// マウス座標を取得する。
			POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			// マウス座標にあるボーダーを取得する。
			g_hotBorder = hitTest(point);

			// ボーダーが有効かチェックする。
			if (g_hotBorder != HotBorder::none)
			{
				// オフセットを取得する。
				g_offset = getOffset(point);

				// マウスキャプチャを開始する。
				::SetCapture(hwnd);

				// 再描画する。
				::InvalidateRect(hwnd, 0, FALSE);
			}

			break;
		}
	case WM_LBUTTONUP:
		{
			MY_TRACE(_T("singleWindowProc(WM_LBUTTONUP)\n"));

			// マウス座標を取得する。
			POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			// マウスをキャプチャ中かチェックする。
			if (::GetCapture() == hwnd)
			{
				// マウスキャプチャを終了する。
				::ReleaseCapture();

				// ボーダーを動かす。
				dragBorder(point);

				// レイアウトを再計算する。
				recalcLayout();

				// 再描画する。
				::InvalidateRect(hwnd, 0, FALSE);
			}

			break;
		}
	case WM_MOUSEMOVE:
		{
			// マウス座標を取得する。
			POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			// マウスをキャプチャ中かチェックする。
			if (::GetCapture() == hwnd)
			{
				// ボーダーを動かす。
				dragBorder(point);

				// レイアウトを再計算する。
				recalcLayout();

				// 再描画する。
				::InvalidateRect(hwnd, 0, FALSE);
			}
			else
			{
				// マウス座標にあるボーダーを取得する。
				int hotBorder = hitTest(point);

				// ホットボーダーと別のボーダーかチェックする。
				if (g_hotBorder != hotBorder)
				{
					// ホットボーダーを更新する。
					g_hotBorder = hotBorder;

					// 再描画する。
					::InvalidateRect(hwnd, 0, FALSE);
				}

				// マウスリーブイベントをトラックする。
				TRACKMOUSEEVENT tme = { sizeof(tme) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				::TrackMouseEvent(&tme);
			}

			break;
		}
	case WM_MOUSELEAVE:
		{
			MY_TRACE(_T("singleWindowProc(WM_MOUSELEAVE)\n"));

			// 無効なボーダーを取得する。
			int hotBorder = HotBorder::none;

			// ホットボーダーと別のボーダーかチェックする。
			if (g_hotBorder != hotBorder)
			{
				// ホットボーダーを更新する。
				g_hotBorder = hotBorder;

				// 再描画する。
				::InvalidateRect(hwnd, 0, FALSE);
			}

			break;
		}
	case WindowMessage::WM_POST_INIT: // 最後の初期化処理。
		{
			// 最初のレイアウト計算。
			recalcLayout();
			::SetForegroundWindow(hwnd);
			::SetActiveWindow(hwnd);

			break;
		}
	}

	return ::DefWindowProc(hwnd, message, wParam, lParam);
}

//---------------------------------------------------------------------

IMPLEMENT_HOOK_PROC_NULL(HWND, WINAPI, CreateWindowExA, (DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param))
{
	if (!((DWORD)className & 0xFFFF0000UL))
	{
		// className が ATOM の場合は何もしない。
		return true_CreateWindowExA(exStyle, className, windowName, style, x, y, w, h, parent, menu, instance, param);
	}

	// デバッグ用出力。
	MY_TRACE(_T("CreateWindowExA(%hs, %hs)\n"), className, windowName);

	if (::lstrcmpiA(windowName, "AviUtl") == 0)
	{
		// AviUtl ウィンドウが作成される直前のタイミング。

		// 土台となるシングルウィンドウを作成する。
		g_singleWindow = createSingleWindow();

		// 設定をファイルから読み込む。
		loadConfig();

		// シングルウィンドウが非表示なら表示する。
		if (!::IsWindowVisible(g_singleWindow))
			::ShowWindow(g_singleWindow, SW_SHOW);
	}
	else if (::lstrcmpiA(className, "AviUtl") == 0 && parent == g_aviutlWindow.m_hwnd)
	{
		// AviUtl のポップアップウィンドウの親をシングルウィンドウに変更する。
		parent = g_singleWindow;
	}

	HWND hwnd = true_CreateWindowExA(exStyle, className, windowName, style, x, y, w, h, parent, menu, instance, param);

	if (::lstrcmpiA(windowName, "AviUtl") == 0)
	{
		// AviUtl ウィンドウに関する初期化処理を行う。
		g_aviutlWindow.init(hwnd);
	}
	else if (::lstrcmpiA(windowName, "拡張編集") == 0)
	{
		// 拡張編集が読み込まれたのでアドレスを取得する。
		g_auin.initExEditAddress();

		// 設定ダイアログのフックを仕掛ける。
		true_SettingDialogProc = g_auin.HookSettingDialogProc(hook_SettingDialogProc);
		MY_TRACE_HEX(true_SettingDialogProc);
		MY_TRACE_HEX(&hook_SettingDialogProc);

		DWORD exedit = g_auin.GetExedit();

		// rikky_memory.auf + rikky_module.dll 用のフック。
		true_ScriptParamDlgProc = writeAbsoluteAddress(exedit + 0x3454 + 1, hook_ScriptParamDlgProc);

		// 拡張編集ウィンドウに関する初期化処理を行う。
		g_exeditWindow.init(hwnd);
	}
	else if (::lstrcmpiA(windowName, "ExtendedFilter") == 0)
	{
		// 設定ダイアログに関する初期化処理を行う。
		g_settingDialog.init(hwnd);

		// すべてのウィンドウの初期化処理が終わったので
		// ポストメッセージ先で最初のレイアウト計算を行う。
		::PostMessage(g_singleWindow, WindowMessage::WM_POST_INIT, 0, 0);
	}
#if 0
	else if (::lstrcmpiA(className, "AviUtl") == 0 && parent == g_aviutlWindow.m_hwnd)
	{
		// 「スクリプト並べ替え管理」「シークバー＋」用。
		// ::GetWindow(fp->hwnd, GW_OWNER) が AviUtl ウィンドウを返すようにする。
		::SetWindowLong(hwnd, GWL_HWNDPARENT, (LONG)g_aviutlWindow.m_hwnd);
	}
#endif
	return hwnd;
}

/*
	GetMenu、SetMenu、DrawMenuBar では
	AviUtl ウィンドウのハンドルが渡されたとき、シングルウィンドウのハンドルに取り替えて偽装する。
	これによって、AviUtl ウィンドウのメニュー処理がシングルウィンドウに対して行われるようになる。
*/
IMPLEMENT_HOOK_PROC(HMENU, WINAPI, GetMenu, (HWND hwnd))
{
//	MY_TRACE(_T("GetMenu(0x%08X)\n"), hwnd);

	if (hwnd == g_aviutlWindow.m_hwnd)
	{
//		MY_TRACE(_T("ウィンドウを偽装します\n"));

		hwnd = g_singleWindow;
	}

	return true_GetMenu(hwnd);
}

IMPLEMENT_HOOK_PROC(BOOL, WINAPI, SetMenu, (HWND hwnd, HMENU menu))
{
//	MY_TRACE(_T("SetMenu(0x%08X, 0x%08X)\n"), hwnd, menu);

	if (hwnd == g_aviutlWindow.m_hwnd)
	{
//		MY_TRACE(_T("ウィンドウを偽装します\n"));

		hwnd = g_singleWindow;
	}

	return true_SetMenu(hwnd, menu);
}

IMPLEMENT_HOOK_PROC(BOOL, WINAPI, DrawMenuBar, (HWND hwnd))
{
//	MY_TRACE(_T("DrawMenuBar(0x%08X)\n"), hwnd);

	if (hwnd == g_aviutlWindow.m_hwnd)
	{
//		MY_TRACE(_T("ウィンドウを偽装します\n"));

		hwnd = g_singleWindow;
	}

	return true_DrawMenuBar(hwnd);
}

IMPLEMENT_HOOK_PROC(HWND, WINAPI, FindWindowExA, (HWND parent, HWND childAfter, LPCSTR className, LPCSTR windowName))
{
	MY_TRACE(_T("FindWindowExA(0x%08X, 0x%08X, %hs, %hs)\n"), parent, childAfter, className, windowName);

	// 「テキスト編集補助プラグイン」用。
	if (!parent && className && ::lstrcmpiA(className, "ExtendedFilterClass") == 0)
		return g_settingDialog.m_hwnd;

	return true_FindWindowExA(parent, childAfter, className, windowName);
}

IMPLEMENT_HOOK_PROC(HWND, WINAPI, FindWindowW, (LPCWSTR className, LPCWSTR windowName))
{
	MY_TRACE(_T("FindWindowW(%ws, %ws)\n"), className, windowName);

	// 「PSDToolKit」の「送る」用。
	if (className && ::lstrcmpiW(className, L"ExtendedFilterClass") == 0)
		return g_settingDialog.m_hwnd;

	return true_FindWindowW(className, windowName);
}

IMPLEMENT_HOOK_PROC(HWND, WINAPI, GetWindow, (HWND hwnd, UINT cmd))
{
//	MY_TRACE(_T("GetWindow(0x%08X, %d)\n"), hwnd, cmd);
//	MY_TRACE_HWND(hwnd);

	if (cmd == GW_OWNER)
	{
		if (hwnd == g_exeditWindow.m_hwnd)
		{
			// 拡張編集ウィンドウのオーナーウィンドウは AviUtl ウィンドウ。
			return g_aviutlWindow.m_hwnd;
		}
		else if (hwnd == g_settingDialog.m_hwnd)
		{
			// 設定ダイアログのオーナーウィンドウは拡張編集ウィンドウ。
			return g_exeditWindow.m_hwnd;
		}
#if 1
		HWND retValue = true_GetWindow(hwnd, cmd);

		if (retValue == g_singleWindow)
		{
			// 「スクリプト並べ替え管理」「シークバー＋」などの一般的なプラグイン用。
			// シングルウィンドウがオーナーになっている場合は AviUtl ウィンドウを返すようにする。
			return g_aviutlWindow.m_hwnd;
		}

		return retValue;
#endif
	}

	return true_GetWindow(hwnd, cmd);
}

IMPLEMENT_HOOK_PROC(BOOL, WINAPI, EnumThreadWindows, (DWORD threadId, WNDENUMPROC enumProc, LPARAM lParam))
{
	MY_TRACE(_T("EnumThreadWindows(%d, 0x%08X, 0x%08X)\n"), threadId, enumProc, lParam);

	// 「イージング設定時短プラグイン」用。
	if (threadId == ::GetCurrentThreadId() && enumProc && lParam)
	{
		// enumProc() の中で ::GetWindow() が呼ばれる。
		if (!enumProc(g_settingDialog.m_hwnd, lParam))
			return FALSE;
	}

	return true_EnumThreadWindows(threadId, enumProc, lParam);
}

IMPLEMENT_HOOK_PROC(BOOL, WINAPI, EnumWindows, (WNDENUMPROC enumProc, LPARAM lParam))
{
	MY_TRACE(_T("EnumWindows(0x%08X, 0x%08X)\n"), enumProc, lParam);

	// 「拡張編集RAMプレビュー」用。
	if (enumProc && lParam)
	{
		if (!enumProc(g_aviutlWindow.m_hwnd, lParam))
			return FALSE;
	}

	return true_EnumWindows(enumProc, lParam);
}

IMPLEMENT_HOOK_PROC_NULL(INT_PTR, CALLBACK, ScriptParamDlgProc, (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam))
{
	switch (message)
	{
	case WM_INITDIALOG:
		{
			MY_TRACE(_T("ScriptParamDlgProc(WM_INITDIALOG)\n"));

			// rikky_memory.auf + rikky_module.dll 用。
			::PostMessage(g_settingDialog.m_hwnd, WM_NCACTIVATE, FALSE, (LPARAM)hwnd);

			break;
		}
	}

	return true_ScriptParamDlgProc(hwnd, message, wParam, lParam);
}

COLORREF WINAPI Dropper_GetPixel(HDC _dc, int x, int y)
{
	MY_TRACE(_T("Dropper_GetPixel(0x%08X, %d, %d)\n"), _dc, x, y);

	// すべてのモニタのすべての場所から色を抽出できるようにする。

	POINT point; ::GetCursorPos(&point);
	::LogicalToPhysicalPointForPerMonitorDPI(0, &point);
	HDC dc = ::GetDC(0);
	COLORREF color = ::GetPixel(dc, point.x, point.y);
	::ReleaseDC(0, dc);
	return color;
}

BOOL isAncestor(HWND hwnd, HWND child)
{
	while (child)
	{
		if (child == hwnd)
			return TRUE;

		child = ::GetParent(child);
	}

	return FALSE;
}

HWND WINAPI KeyboardHook_GetActiveWindow()
{
	MY_TRACE(_T("KeyboardHook_GetActiveWindow()\n"));

	HWND focus = ::GetFocus();

	if (isAncestor(g_settingDialog.m_hwnd, focus))
	{
		MY_TRACE(_T("設定ダイアログを返します\n"));
		return g_settingDialog.m_hwnd;
	}

	if (isAncestor(g_exeditWindow.m_hwnd, focus))
	{
		MY_TRACE(_T("拡張編集ウィンドウを返します\n"));
		return g_exeditWindow.m_hwnd;
	}

	return ::GetActiveWindow();
}

//---------------------------------------------------------------------

EXTERN_C BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		{
			// ロケールを設定する。
			// これをやらないと日本語テキストが文字化けするので最初に実行する。
			_tsetlocale(LC_ALL, _T(""));

			MY_TRACE(_T("DLL_PROCESS_ATTACH\n"));

			// この DLL のハンドルをグローバル変数に保存しておく。
			g_instance = instance;
			MY_TRACE_HEX(g_instance);

			// この DLL の参照カウンタを増やしておく。
			WCHAR moduleFileName[MAX_PATH] = {};
			::GetModuleFileNameW(g_instance, moduleFileName, MAX_PATH);
			::LoadLibraryW(moduleFileName);

			initHook();

			break;
		}
	case DLL_PROCESS_DETACH:
		{
			MY_TRACE(_T("DLL_PROCESS_DETACH\n"));

			termHook();

			break;
		}
	}

	return TRUE;
}

//---------------------------------------------------------------------
