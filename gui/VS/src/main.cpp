#include <main.hpp>

static TCHAR szWindowClass[] = _T("DesktopApp");
static TCHAR szTitle[] = _T("Download Files from the Web");

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){
    
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(wcex.hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCE(ID_MENU1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex)){
        MessageBox(NULL, _T("Call to RegisterClassEx failed!"), szTitle, NULL);
        return 1;
    }
    HINSTANCE hInst;
    hInst = hInstance;

    // The parameters to CreateWindowEx explained:
    // WS_EX_OVERLAPPEDWINDOW : An optional extended window style.
    // szWindowClass: the name of the application
    // szTitle: the text that appears in the title bar
    // WS_OVERLAPPEDWINDOW: the type of window to create
    // CW_USEDEFAULT, CW_USEDEFAULT: initial position (x, y)
    // 500, 100: initial size (width, length)
    // NULL: the parent of this window
    // NULL: this application does not have a menu bar
    // hInstance: the first parameter from WinMain
    // NULL: not used in this application
    HWND hWnd = CreateWindowEx(NULL,szWindowClass, szTitle, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, Wwidth, Wheight, NULL, NULL, hInstance, NULL);
    if (!hWnd)
    {
        MessageBox(NULL, _T("Call to CreateWindow failed!"), szTitle, NULL);
        return 1;
    }

    // The parameters to ShowWindow explained:
    // hWnd: the value returned from CreateWindow
    // nCmdShow: the fourth parameter from WinMain
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HINSTANCE hInstance;
    switch (message)
    {
        case WM_CREATE: {
            hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
            CreateLayout(hWnd);
            break;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_CREATEBUTTON:{
                    DialogBox(hInstance,MAKEINTRESOURCE(DoneDialog),hWnd,DlgProc);
                    break;
                }
                case ID_EXIT: {
                    PostQuitMessage(0);
                    break;
                }
                case ID_ONLINEHELP: {
                    DialogBox(hInstance, MAKEINTRESOURCE(DoneDialog), hWnd, DlgProc);
                    break;
                }
                case ID_OFFLINEHELP: {
                    DialogBox(hInstance, MAKEINTRESOURCE(DoneDialog), hWnd, DlgProc);
                    break;
                }
            }
            break;
        }
        default: {
            return DefWindowProc(hWnd, message, wParam, lParam);
            break;
        }
    }
    return 0;
}

INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            return TRUE;
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK: {
                    EndDialog(hWnd, FALSE);
                    break;
                }
                case IDCANCEL: {
                    EndDialog(hWnd, FALSE);
                    break;
                }
            }
            break;
        }
    }
    return FALSE;
}

void CreateLayout(HWND hwnd) {
    CreateWindowEx(NULL, TEXT("BUTTON"), TEXT("Download"), WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_BORDER, Wwidth - (35 + 100), Wheight-(25+100), 100, 50, hwnd, (HMENU)ID_CREATEBUTTON, NULL, NULL);
}