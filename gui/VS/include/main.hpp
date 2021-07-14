#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <resource.h>

const int Wwidth= 730;
const int Wheight=245;
const int offset = 50;
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
void CreateLayout(HWND,HWND&,HWND&);