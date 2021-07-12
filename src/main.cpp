#include <main.hpp>

int totaldotz = 40;

int main(int argc, char** argv){
    std::vector<std::string> opts = {"-o","--output","--url","-u","-v","--verbose"};
    ArgsParser parser(opts);
    
    //parse params
    parser.Parse(parser.ConvertParams(argc,argv));
    std::vector<std::pair<std::pair<std::string, std::string>, bool>> result = parser.GetResult();

    //check if params are correct
    bool outputFound = false;
    std::string output = "";
    bool urlFound = false;
    std::string url = "";
    bool verbose = false;

    for (int i = 0; i < result.size(); i++) {
        if (result[i].first.first=="-o" || result[i].first.first == "--output") {
            if (result[i].second) {
                output = result[i].first.second;
                if (output != "") {
                    std::cout << "output: " << output << std::endl;
                    outputFound = true;
                }
            }
        }
        else if (result[i].first.first == "-u" || result[i].first.first == "--url") {
            if (result[i].second) {
                url = result[i].first.second;
                if (url!="") {
                    std::cout << "url: " << url << std::endl;
                    urlFound = true;
                }
            }
        }
        else if (result[i].first.first == "-v" || result[i].first.first == "--verbose") {
            if (result[i].second) {
                verbose = true;
            }
        }
    }

    if (!outputFound || !urlFound) {
        if (!outputFound && !urlFound) {
            std::cout << "Please provide the following parameters:" << std::endl;
            std::cout << "-o [file name] | --output [file name] => specify the output file name" << std::endl;
            std::cout << "-u [url] | --url [url] => specify the url of the file to download" << std::endl;
            std::cout << std::endl << "Optional parameters:" << std::endl;
            std::cout << "-v | --verbose => enable verbose mode" << std::endl;
        }
        else if (!outputFound) {
            std::cout << "Please provide the following parameters:" << std::endl;
            std::cout << "-o [file name] | --output [file name] => specify the output file name" << std::endl;
            std::cout << std::endl << "Optional parameters:" << std::endl;
            std::cout << "-v | --verbose => enable verbose mode" << std::endl;
        }
        else {
            std::cout << "Please provide the following parameters:" << std::endl;
            std::cout << "-u [url] | --url [url] => specify the url of the file to download" << std::endl;
            std::cout << std::endl << "Optional parameters:" << std::endl;
            std::cout << "-v | --verbose => enable verbose mode" << std::endl;
        }
        ShowCursor();
        return 1;
    }

    std::cout<<"starting curl example"<<std::endl;
    CURL* curl;
    CURLcode Curlresult;
    
    curl= curl_easy_init();

    if(curl){
        curl_easy_setopt(curl,CURLOPT_URL, url.c_str());
        /* allow redirections */
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION, 1L);

        if (verbose) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
            curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func);
        }

        FILE* file;

        file = fopen(output.c_str(), "wb");

        if (file) {
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
            HideCursor();
            /* do request */
            Curlresult = curl_easy_perform(curl);
            ShowCursor();
            if (Curlresult != CURLE_OK) {
                std::cout << "request failed !" << std::endl;
            }
            else {
                if (verbose) {
                    std::cout << "\r";
                    for (int i = 0; i < totaldotz+7; i++) {
                        printf(" ");
                    }
                    std::cout << "\r";
                }
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