#include <main.hpp>

int main(){
    std::cout<<"starting curl example"<<std::endl;
    CURL* curl;
    CURLcode result;

    curl= curl_easy_init();

    if(curl){
        curl_easy_setopt(curl,CURLOPT_URL, "https://pabloprogrammingsoftware.herokuapp.com");
        /* allow redirections */
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION, 1L);

        /* do request */
        result= curl_easy_perform(curl);

        if(result != CURLE_OK){
            std::cout<<"request failed !"<<std::endl;
        }
        else{
            std::cout<<"request performed successfully!"<<std::endl;
        }
        curl_easy_cleanup(curl);
    }
    else{
        std::cout<<"error initializing curl!"<<std::endl;
    }

    return 0;
}