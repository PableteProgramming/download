#include <request.hpp>

std::pair<int, std::string> DoRequest(std::string url, std::string filename) {
    CURL* curl;
    CURLcode Curlresult;

    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        /* allow redirections */
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_func);

        FILE* file;

        file = fopen(filename.c_str(), "wb");

        if (file) {
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
            /* do request */
            Curlresult = curl_easy_perform(curl);
            if (Curlresult != CURLE_OK) {
                return std::make_pair(1,"request failed !");
                //std::cout << "request failed !" << std::endl;
            }
            else {
                return std::make_pair(0,"request performed successfully!");
                //std::cout << "request performed successfully!" << std::endl;
            }
            fclose(file);
        }
        else {
            return std::make_pair(1,"error while opening file");
            //std::cout << "error while opening file" << std::endl;
        }
        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }
    else {
        return std::make_pair(1,"error initializing curl!");
        //std::cout << "error initializing curl!" << std::endl;
    }
	return std::make_pair(0,"");
}

int progress_func(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded){
    double percentage = NowDownloaded / TotalToDownload * 100;
    std::string sPercentage = std::to_string(percentage);
    std::string info = std::to_string(NowDownloaded) + "/" + std::to_string(TotalToDownload);
    UpdateProgress(sPercentage, info);
    return 0;
}