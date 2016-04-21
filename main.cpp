#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESPONSE_DATA_SIZE (1024*64)
#define MAX_RESPONSE_HEADER_SIZE (1024)
#define MAX_URL_SIZE (1024*10)
#define MAX_FILENAME_SIZE 64

#define YT_CLIP_TYPE_UNKNOWN 0
#define YT_CLIP_TYPE_WEBM 1
#define YT_CLIP_TYPE_MP4 2
#define YT_CLIP_TYPE_3GPP 3
#define YT_CLIP_TYPE_FLV 4

#define MY_DBG			printf("%s: %u\n", __func__, __LINE__)

struct ResponseHeaderBuffer
{
    char buffer[MAX_RESPONSE_HEADER_SIZE];
    unsigned int header_size;
};

struct ResponseDataBuffer
{
    char buffer[MAX_RESPONSE_DATA_SIZE];
    unsigned int payload_size;
};

static size_t write_payload(char *buffer, size_t size, size_t nitems, void *userdata)
{
    ResponseDataBuffer* const r_buff = (ResponseDataBuffer*)userdata;
    if(r_buff->payload_size + size*nitems < MAX_RESPONSE_DATA_SIZE)
    {
        memcpy(r_buff->buffer+r_buff->payload_size, buffer, size*nitems);
        r_buff->payload_size += size * nitems;
    }
    return size*nitems;
}

unsigned int total_size = 0;
static size_t write_header(char *buffer, size_t size, size_t nitems, void *userdata)
{
    for(unsigned int i = 0 ; i < size*nitems ; i++)
    {
        ((struct ResponseHeaderBuffer*)userdata)->buffer[((struct ResponseHeaderBuffer*)userdata)->header_size++] = buffer[i];
        if(buffer[i] == '\n')
        {
            if(strstr(((struct ResponseHeaderBuffer*)userdata)->buffer, "Content-Length:") != NULL)
            {
                sscanf(((struct ResponseHeaderBuffer*)userdata)->buffer, "Content-Length: %u", &total_size);
            }
            memset(((struct ResponseHeaderBuffer*)userdata)->buffer, 0x0, MAX_RESPONSE_HEADER_SIZE);
            ((struct ResponseHeaderBuffer*)userdata)->header_size = 0;
        }
    }
    return size*nitems;
}

unsigned int dl_size = 0;
static size_t fwrite_payload(char *buffer, size_t size, size_t nitems, void *userdata)
{
        dl_size  = dl_size + (size*nitems);
        printf("\r");
        printf("[%3u%%]<", dl_size*100 / total_size);
        for(unsigned int i = 0 ; i < dl_size*50 / total_size ; i++)
        {
            printf("=");
        }
        for(unsigned int i = 0 ; i < 50 - dl_size*50 / total_size ; i++)
        {
            printf("-");
        }
        printf(">");
        fflush(stdout);
        return fwrite(buffer, size, nitems, (FILE*)userdata);
}

unsigned int get_url_with_format(const char* clip_url, char* video_request_url, unsigned int video_request_url_length, unsigned char format)
{
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(curl) {
        struct ResponseDataBuffer* response_buffer;
        char clip_id[32] = {0};
        char video_info_query[512] = {0};
        char* curl_stream_map = 0;
        char* curl_video_item = 0;

        {// 0. Request video information
            response_buffer = (struct ResponseDataBuffer*)malloc(sizeof(struct ResponseDataBuffer));
            if(response_buffer == 0)
            {
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }

            sscanf(clip_url, "https://www.youtube.com/watch?v=%s", clip_id);
            sprintf(video_info_query, "https://www.youtube.com/get_video_info?video_id=%s", clip_id);
            curl_easy_setopt(curl, CURLOPT_URL, video_info_query);
            memset(response_buffer->buffer, 0x0, MAX_RESPONSE_DATA_SIZE);
            response_buffer->payload_size = 0;
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response_buffer);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_payload);
            res = curl_easy_perform(curl);
            if(res != CURLE_OK){
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
        }

        {// 1. Find url_encoded_fmt_stream_map from the response
            char* url_encoded_fmt_stream_map_start = strstr(response_buffer->buffer, "url_encoded_fmt_stream_map=");
            if(url_encoded_fmt_stream_map_start == NULL)
            {
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
            char* url_encoded_fmt_stream_map_end = strchr(url_encoded_fmt_stream_map_start+1, '&');
            if(url_encoded_fmt_stream_map_end == NULL) // This is a case where url_encoded_fmt_stream_map is the last element
            {
                url_encoded_fmt_stream_map_end = strchr(url_encoded_fmt_stream_map_start+1, 0);
            }
            if(url_encoded_fmt_stream_map_end == NULL)
            {
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
            int curl_easy_unescape_out;
            curl_stream_map = curl_easy_unescape(curl,
                                                 url_encoded_fmt_stream_map_start+strlen("url_encoded_fmt_stream_map="),
                                                 url_encoded_fmt_stream_map_end-url_encoded_fmt_stream_map_start-strlen("url_encoded_fmt_stream_map="),
                                                 &curl_easy_unescape_out);
            if(curl_stream_map == NULL)
            {
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
        }
        free(response_buffer);

        {// 2. Find video information from stream_map
            char* video_item_start = curl_stream_map;
            char* video_item_end = strchr(video_item_start+1, ',');
            if(video_item_end == NULL)
            {
                video_item_end = strchr(video_item_start+1, 0);
            }
            if(video_item_end == NULL)
            {
                curl_free(curl_stream_map);
                curl_stream_map = 0;
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
            video_item_start = video_item_start-1; // Trick...for curl_video_item = curl_easy_unescape(curl, video_item_start+1, video_item_end - video_item_start-1, &curl_easy_unescape_out);
            while(video_item_start)
            {
                int curl_easy_unescape_out;
                curl_video_item = curl_easy_unescape(curl, video_item_start+1, video_item_end - video_item_start-1, &curl_easy_unescape_out);
                if(curl_video_item == NULL)
                {
                    curl_free(curl_stream_map);
                    curl_stream_map = 0;
                    curl_easy_cleanup(curl);
                    curl = 0;
                    curl_global_cleanup();
                    return 0;
                }
                switch(format)
                {
                case YT_CLIP_TYPE_WEBM:
                    if(strstr(curl_video_item, "type=video/webm") != NULL)
                    {
                        goto match_found;
                    }
                    break;
                case YT_CLIP_TYPE_MP4:
                    if(strstr(curl_video_item, "type=video/mp4") != NULL)
                    {
                        goto match_found;
                    }
                    break;
                case YT_CLIP_TYPE_3GPP:
                    if(strstr(curl_video_item, "type=video/3gpp") != NULL)
                    {
                        goto match_found;
                    }
                    break;
                case YT_CLIP_TYPE_FLV:
                    if(strstr(curl_video_item, "type=video/x-flv") != NULL)
                    {
                        goto match_found;
                    }
                    break;
                }
                video_item_start = strchr(video_item_start+1, ',');
                if(video_item_start == NULL)
                {
                    curl_free(curl_video_item);
                    curl_video_item = 0;
                    curl_free(curl_stream_map);
                    curl_stream_map = 0;
                    curl_easy_cleanup(curl);
                    curl = 0;
                    curl_global_cleanup();
                    return 0;
                }
                video_item_end = strchr(video_item_start+1, ',');
                if(video_item_end == NULL)
                {
                    video_item_end = strchr(video_item_start+1, 0);
                }
                if(video_item_end == NULL)
                {
                    curl_free(curl_video_item);
                    curl_video_item = 0;
                    curl_free(curl_stream_map);
                    curl_stream_map = 0;
                    curl_easy_cleanup(curl);
                    curl = 0;
                    curl_global_cleanup();
                    return 0;
                }
                if(curl_video_item)
                {
                    curl_free(curl_video_item);
                    curl_video_item = 0;
                }
            }
            if(curl_video_item == NULL)
            {
                curl_free(curl_stream_map);
                curl_stream_map = 0;
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
match_found:
            curl_free(curl_stream_map);
            curl_stream_map = 0;
        }

        {// 3. Generate request url with curl_video_item_list
            if(strlen(curl_video_item) >= video_request_url_length)
            {
                curl_free(curl_video_item);
                curl_video_item = 0;
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
            memset(video_request_url, 0x0, video_request_url_length);

            char* url_start = strstr(curl_video_item, "url=");
            if(url_start == NULL)
            {
                curl_free(curl_video_item);
                curl_video_item = 0;
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
            strcpy(video_request_url, url_start+strlen("url="));
            video_request_url[strlen(video_request_url)] = '&';
            strncpy(video_request_url+strlen(video_request_url), curl_video_item, url_start - curl_video_item);
            if(video_request_url[strlen(video_request_url)-1] == '&')
            {
                video_request_url[strlen(video_request_url)-1] = 0;
            }
        }
        curl_free(curl_video_item);
        printf("URL = %s\n", video_request_url);

        {// 5. Remove duplicate itag field. (I don't know why itag field comes twice. Anyway itag should not appear more then once.)
            //test
            char* second_itag_start = strstr(video_request_url, "itag=");
            if(second_itag_start == 0)
            {
                curl_easy_cleanup(curl);
                curl = 0;
                curl_global_cleanup();
                return 0;
            }
            while((second_itag_start = strstr(second_itag_start+1, "&itag=")) != NULL)
            {
                char* second_itag_end = strchr(second_itag_start+1, '&');
                if(second_itag_end == NULL)
                {
                    second_itag_end = strchr(second_itag_start+1, 0);
                }
                if(second_itag_end == NULL)
                {
                    curl_easy_cleanup(curl);
                    curl = 0;
                    curl_global_cleanup();
                    return 0;
                }
                memmove(second_itag_start, second_itag_end, strlen(video_request_url) - (second_itag_end - second_itag_start));
                second_itag_start = second_itag_start -1;
            }
        }
        curl_easy_cleanup(curl);
        curl = 0;
        curl_global_cleanup();
    }
    return 1;
}

void download(char* url, char* filename_extenstion)
{
    FILE* file;
    CURL *curl;
    CURLcode res;
    struct ResponseHeaderBuffer buffer = {0};

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(curl == 0)
    {
        curl_global_cleanup();
        return;
    }

    file = fopen(filename_extenstion, "wb");
    if(file == 0)
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return;
    }
    if(file)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)file);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_payload);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&buffer);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            fclose(file);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return;
        }
        fclose(file);
        printf("\n");
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return;
}

int main(int argc, char* argv[])
{
    char video_request_url[MAX_URL_SIZE] = {0};
    unsigned char format = YT_CLIP_TYPE_UNKNOWN;
    char filename[MAX_FILENAME_SIZE] = {0};

    if(argc != 4)
    {
        printf("Usage: %s url format{webm, mp4, 3gpp, flv} filename", argv[0]);
        return -1;
    }
    if(strcmp(argv[2], "webm") == 0)
    {
        format = YT_CLIP_TYPE_WEBM;
    }
    else if(strcmp(argv[2], "mp4") == 0)
    {
        format = YT_CLIP_TYPE_MP4;
    }
    else if(strcmp(argv[2], "3gpp") == 0)
    {
        format = YT_CLIP_TYPE_3GPP;
    }
    else if(strcmp(argv[2], "flv") == 0)
    {
        format = YT_CLIP_TYPE_FLV;
    }
    else
    {
        printf("Usage: %s url format{webm, mp4, 3gpp, flv}", argv[0]);
        return -1;
    }
    if(get_url_with_format(argv[1], video_request_url, MAX_URL_SIZE, format) == 0)
    {
        printf("Could not find a match\n");
        return -1;
    }
    sprintf(filename ,"%s.%s", argv[3], (format == YT_CLIP_TYPE_WEBM?"wemb":(format == YT_CLIP_TYPE_MP4?"mp4":(format == YT_CLIP_TYPE_3GPP?"3gpp":"flv"))));
    download(video_request_url, filename);
    printf("Downloading is done.\n");
    printf("Enjoy %s.\n", filename);
    return 0;
}

