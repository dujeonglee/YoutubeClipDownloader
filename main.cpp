#include <iostream>
#include <curl/curl.h>
#include <cstring>
#include <string>
#include <vector>
#include "avltree.h"

std::string response;
static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    for(unsigned int i = 0 ; i < size*nmemb ; i++)
    {
        std::cout<<((char*)ptr)[i];
        response.push_back(((char*)ptr)[i]);
    }
    return size*nmemb;
}

FILE* outfile;
unsigned int recvsize = 0;
static size_t fwrite_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    fwrite(ptr, size, nmemb, outfile);
    recvsize += size*nmemb;
    std::cout<<"DATA"<<recvsize<<"\n";
    return size*nmemb;
}

struct KEY
{
    char key[64];
}__attribute__((packed));

int main(int argc, char* argv[])
{
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl) {
        char video_info_query[512] = {0};
        avltree<KEY, char*> parsing_response;
        std::vector<char*> video_list;
        std::vector< avltree<KEY, char*> > parsing_video_info;

        sprintf(video_info_query, "https://www.youtube.com/get_video_info?video_id=%s", argv[1]);
        curl_easy_setopt(curl, CURLOPT_URL, video_info_query);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        res = curl_easy_perform(curl);

        if(res != CURLE_OK) fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        char* cstr = new char [response.length()+1];
        std::strcpy (cstr, response.c_str());
        char * item = std::strtok (cstr,"&");
        while (item!=0)
        {
            unsigned int key_len = 0;
            for( ; key_len < std::strlen(item) ; key_len++)
            {
                if(item[key_len] == '=')
                {
                    KEY key = {0};
                    std::strncpy(key.key, item, key_len);
                    char* value = new char[std::strlen(item) - key_len];
                    std::strcpy(value, item+key_len+1);

                    parsing_response.insert(key, value);
                    break;
                }
            }
            item = std::strtok(NULL,"&");
        }
        delete []cstr;

        KEY key = {0};
        std::strcpy(key.key, "url_encoded_fmt_stream_map");
        if(parsing_response.find(key) != nullptr)
        {
            int out;
            char* stream_map = curl_easy_unescape(curl, (*parsing_response.find(key)), std::strlen((*parsing_response.find(key))), &out);
            char* video = std::strtok(stream_map, ",");
            while(video != 0)
            {
                char* video_info = new char[std::strlen(video)+1];
                memset(video_info, 0x0, std::strlen(video)+1);
                std::strcpy(video_info, video);
                video_list.push_back(video_info);
                video = std::strtok(nullptr, ",");
            }
            curl_free(stream_map);
        }
        parsing_response.perform_for_all_data([](char* data){delete [] data;});

        for(unsigned int i = 0 ; i < video_list.size() ; i++)
        {
            avltree<KEY, char*> video;
            std::cout<<"=============================================================\n";
            int out;
            char* video_tmp = curl_easy_unescape(curl, video_list[i], std::strlen(video_list[i]), &out);
            char* item = std::strtok(video_tmp, "&?");
            while(item != 0)
            {
                if(std::strncmp(item, "url", std::strlen("url")) == 0)
                {
                    KEY key = {0};
                    char* value = nullptr;

                    std::strncpy(key.key, item, std::strlen("url"));
                    value = new char[std::strlen(item) - std::strlen("url")];
                    memset(value, 0x0, std::strlen(item) - std::strlen("url"));
                    std::strcpy(value, item+std::strlen("url")+1);

                    video.insert(key, value);
                }
                else if(std::strncmp(item, "type", std::strlen("type")) == 0)
                {
                    std::cout<<item<<"\n";
                }
                else if(std::strncmp(item, "fallback_host", std::strlen("fallback_host")) == 0)
                {
                }
                else
                {
                    unsigned int key_len = 0;
                    KEY key = {0};
                    char* value = nullptr;

                    while(item[++key_len] != '=');

                    std::strncpy(key.key, item, key_len);
                    value = new char[std::strlen(item) - key_len];
                    memset(value, 0x0, std::strlen(item) - key_len);
                    std::strcpy(value, item+key_len+1);

                    video.insert(key, value);
                }
                item = std::strtok(nullptr, "&?");
            }
            curl_free(video_tmp);
            std::string url;
            std::string param;
            std::string query;
            video.perform_for_all_key_data([&](KEY k, char* data){
                if(std::strncmp(k.key, "url", 3) == 0)
                {
                    for(unsigned int i = 0 ; i < std::strlen(data) ; i++)
                    {
                        url.push_back(data[i]);
                    }
                }
                else
                {
                    for(unsigned int i = 0 ; i < std::strlen(k.key) ; i++)
                    {
                        param.push_back(k.key[i]);
                    }
                    param.push_back('=');
                    for(unsigned int i = 0 ; i < std::strlen(data) ; i++)
                    {
                        param.push_back(data[i]);
                    }
                    param.push_back('&');
                }
            });
            param.pop_back();
            char choice;
            std::cout<<"want to download?\n";
            std::cin >> choice;
            if(choice == 'y' || choice == 'Y')
            {
                query = url+"?"+param;
                std::cout<<query<<"\n";

                curl_easy_setopt(curl, CURLOPT_URL, query.c_str());
                response.clear();
                outfile = fopen("out.mp4", "wb");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_data);

                res = curl_easy_perform(curl);
                fclose(outfile);
            }

            video.perform_for_all_data([](char* data){delete [] data;});
            video.clear();
        }
        curl_easy_cleanup(curl);
    }
    return 0;
}
