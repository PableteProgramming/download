#include <iostream>
#include <cmath>
#include <curl/curl.h>

void HideCursor();
void ShowCursor();
int progress_func(void*, double, double, double, double);