#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <resource.h>

const int Wwidth= 400;
const int Wheight=400;
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
void CreateLayout(HWND);