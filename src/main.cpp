#include <main.hpp>


int main(){
    std::vector<std::string> opts = {"-o","--output","--url","-u"};
    ArgsParser parser(opts);
    parser.Parse();
    std::cout<<"starting curl example"<<std::endl;
    CURL* curl;
    CURLcode result;
    
    curl= curl_easy_init();

    if(curl){
        curl_easy_setopt(curl,CURLOPT_URL, "https://github.com/PableteProgramming/hosting/raw/master/Cryption-win10-x86.exe");
        /* allow redirections */
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func);

        FILE* file;

        file = fopen("Cryption-win10-x86.exe", "wb");

        if (file) {
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
            HideCursor();
            /* do request */
            result = curl_easy_perform(curl);
            ShowCursor();
            std::cout << std::endl;
            if (result != CURLE_OK) {
                std::cout << "request failed !" << std::endl;
            }
            else {
                std::cout << "request performed successfully!" << std::endl;
            }
            fclose(file);
        }
        else {
            std::cout << "error while opening file" << std::endl;
        }
        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
    else{
        std::cout<<"error initializing curl!"<<std::endl;
    }

    return 0;
}

int progress_func(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
    // ensure that the file to be downloaded is not empty
    // because that would cause a division by zero error later on
    if (TotalToDownload <= 0.0) {
        return 0;
    }

    // how wide you want the progress meter to be
    int totaldotz = 40;
    double fractiondownloaded = NowDownloaded / TotalToDownload;
    // part of the progressmeter that's already "full"
    int dotz = (int)round(fractiondownloaded * totaldotz);

    // create the "meter"
    printf("%3.0f%% [", fractiondownloaded * 100);
    // part  that's full already
    int j = 0;
    for (int i=0; i < dotz; i++) {
        printf("=");
        j = i;
    }
    // remaining part (spaces)
    for (int i=j+1; i < totaldotz; i++) {
        printf(" ");
    }
    // and back to line begin - do not forget the fflush to avoid output buffering problems!
    printf("]\r");
    fflush(stdout);
    // if you don't return 0, the transfer will be aborted - see the documentation
    return 0;
}

#ifdef __linux__
void HideCursor() {
    printf("\e[?25l");
}
void ShowCursor() {
    printf("\e[?25h");
}
#else
#include <windows.h>
void HideCursor() {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 25;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(consoleHandle, &info);
}
void ShowCursor() {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 25;
    info.bVisible = TRUE;
    SetConsoleCursorInfo(consoleHandle, &info);
}
#endif